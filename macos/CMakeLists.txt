# Agent Redactor — macOS build driver.
# Builds the OS-agnostic core static library plus the macOS engine/CLI binary.
#
#   macos/fetch-deps.sh    # fetches the pinned onnxruntime tarball + checks prerequisites
#   cmake -B macos/build -G Ninja
#   cmake --build macos/build
#
# Prereqs (Homebrew): cmake, ninja, nlohmann-json, openssl@3. libcurl and the
# Keychain (Security.framework) come from the macOS SDK.
cmake_minimum_required(VERSION 3.24)
project(agentredactor-macos LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Target macOS 14 (Sonoma)+. The pinned onnxruntime build below is osx-arm64;
# an Intel build would substitute the osx-x64 tarball (same version pin).
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

# Velopack self-release builds (macos/build-release.sh passes
# -DAR_SELFRELEASE=ON, mirroring linux/CMakeLists.txt). Nothing consumes the
# define yet — the SwiftUI GUI owns update checks and engine-side Velopack
# wiring will key off this same flag later.
option(AR_SELFRELEASE "Enable Velopack self-update in the engine/GUI" OFF)
if (AR_SELFRELEASE)
    add_compile_definitions(AR_SELFRELEASE)
endif()

# Version source of truth is shared with the Windows and Linux builds.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../windows/version.txt AR_VERSION LIMIT_COUNT 1)
string(STRIP "${AR_VERSION}" AR_VERSION)
message(STATUS "Agent Redactor version: ${AR_VERSION}")
add_compile_definitions(AR_VERSION_STRING="${AR_VERSION}")

# Homebrew OpenSSL 3 (secure-storage envelope parity with the Linux OpenSSL
# build). Only a default; an explicit -DOPENSSL_ROOT_DIR always wins.
if (NOT DEFINED OPENSSL_ROOT_DIR AND EXISTS "/opt/homebrew/opt/openssl@3")
    set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@3" CACHE PATH "OpenSSL root")
endif()

# onnxruntime has no brew formula; fetch-deps.sh downloads the official
# osx-arm64 tarball (pinned + sha256-checked) into third_party/ (gitignored).
# Both knobs remain user-overridable like the Linux build's.
set(AR_ONNXRUNTIME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime)
if (NOT DEFINED ONNXRUNTIME_INCLUDE_DIR AND EXISTS ${AR_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h)
    set(ONNXRUNTIME_INCLUDE_DIR "${AR_ONNXRUNTIME_ROOT}/include" CACHE PATH "onnxruntime include dir")
endif()
if (NOT DEFINED ONNXRUNTIME_LIB AND EXISTS ${AR_ONNXRUNTIME_ROOT}/lib/libonnxruntime.1.29.0.dylib)
    set(ONNXRUNTIME_LIB "${AR_ONNXRUNTIME_ROOT}/lib/libonnxruntime.1.29.0.dylib" CACHE FILEPATH "onnxruntime library")
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_BINARY_DIR}/core)

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/engine/CMakeLists.txt)
    add_subdirectory(engine)
endif()

# Stage model companion files next to the dev build binary so a local build
# can refresh them into the fallback model dir and download the ~1.6 GB
# weights on first run. The large weights file (*.onnx_data) is excluded.
set(AR_MODELS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../windows/models)
if (EXISTS ${AR_MODELS_SRC})
    file(COPY ${AR_MODELS_SRC}/ DESTINATION ${CMAKE_BINARY_DIR}/engine/models
        FILES_MATCHING
            PATTERN "*.json"
            PATTERN "onnx/model_quantized.onnx")
endif()
