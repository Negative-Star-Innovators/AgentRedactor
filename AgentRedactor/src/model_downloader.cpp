#include "model_downloader.h"
#include "constants.h"
#include "utils.h"
#include <windows.h>
#include <shlobj.h>
#include <system_error>

namespace AgentRedactor {
namespace ModelDownloader {

namespace {

// The only large file not in the repo; it is an asset of the `models-v1`
// GitHub release (mirrors the download step in .github/workflows/build-msix.yml).
constexpr const wchar_t* kWeightsUrl =
    L"https://github.com/Negative-Star-Innovators/AgentRedactor/releases/download/models-v1/model_quantized.onnx_data";
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

std::filesystem::path GetFallbackModelDir() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        return std::filesystem::path(path) / L"AgentRedactor" / MODEL_DIR;
    }
    return std::filesystem::path(L"C:\\AgentRedactor") / MODEL_DIR;
}

bool HasModelWeights(const std::filesystem::path& modelDir) {
    std::error_code ec;
    return std::filesystem::exists(WeightsPath(modelDir), ec) && !ec;
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
    // killed download never leaves a truncated file looking complete.
    auto weightsDest = WeightsPath(fallbackModelDir);
    auto partial = weightsDest;
    partial += L".partial";

    std::error_code ec;
    std::filesystem::create_directories(weightsDest.parent_path(), ec);
    if (ec) {
        LOGF_LIFECYCLE(L"[ModelDownloader] Failed to create %s", weightsDest.parent_path().c_str());
        return false;
    }

    LOG_LIFECYCLE(L"[ModelDownloader] Downloading model weights (~1.6 GB)...");
    auto lastPercent = -1;
    bool ok = Utils::HttpDownloadFile(kWeightsUrl, partial,
        [&](uint64_t downloaded, uint64_t total) {
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
        });

    if (!ok) {
        LOG_LIFECYCLE(L"[ModelDownloader] Weight download failed");
        std::filesystem::remove(partial, ec);
        return false;
    }

    std::filesystem::rename(partial, weightsDest, ec);
    if (ec) {
        LOGF_LIFECYCLE(L"[ModelDownloader] Failed to finalize %s", weightsDest.c_str());
        std::filesystem::remove(partial, ec);
        return false;
    }

    LOG_LIFECYCLE(L"[ModelDownloader] Model weights downloaded");
    return HasModelWeights(fallbackModelDir);
}

} // namespace ModelDownloader
} // namespace AgentRedactor
