# Agent Redactor — Linux build driver.
# Builds the OS-agnostic core static library plus the Linux engine/CLI binary.
#
#   cmake -B build -G Ninja \
#     -DONNXRUNTIME_INCLUDE_DIR=~/onnxruntime/include \
#     -DONNXRUNTIME_LIB=~/onnxruntime/lib/libonnxruntime.so
#   cmake --build build
#
# onnxruntime has no apt package; download the official linux-x64 tarball
# (tested with 1.29.0) and point the two knobs at it.
cmake_minimum_required(VERSION 3.24)
project(agentredactor-linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Version source of truth is shared with the Windows build.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../windows/version.txt AR_VERSION LIMIT_COUNT 1)
string(STRIP "${AR_VERSION}" AR_VERSION)
message(STATUS "Agent Redactor version: ${AR_VERSION}")
add_compile_definitions(AR_VERSION_STRING="${AR_VERSION}")

# Self-release builds (Velopack-packaged AppImage) get the updater; plain dev
# builds carry no update code (mirror of AGENTREDACTOR_SELFRELEASE on Windows).
# Requires linux/fetch-velopack.sh to have been run.
option(AR_SELFRELEASE "Enable Velopack self-update in the GUI" OFF)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_BINARY_DIR}/core)

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/engine/CMakeLists.txt)
    add_subdirectory(engine)
endif()

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gui/CMakeLists.txt)
    add_subdirectory(gui)
endif()

# Stage model companion files next to the dev build binaries so a local build
# can refresh them into the fallback model dir and download the ~1.6 GB weights
# on first run. The large weights file (*.onnx_data) is intentionally excluded.
set(AR_MODELS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../windows/models)
if (EXISTS ${AR_MODELS_SRC})
    foreach(_dest ${CMAKE_BINARY_DIR}/engine/models ${CMAKE_BINARY_DIR}/gui/models)
        file(COPY ${AR_MODELS_SRC}/ DESTINATION ${_dest}
            FILES_MATCHING
                PATTERN "*.json"
                PATTERN "onnx/model_quantized.onnx")
    endforeach()
endif()
