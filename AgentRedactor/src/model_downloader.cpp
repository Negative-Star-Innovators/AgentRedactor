#include "model_downloader.h"
#include "constants.h"
#include "utils.h"
#include <windows.h>
#include <shlobj.h>
#include <system_error>

namespace AgentRedactor {
namespace ModelDownloader {

namespace {

// The only large file not in the repo. Served exclusively from the
// Cloudflare R2 endpoint (R2 is the single host for model downloads).
constexpr const wchar_t* kWeightsUrls[] = {
    L"https://api.agentredactor.negativestarinnovators.com/models/model_quantized.onnx_data",
};
constexpr const wchar_t* kWeightsRelativePath = L"onnx\\model_quantized.onnx_data";

// Small companion files that ship with the app next to the exe and are copied
// (not downloaded) into the fallback directory when missing.
constexpr const wchar_t* kCompanionFiles[] = {
    L"tokenizer.json",
    L"config.json",
    L"viterbi_calibration.json",
    L"onnx\\model_quantized.onnx",
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

} // anonymous namespace

std::filesystem::path WeightsFilePath(const std::filesystem::path& modelDir) {
    return WeightsPath(modelDir);
}

std::filesystem::path GetFallbackModelDir() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        return std::filesystem::path(path) / L"AgentRedactor" / MODEL_DIR;
    }
    return std::filesystem::path(L"C:\\AgentRedactor") / MODEL_DIR;
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
        weights.c_str());
    std::filesystem::remove(weights, ec);
    return false;
}

std::filesystem::path ResolveModelDir() {
    auto exeModels = Utils::GetExecutablePath() / MODEL_DIR;
    if (HasModelWeights(exeModels)) return exeModels;
    return GetFallbackModelDir();
}

bool EnsureModelFiles(const std::filesystem::path& fallbackModelDir,
    const std::function<void(int percent, const std::wstring& message)>& progress) {
    if (HasModelWeights(fallbackModelDir)) return true;

    auto exeModels = Utils::GetExecutablePath() / MODEL_DIR;

    // Copy the small companion files from the exe-dir models folder. They are
    // required by PIIDetector (tokenizer / config / calibration / model graph)
    // but are tiny and ship inside both package types.
    for (const auto* relative : kCompanionFiles) {
        auto dest = fallbackModelDir / relative;
        std::error_code ec;
        if (std::filesystem::exists(dest, ec)) continue;
        auto src = exeModels / relative;
        if (!std::filesystem::exists(src, ec)) {
            LOGF_LIFECYCLE(L"[ModelDownloader] Companion file missing next to exe: %s", src.c_str());
            return false;
        }
        std::filesystem::create_directories(dest.parent_path(), ec);
        if (ec) {
            LOGF_LIFECYCLE(L"[ModelDownloader] Failed to create %s: %s",
                dest.parent_path().c_str(), Utils::Utf8ToWide(ec.message()).c_str());
            return false;
        }
        std::filesystem::copy_file(src, dest, ec);
        if (ec) {
            LOGF_LIFECYCLE(L"[ModelDownloader] Failed to copy %s: %s",
                src.c_str(), Utils::Utf8ToWide(ec.message()).c_str());
            return false;
        }
    }

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
        LOGF_LIFECYCLE(L"[ModelDownloader] Failed to create %s", weightsDest.parent_path().c_str());
        return false;
    }

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
        LOGF_LIFECYCLE(L"[ModelDownloader] Failed to finalize %s", weightsDest.c_str());
        return false;
    }

    LOG_LIFECYCLE(L"[ModelDownloader] Model weights downloaded");
    return HasModelWeights(fallbackModelDir);
}

} // namespace ModelDownloader
} // namespace AgentRedactor
