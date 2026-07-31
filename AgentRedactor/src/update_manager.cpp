#include "update_manager.h"

#ifdef AGENTREDACTOR_SELFRELEASE

#include "constants.h"
#include "utils.h"
#include "localization.h"
#include <windows.h>

// Same workaround as pch.h: the Windows SDK defines a GetCurrentTime macro
// that collides with WinUI 3 projection headers.
#undef GetCurrentTime

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace AgentRedactor {
namespace UpdateManager {

namespace {

// The Cloudflare worker 302-redirects every file under this prefix to the
// assets of the latest published GitHub release. The host is shared; the
// channel segment is arch-aware: x64 builds use the original "win" channel,
// ARM64 builds use "win-arm64" (matches vpk pack -c in build.ps1).
#define AR_UPDATE_FEED_HOST L"https://api.agentredactor.negativestarinnovators.com/updates/"
#if defined(_M_ARM64)
constexpr const wchar_t* kUpdateChannel = L"win-arm64";
constexpr const wchar_t* kUpdateFeedUrl = AR_UPDATE_FEED_HOST L"win-arm64";
#else
constexpr const wchar_t* kUpdateChannel = L"win";
constexpr const wchar_t* kUpdateFeedUrl = AR_UPDATE_FEED_HOST L"win";
#endif

// Test hook (self-release builds only): AGENTREDACTOR_UPDATE_FEED overrides
// the update feed URL so the upgrade E2E test can point at a local folder
// feed. Unset in normal use.
std::wstring GetUpdateFeedUrl() {
    if (const wchar_t* overrideUrl = _wgetenv(L"AGENTREDACTOR_UPDATE_FEED");
        overrideUrl && *overrideUrl) {
        return overrideUrl;
    }
    return kUpdateFeedUrl;
}

// Test hook (self-release builds only): AGENTREDACTOR_UPDATE_AUTOAPPLY=1
// skips the restart ContentDialog after an update is downloaded and applies
// it + restarts immediately, for unattended upgrade E2E runs.
bool AutoApplyEnabled() {
    const wchar_t* value = _wgetenv(L"AGENTREDACTOR_UPDATE_AUTOAPPLY");
    return value && value[0] == L'1' && value[1] == L'\0';
}

UiDispatch g_uiDispatch;
std::atomic<bool> g_checkRunning{ false };
std::wstring g_availableVersion;      // version of the downloaded package
std::filesystem::path g_downloadedPackage;

// Velopack layout: Update.exe sits in the install root, the app runs from
// <root>\current. Check next to the exe first (vpk pack output folder), then
// the parent directory (installed layout).
std::filesystem::path FindUpdateExe() {
    auto exeDir = Utils::GetExecutablePath();
    std::error_code ec;
    auto local = exeDir / L"Update.exe";
    if (std::filesystem::exists(local, ec)) return local;
    auto parent = exeDir.parent_path() / L"Update.exe";
    if (std::filesystem::exists(parent, ec)) return parent;
    return {};
}

// Single place that builds Update.exe command lines — adjust flags here if
// the bundled Update.exe CLI ever changes (reference: Velopack "Update.exe
// (Windows)" docs; restart-after-apply is the default, --norestart disables).
std::wstring BuildApplyCommand(const std::filesystem::path& updateExe,
    const std::filesystem::path& packagePath, DWORD waitPid) {
    return L"\"" + updateExe.wstring() + L"\" apply --package \"" + packagePath.wstring() +
        L"\" --waitPid " + std::to_wstring(waitPid);
}

struct SemVersion { int parts[3] = { 0, 0, 0 }; bool valid = false; };

SemVersion ParseSemVersion(const std::string& text) {
    SemVersion v;
    std::string core = text;
    // Strip any prerelease/build suffix; releases are always plain x.y.z.
    size_t cut = core.find_first_of("-+");
    if (cut != std::string::npos) core = core.substr(0, cut);
    int index = 0;
    size_t pos = 0;
    while (index < 3) {
        size_t dot = core.find('.', pos);
        std::string piece = (dot == std::string::npos) ? core.substr(pos) : core.substr(pos, dot - pos);
        if (piece.empty()) return v;
        try {
            v.parts[index] = std::stoi(piece);
        } catch (...) {
            return v;
        }
        ++index;
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    v.valid = index > 0;
    return v;
}

bool IsNewerVersion(const std::string& remote, const std::string& local) {
    auto r = ParseSemVersion(remote);
    auto l = ParseSemVersion(local);
    if (!r.valid || !l.valid) return false;
    for (int i = 0; i < 3; ++i) {
        if (r.parts[i] != l.parts[i]) return r.parts[i] > l.parts[i];
    }
    return false;
}

// Result of the worker-thread pipeline.
struct CheckOutcome {
    CheckResult result = CheckResult::Error;
    std::wstring newVersion; // set when result == UpdateReady
};

CheckOutcome CheckAndDownload() {
    auto updateExe = FindUpdateExe();
    if (updateExe.empty()) {
        LOG_LIFECYCLE(L"[UpdateManager] Update.exe not found; not a Velopack install, skipping update check");
        return { CheckResult::Error, {} };
    }

    std::string body;
    std::wstring feedJsonUrl = GetUpdateFeedUrl() + L"/releases." + kUpdateChannel + L".json";
    if (!Utils::HttpGetString(feedJsonUrl, body)) {
        LOG_LIFECYCLE(L"[UpdateManager] Update check failed (feed unreachable)");
        return { CheckResult::Error, {} };
    }

    std::string bestVersion;
    std::string bestFileName;
    try {
        auto feed = json::parse(body);
        for (const auto& asset : feed.value("Assets", json::array())) {
            // Type 1 == Full (nupkg); skip delta/portable/installer assets.
            if (asset.value("Type", 0) != 1) continue;
            std::string version = asset.value("Version", "");
            std::string fileName = asset.value("FileName", "");
            if (version.empty() || fileName.empty()) continue;
            if (bestVersion.empty() || IsNewerVersion(version, bestVersion)) {
                bestVersion = version;
                bestFileName = fileName;
            }
        }
    } catch (const std::exception& e) {
        LOGF_LIFECYCLE(L"[UpdateManager] Failed to parse update feed: %s", Utils::Utf8ToWide(e.what()).c_str());
        return { CheckResult::Error, {} };
    }

    std::string currentVersion = Utils::WideToUtf8(APP_VERSION);
    if (bestVersion.empty() || !IsNewerVersion(bestVersion, currentVersion)) {
        LOGF_LIFECYCLE(L"[UpdateManager] Up to date (current %s, latest %s)",
            APP_VERSION, Utils::Utf8ToWide(bestVersion).c_str());
        return { CheckResult::UpToDate, {} };
    }

    // Download the full package into the Velopack packages directory.
    auto packagesDir = updateExe.parent_path() / L"packages";
    std::error_code ec;
    std::filesystem::create_directories(packagesDir, ec);
    auto packagePath = packagesDir / Utils::Utf8ToWide(bestFileName);
    auto partialPath = packagePath;
    partialPath += L".partial";

    std::wstring packageUrl = GetUpdateFeedUrl() + L"/" + Utils::Utf8ToWide(bestFileName);
    LOGF_LIFECYCLE(L"[UpdateManager] Downloading update %s", Utils::Utf8ToWide(bestVersion).c_str());
    if (!Utils::HttpDownloadFile(packageUrl, partialPath, nullptr)) {
        LOG_LIFECYCLE(L"[UpdateManager] Update download failed");
        std::filesystem::remove(partialPath, ec);
        return { CheckResult::Error, {} };
    }
    std::filesystem::rename(partialPath, packagePath, ec);
    if (ec) {
        LOG_LIFECYCLE(L"[UpdateManager] Failed to finalize downloaded update package");
        std::filesystem::remove(partialPath, ec);
        return { CheckResult::Error, {} };
    }

    g_availableVersion = Utils::Utf8ToWide(bestVersion);
    g_downloadedPackage = packagePath;
    LOGF_LIFECYCLE(L"[UpdateManager] Update %s downloaded", g_availableVersion.c_str());
    return { CheckResult::UpdateReady, g_availableVersion };
}

// Launches `Update.exe apply --package ... --waitPid <self>` and exits the app
// so the updater can swap in the downloaded version and relaunch it.
// Normally called on the UI thread; also tolerates running without a live
// XAML application (auto-apply test hook) by exiting the process directly.
void ApplyDownloadedUpdateAndExit() {
    auto updateExe = FindUpdateExe();
    if (updateExe.empty() || g_downloadedPackage.empty()) return;

    std::wstring command = BuildApplyCommand(updateExe, g_downloadedPackage, GetCurrentProcessId());
    LOGF_LIFECYCLE(L"[UpdateManager] Applying update: %s", command.c_str());

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring mutableCommand = command; // CreateProcessW requires a mutable buffer
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0,
        nullptr, nullptr, &si, &pi)) {
        LOGF_LIFECYCLE(L"[UpdateManager] Failed to launch Update.exe (error %lu)", GetLastError());
        return;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    try {
        if (auto app = winrt::Microsoft::UI::Xaml::Application::Current()) {
            app.Exit();
            return;
        }
    } catch (...) {
    }
    // No running XAML application: exit so Update.exe's --waitPid is released.
    ExitProcess(0);
}

void ShowRestartPrompt(const std::wstring& newVersion) {
    if (!g_uiDispatch) return;
    g_uiDispatch([newVersion](const winrt::Microsoft::UI::Xaml::XamlRoot& xamlRoot) {
        try {
            winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog;
            dialog.XamlRoot(xamlRoot);
            dialog.Title(winrt::box_value(LocString(L"UpdateAvailable_Title")));
            dialog.Content(winrt::box_value(LocFormat(L"UpdateAvailable_Message", { newVersion })));
            dialog.PrimaryButtonText(LocString(L"Dialog_RestartNowButton"));
            dialog.CloseButtonText(LocString(L"Dialog_RestartLaterButton"));
            dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
            dialog.PrimaryButtonClick([](auto&&, auto&&) {
                ApplyDownloadedUpdateAndExit();
            });
            dialog.ShowAsync();
        } catch (...) {
            LOG_LIFECYCLE(L"[UpdateManager] Failed to show update prompt");
        }
    });
}

void RunPipeline(std::function<void(CheckResult)> completion) {
    // One check at a time; a second request while running is dropped quietly.
    bool expected = false;
    if (!g_checkRunning.compare_exchange_strong(expected, true)) return;

    std::thread([completion = std::move(completion)]() {
        CheckOutcome outcome;
        try {
            outcome = CheckAndDownload();
        } catch (...) {
            outcome = { CheckResult::Error, {} };
        }

        if (outcome.result == CheckResult::UpdateReady) {
            if (AutoApplyEnabled()) {
                LOG_LIFECYCLE(L"[UpdateManager] AGENTREDACTOR_UPDATE_AUTOAPPLY=1; applying downloaded update without prompting");
                if (g_uiDispatch) {
                    g_uiDispatch([](const winrt::Microsoft::UI::Xaml::XamlRoot&) {
                        ApplyDownloadedUpdateAndExit();
                    });
                } else {
                    ApplyDownloadedUpdateAndExit();
                }
            } else {
                ShowRestartPrompt(outcome.newVersion);
            }
        }
        if (completion) {
            if (g_uiDispatch) {
                g_uiDispatch([completion, result = outcome.result](const winrt::Microsoft::UI::Xaml::XamlRoot&) {
                    completion(result);
                });
            } else {
                // No UI registered (e.g. window never created): report directly.
                completion(outcome.result);
            }
        }
        g_checkRunning = false;
    }).detach();
}

} // anonymous namespace

void SetUiDispatch(UiDispatch dispatch) {
    g_uiDispatch = std::move(dispatch);
}

void CheckAndDownloadInBackground() {
    RunPipeline(nullptr);
}

void CheckNowInteractive(std::function<void(CheckResult)> completion) {
    RunPipeline(std::move(completion));
}

} // namespace UpdateManager
} // namespace AgentRedactor

#else // !AGENTREDACTOR_SELFRELEASE — Store/MSIX build: all no-ops.

namespace AgentRedactor {
namespace UpdateManager {

void SetUiDispatch(UiDispatch) {}
void CheckAndDownloadInBackground() {}
void CheckNowInteractive(std::function<void(CheckResult)>) {}

} // namespace UpdateManager
} // namespace AgentRedactor

#endif // AGENTREDACTOR_SELFRELEASE
