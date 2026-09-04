#include "model_downloader.h"
#include "constants.h"
#include "utils.h"
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif
#include <system_error>
#include <fstream>
#include <iterator>

namespace AgentRedactor {
namespace ModelDownloader {

namespace {

// The only large file not in the repo. Served exclusively from the
// Cloudflare R2 endpoint (R2 is the single host for model downloads).
constexpr const wchar_t* kWeightsUrls[] = {
    L"https://api.agentredactor.negativestarinnovators.com/models/model_quantized.onnx_data",
};
constexpr const wchar_t* kWeightsRelativePath = L"onnx/model_quantized.onnx_data";

// Small companion files that ship with the app next to the exe and are copied
// (not downloaded) into the fallback directory when missing.
constexpr const wchar_t* kCompanionFiles[] = {
    L"tokenizer.json",
    L"config.json",
    L"viterbi_calibration.json",
    L"onnx/model_quantized.onnx",
};

std::filesystem::path WeightsPath(const std::filesystem::path& modelDir) {
    return modelDir / kWeightsRelativePath;
}

void ReportProgress(const std::function<void(int, const std::wstring&)>& progress,
    int percent, const std::wstring& message) {
    if (progress) {
        try { progress(percent, message); } catch (...) {}
    }
}

// Companion files are small enough (<= a few hundred KB) that a byte
// compare is cheap and exact.
bool CompanionMatches(const std::filesystem::path& src, const std::filesystem::path& dest) {
    std::error_code ec;
    if (!std::filesystem::exists(dest, ec)) return false;
    const auto srcSize = std::filesystem::file_size(src, ec);
    if (ec) return true;  // source unreadable: leave dest alone
    const auto destSize = std::filesystem::file_size(dest, ec);
    if (ec || srcSize != destSize) return false;
    std::ifstream a(src, std::ios::binary);
    std::ifstream b(dest, std::ios::binary);
    if (!a || !b) return true;  // unreadable: leave dest alone
    return std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(b));
}

} // anonymous namespace

std::filesystem::path WeightsFilePath(const std::filesystem::path& modelDir) {
    return WeightsPath(modelDir);
}

std::filesystem::path GetFallbackModelDir() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        return std::filesystem::path(path) / L"AgentRedactor" / MODEL_DIR;
    }
    return std::filesystem::path(L"C:\\AgentRedactor") / MODEL_DIR;
#else
    // XDG: $XDG_DATA_HOME/agentredactor/models, defaulting to
    // ~/.local/share/agentredactor/models.
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "agentredactor" / "models";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "share" / "agentredactor" / "models";
    }
    return std::filesystem::path("/tmp/agentredactor") / "models";
#endif
}

bool HasModelWeights(const std::filesystem::path& modelDir) {
    std::error_code ec;
    auto weights = WeightsPath(modelDir);
    if (!std::filesystem::exists(weights, ec) || ec) return false;
    auto size = std::filesystem::file_size(weights, ec);
    if (ec) return false;
    if (size == kWeightsExpectedBytes) return true;
    // A wrong-sized file is a truncated or corrupt download: treat it as
    // missing and delete it so the next launch re-downloads (self-heal)
    // instead of failing to initialize the detector forever.
    LOGF_LIFECYCLE(L"[ModelDownloader] Deleting corrupt weights (size %llu, expected %llu): %s",
        static_cast<unsigned long long>(size), static_cast<unsigned long long>(kWeightsExpectedBytes),
        weights.wstring().c_str());
    std::filesystem::remove(weights, ec);
    return false;
}

std::filesystem::path ResolveModelDir() {
    auto exeModels = Utils::GetExecutablePath() / MODEL_DIR;
    if (HasModelWeights(exeModels)) return exeModels;
    return GetFallbackModelDir();
}

