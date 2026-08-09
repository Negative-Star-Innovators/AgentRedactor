# Agent Redactor core library — OS-agnostic proxy / NER / redaction engine.
#
# Status (PR 1 — restructure): this file is scaffolding introduced alongside the
# AgentRedactor/ -> windows/ move. It currently builds on Windows only; several
# sources still carry Windows-specific code (localization.cpp uses MRT resources,
# utils.cpp / logging.h use Win32 APIs). The Linux port (PR 3) peels those shims
# behind a platform interface and makes this target cross-platform.
cmake_minimum_required(VERSION 3.24)
project(agentredactor-core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json CONFIG REQUIRED)

# onnxruntime: no official CMake config on all platforms; locate via vcpkg or
# a system install. Include dir must contain onnxruntime_cxx_api.h.
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    PATH_SUFFIXES onnxruntime)
find_library(ONNXRUNTIME_LIB onnxruntime)
if (NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIB)
    message(FATAL_ERROR "onnxruntime not found — install via vcpkg or set ONNXRUNTIME_INCLUDE_DIR/ONNXRUNTIME_LIB")
endif()

add_library(agentredactor-core STATIC
    src/api_key_profile.cpp
    src/bpe_tokenizer.cpp
    src/http_server.cpp
    src/keyword_engine.cpp
    src/localization.cpp
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

target_include_directories(agentredactor-core
    PUBLIC include
    PRIVATE ${ONNXRUNTIME_INCLUDE_DIR}
)

target_link_libraries(agentredactor-core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE ${ONNXRUNTIME_LIB}
)
