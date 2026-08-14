#include "pch.h"
#include "AppState.h"
#include "utils.h"
#include "localization.h"
#include "constants.h"
#include "logging.h"
#include <chrono>
#include <fstream>
#include <thread>

using namespace AgentRedactor;

namespace {
    constexpr UINT WM_APP_NOTIFY_LOG = WM_APP + 1;
    constexpr UINT WM_APP_NOTIFY_STATS = WM_APP + 2;
    constexpr UINT WM_APP_NOTIFY_MODEL = WM_APP + 3;
    constexpr UINT WM_APP_NOTIFY_SETTINGS = WM_APP + 4;
    constexpr wchar_t MSG_WND_CLASS[] = L"AgentRedactorMsgWindow";
}

::AgentRedactor::AppState* g_appState = nullptr;

AppState* AppState::Instance() {
    return g_appState;
}

AppState::AppState() {
    g_appState = this;
}

AppState::~AppState() {
    Shutdown();
    g_appState = nullptr;
}

bool AppState::Initialize(const std::filesystem::path& dataDir) {
    const auto configDir = dataDir.empty() ? Utils::GetAppDataPath() : dataDir;

    EnsureEngineRunning(configDir);

    // The GUI keeps its own file log; mirror the persisted toggle locally so
    // LOG writes from this process behave as before. The engine applies the
    // same setting process-side.
    logManager_.SetLoggingEnabled(Settings()->IsLoggingEnabled());
    // Sensitive logging is session-only and always starts off.
    logManager_.SetShowSensitive(false);

    // Apply any saved language override before any UI resources are loaded.
    ::AgentRedactor::InitializeLocalization();

    messageHwnd_ = CreateMessageWindow();
    if (!messageHwnd_) {
        LOG_LIFECYCLE(L"[AppState] Failed to create message window");
    }

    systemTray_ = std::make_unique<SystemTray>(messageHwnd_);
    HICON trayIcon = SystemTray::LoadIconFromFile(L"app.ico", 32);
    if (!trayIcon) trayIcon = SystemTray::CreateGradientIcon(32);
    systemTray_->Create(trayIcon, ::AgentRedactor::LocString(L"AppDisplayName").c_str());
    systemTray_->SetOnLeftClick([this]() { OpenWindow(); });
    systemTray_->SetOnRightClick([this]() { ShowTrayMenu(); });

    if (Settings()->IsStartOnBoot()) {
        RegisterStartupTask();
    } else {
        UnregisterStartupTask();
    }

    pollStop_ = false;
    pollThread_ = std::thread(&AppState::StatusPollLoop, this);
    return true;
}

void AppState::Shutdown() {
    LOG(L"=== Agent Redactor Shutdown ===");
    pollStop_ = true;
    if (pollThread_.joinable()) {
        pollThread_.join();
    }
    // Stop only the engine this GUI instance spawned; an engine that was
    // already running (e.g. left behind by an update) is left alone.
    if (engineSpawned_) {
        engineClient_.Post(L"/engine/stop", json::object());
        engineSpawned_ = false;
    } else if (Settings()->IsMasterPasswordEnabled()) {
        // The engine outlives this GUI: lock the session so the next open
        // must authenticate (Windows Hello or password) again — proxies keep
        // running, only sensitive reads/settings are gated.
        engineClient_.Put(L"/settings/lock", json::object());
    }
    if (systemTray_) systemTray_->Destroy();
    if (messageHwnd_) {
        DestroyWindow(messageHwnd_);
        messageHwnd_ = nullptr;
    }
}

void AppState::SetOnLogAdded(std::function<void()> cb) {
    std::lock_guard lock(callbackMutex_);
    onLogAdded_ = std::move(cb);
}

void AppState::SetOnStatsUpdated(std::function<void()> cb) {
    std::lock_guard lock(callbackMutex_);
    onStatsUpdated_ = std::move(cb);
}

void AppState::SetOnSettingsChanged(std::function<void()> cb) {
    std::lock_guard lock(callbackMutex_);
    onSettingsChanged_ = std::move(cb);
}

void AppState::SetOnSessionLockOverlay(std::function<void(bool visible)> cb) {
    std::lock_guard lock(callbackMutex_);
    onSessionLockOverlay_ = std::move(cb);
}

void AppState::SetSessionLockOverlay(bool visible) {
    std::function<void(bool)> cb;
    {
        std::lock_guard lock(callbackMutex_);
        cb = onSessionLockOverlay_;
    }
    if (cb) cb(visible);
}

void AppState::NotifyLogAdded() {
    if (messageHwnd_) {
        PostMessage(messageHwnd_, WM_APP_NOTIFY_LOG, 0, 0);
    }
}

