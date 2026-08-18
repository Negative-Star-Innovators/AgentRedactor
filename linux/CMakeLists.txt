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

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_BINARY_DIR}/core)

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/engine/CMakeLists.txt)
    add_subdirectory(engine)
endif()

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gui/CMakeLists.txt)
    add_subdirectory(gui)
endif()
