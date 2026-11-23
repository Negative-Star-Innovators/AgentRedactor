# Agent Redactor core library — OS-agnostic proxy / NER / redaction engine.
#
# The Windows build compiles these sources via the vcxproj files under
# windows/; this CMake target is used by the Linux build (linux/CMakeLists.txt).
# localization.cpp is MRT/WinRT-bound and stays Windows-only; the Linux engine
# uses the English-only LocString shim pattern (see windows/engine/engine_loc.cpp).
cmake_minimum_required(VERSION 3.24)
project(agentredactor-core LANGUAGES CXX)

option(AGENTREDACTOR_HF_TOKENIZER "Use Hugging Face tokenizers through the native Rust adapter" ON)
if (AGENTREDACTOR_HF_TOKENIZER)
    find_program(CARGO_EXECUTABLE cargo REQUIRED)
    if (APPLE)
        if (CMAKE_OSX_ARCHITECTURES MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(HF_RUST_TARGET aarch64-apple-darwin)
        else()
            set(HF_RUST_TARGET x86_64-apple-darwin)
        endif()
    elseif (WIN32)
        if (CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            set(HF_RUST_TARGET aarch64-pc-windows-msvc)
        else()
            set(HF_RUST_TARGET x86_64-pc-windows-msvc)
        endif()
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(HF_RUST_TARGET aarch64-unknown-linux-gnu)
    else()
        set(HF_RUST_TARGET x86_64-unknown-linux-gnu)
    endif()
    set(HF_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/hf_tokenizer)
    set(HF_STATIC_LIB ${HF_TARGET_DIR}/${HF_RUST_TARGET}/release/${CMAKE_STATIC_LIBRARY_PREFIX}ar_hf_tokenizer${CMAKE_STATIC_LIBRARY_SUFFIX})
    add_custom_command(
        OUTPUT ${HF_STATIC_LIB}
        COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${HF_TARGET_DIR}
                ${CARGO_EXECUTABLE} build --manifest-path
                ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/hf_tokenizer/Cargo.toml
                --release --target ${HF_RUST_TARGET}
        DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/hf_tokenizer/Cargo.toml
            ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/hf_tokenizer/Cargo.lock
            ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/hf_tokenizer/rust-toolchain.toml
            ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/hf_tokenizer/src/lib.rs
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/hf_tokenizer
        COMMENT "Building Hugging Face tokenizer adapter for ${HF_RUST_TARGET}"
        VERBATIM
    )
    add_custom_target(ar_hf_tokenizer_build DEPENDS ${HF_STATIC_LIB})
    add_library(ar_hf_tokenizer STATIC IMPORTED GLOBAL)
    set_target_properties(ar_hf_tokenizer PROPERTIES IMPORTED_LOCATION ${HF_STATIC_LIB})
    add_dependencies(ar_hf_tokenizer ar_hf_tokenizer_build)
endif()

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
    src/engine_app.cpp
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
elseif (APPLE)
    # macOS: Keychain-backed secure storage (Security.framework) with the same
    # OpenSSL AES-GCM envelope as Linux. LocalAuthentication gates native CLI
    # secret reads; libsecret is Linux-only.
    enable_language(OBJCXX)
    find_package(CURL REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_library(AR_SECURITY_FRAMEWORK Security REQUIRED)
    find_library(AR_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(AR_IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(AR_LOCALAUTH_FRAMEWORK LocalAuthentication REQUIRED)
    list(APPEND AR_CORE_SOURCES src/secure_storage_mac.cpp src/platform_compat.cpp
         src/cli_native_auth_mac.mm)
else()
    find_package(CURL REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
    list(APPEND AR_CORE_SOURCES src/secure_storage_linux.cpp src/platform_compat.cpp)
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
if (AGENTREDACTOR_HF_TOKENIZER)
    target_link_libraries(agentredactor-core PRIVATE ar_hf_tokenizer ${CMAKE_DL_LIBS})
    target_include_directories(agentredactor-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/hf_tokenizer/include)
    target_compile_definitions(agentredactor-core PUBLIC AGENTREDACTOR_HF_TOKENIZER)
    if (NOT APPLE AND NOT WIN32)
        target_link_libraries(agentredactor-core PRIVATE m gcc_s)
    endif()
endif()

if (APPLE)
    target_include_directories(agentredactor-core PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(agentredactor-core
        PRIVATE CURL::libcurl OpenSSL::SSL OpenSSL::Crypto
                ${AR_SECURITY_FRAMEWORK} ${AR_COREFOUNDATION_FRAMEWORK}
                ${AR_IOKIT_FRAMEWORK} ${AR_LOCALAUTH_FRAMEWORK}
    )
    target_compile_options(agentredactor-core PRIVATE -Wall -Wextra)
elseif (NOT WIN32)
    target_include_directories(agentredactor-core PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(agentredactor-core
        PRIVATE CURL::libcurl OpenSSL::SSL OpenSSL::Crypto PkgConfig::LIBSECRET
    )
    target_compile_options(agentredactor-core PRIVATE -Wall -Wextra)
endif()