void AppState::NotifyStatsUpdated() {
    if (messageHwnd_) {
        PostMessage(messageHwnd_, WM_APP_NOTIFY_STATS, 0, 0);
    }
}

void AppState::NotifySettingsChanged() {
    if (messageHwnd_) {
        PostMessage(messageHwnd_, WM_APP_NOTIFY_SETTINGS, 0, 0);
    }
}

bool AppState::GetSettingsSnapshot(json& out) const {
    std::lock_guard lock(settingsMutex_);
    if (!settingsValid_) return false;
    out = settingsCache_;
    return true;
}

// ---------------------------------------------------------------------------
// First-run model download state: owned by the engine, polled via /status.
// ---------------------------------------------------------------------------

void AppState::SetOnModelDownloadStatus(std::function<void()> cb) {
    std::lock_guard lock(callbackMutex_);
    onModelDownloadStatus_ = std::move(cb);
}

void AppState::NotifyModelDownloadStatus() {
    if (messageHwnd_) {
        PostMessage(messageHwnd_, WM_APP_NOTIFY_MODEL, 0, 0);
    }
}

bool AppState::IsModelDownloadRequired() const {
    std::lock_guard lock(statusMutex_);
    return statusValid_ && statusCache_.value("modelDownloadRequired", false);
}

bool AppState::IsModelDownloadInProgress() const {
    std::lock_guard lock(statusMutex_);
    return statusValid_ && statusCache_.value("modelDownloadInProgress", false);
}

bool AppState::HasModelDownloadFailed() const {
    std::lock_guard lock(statusMutex_);
    return statusValid_ && statusCache_.value("modelDownloadFailed", false);
}

std::wstring AppState::ModelDownloadStatus() const {
    std::lock_guard lock(statusMutex_);
    if (!statusValid_) return L"";
    return Utils::Utf8ToWide(statusCache_.value("modelDownloadStatus", ""));
}

int AppState::ModelDownloadPercent() const {
    std::lock_guard lock(statusMutex_);
    return statusValid_ ? statusCache_.value("modelDownloadPercent", -1) : -1;
}

void AppState::RetryModelDownload() {
    StartModelDownloadIfNeeded();
}

void AppState::StartModelDownloadIfNeeded() {
    engineClient_.Post(L"/engine/download-model", json::object());
}

HWND AppState::CreateMessageWindow() {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = MessageWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = MSG_WND_CLASS;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, MSG_WND_CLASS, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, this);
    return hwnd;
}

