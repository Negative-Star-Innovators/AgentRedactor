// Prints the execution providers compiled into the ONNX Runtime build this
// bench links against. Built by build.sh into macos/tools/bench/build/providers.
#include <cstdio>
#include <onnxruntime_cxx_api.h>

int main() {
    const auto providers = Ort::GetAvailableProviders();
    std::printf("Available execution providers (%zu):\n", providers.size());
    for (const auto& p : providers) std::printf("  - %s\n", p.c_str());
    return 0;
}
