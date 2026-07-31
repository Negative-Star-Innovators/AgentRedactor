#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace AgentRedactor {
namespace ModelDownloader {

// The ~1.6 GB model weights (onnx\model_quantized.onnx_data) ship inside the
// MSIX but NOT inside the self-release (Velopack) installer; on first run of
// a self-release install they are downloaded from the `models-v1` GitHub
// release into the fallback directory below (the Velopack install dir is
// treated as immutable). Compiled into both channels; only ever fires when
// the weights are actually missing.

// %LOCALAPPDATA%\AgentRedactor\models
std::filesystem::path GetFallbackModelDir();

// True when the large weight file exists under <modelDir>\onnx.
bool HasModelWeights(const std::filesystem::path& modelDir);

// exe-dir `models` when the weights are present there (MSIX / dev builds),
// otherwise the fallback dir. Pure path resolution; never downloads.
std::filesystem::path ResolveModelDir();

// Synchronous — call from a worker thread. Ensures a complete model directory
// at `fallbackModelDir`: copies the small companion files (tokenizer.json,
// config.json, viterbi_calibration.json, onnx\model_quantized.onnx) from the
// exe-dir models folder when absent, and downloads the large weight file (to
// a `.partial` file, renamed on success). `progress(percent 0-100, message)`
// is optional and may be called frequently. Returns false on any failure.
bool EnsureModelFiles(const std::filesystem::path& fallbackModelDir,
    const std::function<void(int percent, const std::wstring& message)>& progress);

} // namespace ModelDownloader
} // namespace AgentRedactor
