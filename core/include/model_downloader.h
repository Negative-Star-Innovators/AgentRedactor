#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace AgentRedactor {
namespace ModelDownloader {

// The ~1.6 GB model weights (onnx\model_quantized.onnx_data) ship inside the
// MSIX but NOT inside the self-release (Velopack) installer; on first run of
// a self-release install they are downloaded into the fallback directory
// below (the Velopack install dir is treated as immutable), trying the
// Cloudflare R2 endpoint first with the `models-v1` GitHub release as
// fallback. Compiled into both channels; only ever fires when the weights are
// actually missing.

// Expected exact size of onnx\model_quantized.onnx_data.
constexpr uint64_t kWeightsExpectedBytes = 1618042064;

// %LOCALAPPDATA%\AgentRedactor\models
std::filesystem::path GetFallbackModelDir();

// Full path of the large weights file under a model dir.
std::filesystem::path WeightsFilePath(const std::filesystem::path& modelDir);

// True when the large weight file exists under <modelDir>\onnx AND has
// exactly kWeightsExpectedBytes. A wrong-sized (truncated/corrupt) file is
// treated as missing and deleted so old corrupt installs self-heal.
bool HasModelWeights(const std::filesystem::path& modelDir);

// exe-dir `models` when the weights are present there (MSIX / dev builds),
// otherwise the fallback dir. Pure path resolution; never downloads.
std::filesystem::path ResolveModelDir();

// Startup-safe companion refresh: copies the small companion files
// (tokenizer.json, config.json, viterbi_calibration.json,
// onnx\model_quantized.onnx) from the exe-dir models folder into
// `fallbackModelDir` when absent or different (upgrade case), but NEVER
// downloads the weights — a first-run download takes minutes and must not
// block engine startup; it is driven by the GUI via the control API
// (StartModelDownloadIfNeeded → EnsureModelFiles). Cheap no-op when the
// companions already match.
bool RefreshCompanionFiles(const std::filesystem::path& fallbackModelDir);

// Synchronous — call from a worker thread. Ensures a complete model directory
// at `fallbackModelDir`: copies the small companion files (tokenizer.json,
// config.json, viterbi_calibration.json, onnx\model_quantized.onnx) from the
// exe-dir models folder when absent, and downloads the large weight file
// (parallel segmented + resumable, to a `.partial` file renamed on success;
// interrupted downloads keep their `.partial`/`.partN` files so a retry
// resumes instead of restarting). `progress(percent 0-100, message)`
// is optional and may be called frequently. Returns false on any failure.
bool EnsureModelFiles(const std::filesystem::path& fallbackModelDir,
    const std::function<void(int percent, const std::wstring& message)>& progress);

} // namespace ModelDownloader
} // namespace AgentRedactor