LRESULT CALLBACK AppState::MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* app = reinterpret_cast<AppState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!app) return DefWindowProc(hwnd, msg, wParam, lParam);

    if (msg == WM_APP_NOTIFY_LOG) {
        std::lock_guard lock(app->callbackMutex_);
        if (app->onLogAdded_) app->onLogAdded_();
        return 0;
    }
    if (msg == WM_APP_NOTIFY_STATS) {
        std::lock_guard lock(app->callbackMutex_);
        if (app->onStatsUpdated_) app->onStatsUpdated_();
        return 0;
    }
    if (msg == WM_APP_NOTIFY_MODEL) {
        std::lock_guard lock(app->callbackMutex_);
        if (app->onModelDownloadStatus_) app->onModelDownloadStatus_();
        return 0;
    }
    if (msg == WM_APP_NOTIFY_SETTINGS) {
        std::lock_guard lock(app->callbackMutex_);
        if (app->onSettingsChanged_) app->onSettingsChanged_();
        return 0;
    }
    if (msg == WM_TRAYICON) {
        if (app->systemTray_) {
            app->systemTray_->HandleTrayMessage(lParam);
        }
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Engine lifecycle + status polling
// ---------------------------------------------------------------------------

bool AppState::EnsureEngineRunning(const std::filesystem::path& configDir) {
    engineClient_.Connect(configDir);
    if (engineClient_.Ping()) {
        return true;
    }

    LOG_LIFECYCLE(L"[AppState] Engine not reachable; spawning agentredactor.exe");
    engineSpawned_ = SpawnEngine();
    if (!engineSpawned_) {
        LOG_LIFECYCLE(L"[AppState] Failed to spawn the engine process");
    }

    // Wait for the control API to answer. The engine loads the ONNX model
    // during startup, so allow generous time; the GUI keeps working against
    // cached (default) state if this times out.
    for (int attempt = 0; attempt < 300; ++attempt) {
        engineClient_.Connect(configDir);
        if (engineClient_.Ping()) {
            json j;
            if (engineClient_.Get(L"/status", j)) {
                std::lock_guard lock(statusMutex_);
                statusCache_ = std::move(j);
                statusValid_ = true;
            }
            LOG_LIFECYCLE(L"[AppState] Connected to the engine control API");
            return true;
        }
        Sleep(100);
    }
    LOG_LIFECYCLE(L"[AppState] WARNING: engine did not answer within 30 seconds");
    return false;
}

bool AppState::SpawnEngine() {
    // GetExecutablePath() already returns the exe's directory.
    std::wstring exeDir = Utils::GetExecutablePath().wstring();
    std::wstring enginePath = exeDir + L"\\agentredactor.exe";
    if (!Utils::FileExists(enginePath)) {
        LOG_LIFECYCLE(L"[AppState] Engine executable not found: " + enginePath);
        return false;
    }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring cmdLine = L"\"" + enginePath + L"\"";
    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
            nullptr, exeDir.c_str(), &si, &pi)) {
        LOG_LIFECYCLE(L"[AppState] CreateProcess for engine failed. Error: " + std::to_wstring(GetLastError()));
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

void AppState::StatusPollLoop() {
    while (!pollStop_) {
        json j;
        if (engineClient_.Get(L"/status", j)) {
            bool modelChanged = false;
            {
                std::lock_guard lock(statusMutex_);
                if (!statusValid_ ||
                    statusCache_.value("modelDownloadRequired", false) != j.value("modelDownloadRequired", false) ||
                    statusCache_.value("modelDownloadInProgress", false) != j.value("modelDownloadInProgress", false) ||
                    statusCache_.value("modelDownloadFailed", false) != j.value("modelDownloadFailed", false) ||
                    statusCache_.value("modelDownloadPercent", -1) != j.value("modelDownloadPercent", -1) ||
                    statusCache_.value("modelDownloadStatus", std::string()) != j.value("modelDownloadStatus", std::string())) {
                    modelChanged = true;
                }
                statusCache_ = std::move(j);
                statusValid_ = true;
            }
            if (modelChanged) {
                NotifyModelDownloadStatus();
            }
            // 1-second poll replacing the old ProxyEngine onUpdate_ push:
            // HomePage re-reads profiles (stats) through the client.
            NotifyStatsUpdated();
        }

        // Settings are engine-owned; the GUI may change them in-session (live
        // language switch) or the CLI may (set <key>). Diff the snapshot so
        // the UI refreshes without a restart, mirroring the in-GUI path.
        json sj;
        if (engineClient_.Get(L"/settings", sj)) {
            bool settingsChanged = false;
            {
                std::lock_guard lock(settingsMutex_);
                if (!settingsValid_ || settingsCache_.dump() != sj.dump()) {
                    settingsChanged = true;
                }
                settingsCache_ = std::move(sj);
                settingsValid_ = true;
            }
            if (settingsChanged) {
                NotifySettingsChanged();
            }
        }
        for (int i = 0; i < 10 && !pollStop_; ++i) {
            Sleep(100);
        }
    }
}

void AppState::RestartProxyServers() {
    engineClient_.Post(L"/engine/restart-listeners", json::object());
}

bool AppState::IsProxyRunning(int port) const {
    std::lock_guard lock(statusMutex_);
    if (!statusValid_ || !statusCache_.contains("profiles")) return false;
    for (const auto& p : statusCache_["profiles"]) {
        if (p.value("port", 0) == port) {
            return p.value("proxyRunning", false);
        }
    }
    return false;
}

std::vector<int> AppState::GetRunningPorts() const {
    std::vector<int> ports;
    std::lock_guard lock(statusMutex_);
    if (!statusValid_ || !statusCache_.contains("profiles")) return ports;
    for (const auto& p : statusCache_["profiles"]) {
        if (p.value("proxyRunning", false)) {
            ports.push_back(p.value("port", 0));
        }
    }
    return ports;
}

void AppState::ShowTrayMenu() {
    if (!systemTray_) return;

    std::wstring currentLang = ::AgentRedactor::GetCurrentLanguage();

    std::vector<MenuItem> languageItems;
    for (size_t i = 0; i < SUPPORTED_LANGUAGES.size(); ++i) {
        const auto& lang = SUPPORTED_LANGUAGES[i];
        UINT menuId = ID_TRAY_LANGUAGE_FIRST + static_cast<UINT>(i);
        bool active = LanguageMatches(currentLang, lang.tag);
        languageItems.push_back(MenuItem::Item(lang.nativeName.c_str(), menuId,
            [this, tag = lang.tag]() {
                std::wstring currentLang = ::AgentRedactor::GetCurrentLanguage();
                if (tag == currentLang) {
                    if (systemTray_) {
                        systemTray_->ShowNotification(
                            ::AgentRedactor::LocString(L"TrayMenu_LanguageChanged_Title").c_str(),
                            ::AgentRedactor::LocString(L"TrayMenu_LanguageSame_Message").c_str(),
                            NIIF_INFO);
                    }
                } else {
                    SetLanguage(tag);
                }
            }, true, active));
    }

    std::vector<MenuItem> items;
    items.push_back(MenuItem::Item(::AgentRedactor::LocString(L"TrayMenu_Open").c_str(), ID_TRAY_OPEN, [this]() { OpenWindow(); }));
    items.push_back(MenuItem::Submenu(::AgentRedactor::LocString(L"TrayMenu_Language").c_str(), std::move(languageItems)));
    items.push_back(MenuItem::Separator());
    items.push_back(MenuItem::Item(::AgentRedactor::LocString(L"TrayMenu_StartOnBoot").c_str(), ID_TRAY_START_ON_BOOT,
        [this]() { ToggleStartOnBoot(); }, true, Settings()->IsStartOnBoot()));
    items.push_back(MenuItem::Separator());
    items.push_back(MenuItem::Item(::AgentRedactor::LocString(L"TrayMenu_Quit").c_str(), ID_TRAY_QUIT, [this]() { Quit(); }));
    systemTray_->ShowMenu(items);
}

void AppState::SetLanguage(const std::wstring& language) {
    LOG(L"[AppState] SetLanguage called: " + language);
    ::AgentRedactor::SetLanguageOverride(language);

    // Refresh the tray tooltip so it matches the new language.
    if (systemTray_) {
        systemTray_->UpdateTooltip(::AgentRedactor::LocString(L"AppDisplayName").c_str());
    }

    // Notify the main window to re-localize all visible UI in-session.
    if (localizationReloadCallback_) {
        LOG(L"[AppState] Invoking localization reload callback");
        localizationReloadCallback_();
    } else {
        LOG(L"[AppState] No localization reload callback registered");
    }
}

void AppState::OpenWindow() {
    if (mainHwnd_) {
        ShowWindow(mainHwnd_, SW_RESTORE);
        SetWindowPos(mainHwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetWindowPos(mainHwnd_, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(mainHwnd_);
    }
}

void AppState::ToggleStartOnBoot() {
    bool current = Settings()->IsStartOnBoot();
    Settings()->SetStartOnBoot(!current);
    if (!current) {
        RegisterStartupTask();
    } else {
        UnregisterStartupTask();
    }
}

void AppState::Quit() {
    if (onMainWindowClose_) {
        onMainWindowClose_();
    }
}

void AppState::Restart() {
    std::wstring exePath = Utils::GetExecutablePath().wstring();
    std::wstring exeDir = Utils::GetExecutablePath().parent_path().wstring();
    DWORD currentPid = GetCurrentProcessId();

    // Write a small temporary batch helper that polls until this process exits
    // (so the single-instance mutex is released) and then starts a new instance.
    wchar_t tempDir[MAX_PATH];
    DWORD tempLen = GetTempPathW(MAX_PATH, tempDir);
    if (tempLen == 0) {
        LOG_LIFECYCLE(L"[AppState] Restart failed: could not get temp path. Error: " + std::to_wstring(GetLastError()));
        return;
    }
    std::wstring batchPath = std::wstring(tempDir) + L"AgentRedactor_restart_" + std::to_wstring(currentPid) + L".cmd";

    std::wstring batchContent =
        L"@echo off\r\n"
        L":wait\r\n"
        L"tasklist /FI \"PID eq " + std::to_wstring(currentPid) + L"\" 2>nul | find \"" + std::to_wstring(currentPid) + L"\" >nul\r\n"
        L"if errorlevel 1 goto start\r\n"
        L"timeout /t 1 /nobreak >nul\r\n"
        L"goto wait\r\n"
        L":start\r\n"
        L"start \"\" /D \"" + exeDir + L"\" \"" + exePath + L"\"\r\n"
        L"del /F /Q \"" + batchPath + L"\"\r\n";

    {
        std::wofstream f(batchPath, std::ios::out | std::ios::trunc);
        if (!f) {
            LOG_LIFECYCLE(L"[AppState] Restart failed: could not write helper batch file: " + batchPath);
            return;
        }
        f << batchContent;
    }

    std::wstring cmdLine = L"cmd.exe /c \"" + batchPath + L"\"";

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    DWORD creationFlags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;

    // Try to break away from any job object so the helper survives our exit.
    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                        creationFlags | CREATE_BREAKAWAY_FROM_JOB,
                        nullptr, exeDir.c_str(), &si, &pi)) {
        LOG_LIFECYCLE(L"[AppState] Restart breakaway attempt failed, retrying without breakaway. Error: " + std::to_wstring(GetLastError()));
        if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                            creationFlags,
                            nullptr, exeDir.c_str(), &si, &pi)) {
            LOG_LIFECYCLE(L"[AppState] Restart failed to launch helper process. Error: " + std::to_wstring(GetLastError()));
            return;
        }
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    restarting_ = true;
    Quit();
}
