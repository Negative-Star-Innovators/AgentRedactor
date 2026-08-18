# Agent Redactor core library — OS-agnostic proxy / NER / redaction engine.
#
# The Windows build compiles these sources via the vcxproj files under
# windows/; this CMake target is used by the Linux build (linux/CMakeLists.txt).
# localization.cpp is MRT/WinRT-bound and stays Windows-only; the Linux engine
# uses the English-only LocString shim pattern (see windows/engine/engine_loc.cpp).
cmake_minimum_required(VERSION 3.24)
project(agentredactor-core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

# onnxruntime: no official CMake config on all platforms; locate via vcpkg or
# a system install. Include dir must contain onnxruntime_cxx_api.h.
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    PATH_SUFFIXES onnxruntime)
find_library(ONNXRUNTIME_LIB onnxruntime)
if (NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIB)
    message(FATAL_ERROR "onnxruntime not found — install via vcpkg or set ONNXRUNTIME_INCLUDE_DIR/ONNXRUNTIME_LIB")
endif()

set(AR_CORE_SOURCES
    src/api_key_profile.cpp
    src/bpe_tokenizer.cpp
    src/cli.cpp
    src/control_server.cpp
    src/http_server.cpp
    src/keyword_engine.cpp
    src/log_manager.cpp
    src/model_downloader.cpp
    src/pii_detector.cpp
    src/proxy_engine.cpp
    src/redactor.cpp
    src/regex_engine.cpp
    src/settings_manager.cpp
    src/utils.cpp
    src/migrations/settings_migrator.cpp
)

if (WIN32)
    # MRT/WinRT-bound localization compiles on Windows only.
    list(APPEND AR_CORE_SOURCES src/localization.cpp)
else()
    find_package(CURL REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
endif()

add_library(agentredactor-core STATIC ${AR_CORE_SOURCES})

target_include_directories(agentredactor-core
    PUBLIC include
    PRIVATE ${ONNXRUNTIME_INCLUDE_DIR}
)

target_link_libraries(agentredactor-core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE ${ONNXRUNTIME_LIB} Threads::Threads
)

if (NOT WIN32)
    target_include_directories(agentredactor-core PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(agentredactor-core
        PRIVATE CURL::libcurl OpenSSL::SSL OpenSSL::Crypto PkgConfig::LIBSECRET
    )
    target_compile_options(agentredactor-core PRIVATE -Wall -Wextra)
endif()
