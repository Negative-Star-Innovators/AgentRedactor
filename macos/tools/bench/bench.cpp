// CoreML vs CPU execution-provider benchmark for AgentRedactor::PIIDetector.
//
// Drives the *production* detection path (BPETokenizer -> Ort::Session ->
// viterbi calibration -> entity merge) by linking libagentredactor-core.a and
// calling PIIDetector::DetectPII. One process per (provider, launch) so
// cold-start costs and cross-process CoreML conversion-cache effects are
// measured honestly.
//
// Usage: bench <cpu|coreml> <model_dir> <input_file> [--warmups N] [--timed N]
//
// Machine-readable metrics are printed to stdout as KEY=VALUE lines; ORT and
// CoreML diagnostics go to stderr (capture both). Relevant env knobs (handled
// in core): AGENTREDACTOR_ORT_LOG_LEVEL=verbose|info|warning,
// AGENTREDACTOR_COREML_CACHE_DIR=<dir>.
#include "pii_detector.h"
#include "utils.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <vector>

namespace {

double RssPeakMb() {
    struct rusage ru {};
    getrusage(RUSAGE_SELF, &ru);
    // macOS: ru_maxrss is in bytes
    return static_cast<double>(ru.ru_maxrss) / (1024.0 * 1024.0);
}

std::string ReadAll(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

double Median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <cpu|coreml> <model_dir> <input_file> [--warmups N] [--timed N]\n", argv[0]);
        return 64;
    }
    const std::string provider = argv[1];
    const char* modelDir = argv[2];
    const char* inputFile = argv[3];
    int warmups = 2, timed = 5;
    for (int i = 4; i + 1 < argc; i += 2) {
        if (std::string(argv[i]) == "--warmups") warmups = std::atoi(argv[i + 1]);
        if (std::string(argv[i]) == "--timed") timed = std::atoi(argv[i + 1]);
    }

    std::printf("BENCH_PROVIDER=%s\n", provider.c_str());
    const auto providers = Ort::GetAvailableProviders();
    std::printf("ORT_PROVIDERS=");
    for (size_t i = 0; i < providers.size(); ++i)
        std::printf("%s%s", i ? "," : "", providers[i].c_str());
    std::printf("\n");

    const std::string utf8 = ReadAll(inputFile);
    if (utf8.empty()) {
        std::fprintf(stderr, "input file empty or unreadable: %s\n", inputFile);
        return 66;
    }
    const std::wstring text = AgentRedactor::Utils::Utf8ToWide(utf8);
    std::printf("INPUT_BYTES=%zu INPUT_WCHARS=%zu\n", utf8.size(), text.size());

    AgentRedactor::PIIDetector detector(modelDir);
    detector.SetProvider(AgentRedactor::Utils::Utf8ToWide(provider));

    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = detector.Initialize();
    const auto t1 = std::chrono::steady_clock::now();
    const double initMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("INIT_OK=%d\n", ok ? 1 : 0);
    std::printf("INIT_MS=%.1f\n", initMs);
    std::printf("ACTIVE_PROVIDER=%ls\n", detector.GetCurrentProvider().c_str());
    std::printf("RSS_AFTER_INIT_MB=%.1f\n", RssPeakMb());
    if (!ok) return 65;

    const auto& types = detector.GetSupportedPIITypes();
    std::printf("ENABLED_TYPES=%zu\n", types.size());

    auto runOnce = [&]() -> std::pair<double, size_t> {
        const auto a = std::chrono::steady_clock::now();
        const auto entities = detector.DetectPII(text, types, 0);
        const auto b = std::chrono::steady_clock::now();
        return {std::chrono::duration<double, std::milli>(b - a).count(), entities.size()};
    };

    for (int i = 0; i < warmups; ++i) {
        auto [ms, n] = runOnce();
        std::printf("WARMUP[%d]_MS=%.1f ENTITIES=%zu\n", i, ms, n);
    }

    std::vector<double> samples;
    samples.reserve(timed);
    for (int i = 0; i < timed; ++i) {
        auto [ms, n] = runOnce();
        samples.push_back(ms);
        std::printf("TIMED[%d]_MS=%.1f ENTITIES=%zu\n", i, ms, n);
    }

    const auto [minIt, maxIt] = std::minmax_element(samples.begin(), samples.end());
    std::printf("LAT_MEDIAN_MS=%.1f\n", Median(samples));
    std::printf("LAT_MIN_MS=%.1f\n", *minIt);
    std::printf("LAT_MAX_MS=%.1f\n", *maxIt);
    std::printf("RSS_END_MB=%.1f\n", RssPeakMb());
    return 0;
}