namespace {

// Shared companion-refresh body. `weightsPresent` only controls error
// tolerance: with weights already in place a failed copy keeps the older
// (still working) companion; without weights a missing bundle is fatal.
bool RefreshCompanions(const std::filesystem::path& fallbackModelDir, bool weightsPresent) {
    auto exeModels = Utils::GetExecutablePath() / MODEL_DIR;

    // Refresh the small companion files from the exe-dir models folder. They
    // are required by PIIDetector (tokenizer / config / calibration / model
    // graph) and ship inside both package types. Copy when missing OR when
    // the bundled file differs — an upgrade must replace an older graph left
    // in the fallback dir by a previous version (e.g. the sparse-attention
    // model graph), not keep using it. This runs even when the weights are
    // already present, unlike the first-run-only flow before.
    for (const auto* relative : kCompanionFiles) {
        auto dest = fallbackModelDir / relative;
        auto src = exeModels / relative;
        std::error_code ec;
        if (!std::filesystem::exists(src, ec)) {
            if (!weightsPresent) {
                LOGF_LIFECYCLE(L"[ModelDownloader] Companion file missing next to exe (exe dir: %s): %s",
                    exeModels.wstring().c_str(), src.wstring().c_str());
                LOG_LIFECYCLE(L"[ModelDownloader] Cannot download model weights without the bundled model bundle; "
                    L"the app package or dev build is missing its models/ directory.");
                return false;
            }
            continue;  // nothing bundled (dev layout); keep the existing dest
        }
        if (CompanionMatches(src, dest)) continue;
        std::filesystem::create_directories(dest.parent_path(), ec);
        if (ec) {
            LOGF_LIFECYCLE(L"[ModelDownloader] Failed to create %s: %s",
                dest.parent_path().wstring().c_str(), Utils::Utf8ToWide(ec.message()).c_str());
            if (!weightsPresent) return false;
            continue;
        }
        // Copy via temp + rename so a crash mid-copy never leaves a
        // truncated graph that the engine would try to load.
        auto tmp = dest;
        tmp += L".tmp";
        std::filesystem::copy_file(src, tmp, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) std::filesystem::rename(tmp, dest, ec);
        if (ec) {
            LOGF_LIFECYCLE(L"[ModelDownloader] Failed to refresh %s: %s",
                src.wstring().c_str(), Utils::Utf8ToWide(ec.message()).c_str());
            std::filesystem::remove(tmp, ec);
            if (!weightsPresent) return false;
            // Weights present: the older companion still works; keep it.
        }
    }
    return true;
}

} // anonymous namespace

bool RefreshCompanionFiles(const std::filesystem::path& fallbackModelDir) {
    return RefreshCompanions(fallbackModelDir, HasModelWeights(fallbackModelDir));
}

bool EnsureModelFiles(const std::filesystem::path& fallbackModelDir,
    const std::function<void(int percent, const std::wstring& message)>& progress) {
    const bool weightsPresent = HasModelWeights(fallbackModelDir);

    if (!RefreshCompanions(fallbackModelDir, weightsPresent)) return false;

    if (weightsPresent) return true;

    // Download the large weights to a .partial file and rename on success so a
    // killed download never leaves a truncated file looking complete. The
    // download is segmented and resumable; on failure the .partial and .partN
    // files are KEPT so the next retry resumes instead of restarting 1.6 GB.
    auto weightsDest = WeightsPath(fallbackModelDir);
    auto partial = weightsDest;
    partial += L".partial";

    std::error_code ec;
    std::filesystem::create_directories(weightsDest.parent_path(), ec);
    if (ec) {
        LOGF_LIFECYCLE(L"[ModelDownloader] Failed to create %s: %s",
            weightsDest.parent_path().wstring().c_str(), Utils::Utf8ToWide(ec.message()).c_str());
        return false;
    }

    LOGF_LIFECYCLE(L"[ModelDownloader] Weights will be downloaded to %s", weightsDest.wstring().c_str());

    auto lastPercent = -1;
    auto progressCallback = [&](uint64_t downloaded, uint64_t total) {
        int percent = total > 0 ? static_cast<int>(downloaded * 100 / total) : -1;
        if (percent != lastPercent) {
            lastPercent = percent;
            std::wstring message = total > 0
                ? Utils::FormatString(L"%s / %s",
                    Utils::FormatSize(static_cast<size_t>(downloaded)).c_str(),
                    Utils::FormatSize(static_cast<size_t>(total)).c_str())
                : Utils::FormatSize(static_cast<size_t>(downloaded));
            ReportProgress(progress, percent, message);
        }
    };

    // A previous attempt may have fully downloaded but failed to rename.
    bool ok = false;
    auto partialSize = std::filesystem::file_size(partial, ec);
    if (!ec && partialSize == kWeightsExpectedBytes) {
        ok = true;
    } else {
        for (const auto* url : kWeightsUrls) {
            LOGF_LIFECYCLE(L"[ModelDownloader] Downloading model weights (~1.6 GB) from %s", url);
            ok = Utils::HttpDownloadFileSegmented(url, partial, progressCallback);
            if (ok) break;
            LOGF_LIFECYCLE(L"[ModelDownloader] Weight download failed from %s", url);
        }
    }

    if (!ok) {
        LOG_LIFECYCLE(L"[ModelDownloader] Weight download failed");
        return false;
    }

    std::filesystem::rename(partial, weightsDest, ec);
    if (ec) {
        // Keep the complete .partial; the next retry finalizes it without
        // downloading again.
        LOGF_LIFECYCLE(L"[ModelDownloader] Failed to finalize %s", weightsDest.wstring().c_str());
        return false;
    }

    LOG_LIFECYCLE(L"[ModelDownloader] Model weights downloaded");
    return HasModelWeights(fallbackModelDir);
}

} // namespace ModelDownloader
} // namespace AgentRedactor
