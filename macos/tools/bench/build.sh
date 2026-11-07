#!/usr/bin/env bash
# Builds the CoreML-vs-CPU bench into macos/tools/bench/build/.
# Links the same libagentredactor-core.a the engine uses, plus the engine's
# English-only localization shim (core references LocString/LocFormat on
# non-Windows builds). No CMake target is added to the shared macos build.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
BUILD_DIR="$DIR/build"
mkdir -p "$BUILD_DIR"

ORT_ROOT="$ROOT/macos/third_party/onnxruntime"
CORE_LIB="$ROOT/macos/build/core/libagentredactor-core.a"
[ -f "$CORE_LIB" ] || { echo "missing $CORE_LIB — build the core first (cmake --build macos/build)"; exit 1; }

SSL_ROOT="/opt/homebrew/opt/openssl@3"
NLOHMANN_INCLUDE="$(brew --prefix nlohmann-json 2>/dev/null || echo /opt/homebrew)/include"

CXX="${CXX:-clang++}"
"$CXX" -std=c++17 -O2 -Wall \
    -I "$ROOT/core/include" \
    -I "$ORT_ROOT/include" \
    -I "$NLOHMANN_INCLUDE" \
    "$DIR/bench.cpp" \
    "$ROOT/macos/engine/engine_loc.cpp" \
    "$CORE_LIB" \
    -L "$ORT_ROOT/lib" -lonnxruntime \
    -L "$SSL_ROOT/lib" -lssl -lcrypto \
    -lcurl \
    -framework Security -framework CoreFoundation -framework IOKit \
    -pthread \
    -Wl,-rpath,"$ORT_ROOT/lib" \
    -Wl,-rpath,"$SSL_ROOT/lib" \
    -o "$BUILD_DIR/bench"

echo "built $BUILD_DIR/bench"
