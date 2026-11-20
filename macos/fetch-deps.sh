#!/usr/bin/env bash
# Downloads the pinned ONNX Runtime prebuilt library into macos/third_party/onnxruntime/.
# The binary blob is gitignored; this script is the source of truth for the version.
#
# Source: the official NuGet package (Microsoft.ML.OnnxRuntime), NOT GitHub
# Releases. Microsoft removed/disabled the microsoft/onnxruntime GitHub
# repository's releases in Nov 2026 (all release-download URLs 404), while
# nuget.org keeps hosting the identical prebuilt binaries. The NuGet package
# ships one dylib per platform whose embedded install name is the same
# @rpath/libonnxruntime.1.dylib the release tarballs used (verified), so
# linking, rpaths, and build-release.sh/build-store.sh packaging are unchanged;
# the script recreates the tarball's versioned symlink names for CMake.
#
# Also verifies the other macOS build prerequisites that CMake needs:
#   - nlohmann-json (header-only; `brew install nlohmann-json`)
#   - OpenSSL 3 (`brew install openssl@3`) — used for the AES-GCM/PBKDF2 secure-storage
#     envelope, byte-identical to the Linux OpenSSL envelope
#   - libcurl — the macOS SDK ships it; no action needed
set -euo pipefail

ORT_VERSION="1.29.0"
NUGET_PACKAGE_ID="microsoft.ml.onnxruntime"
NUGET_SHA256="4557021fec4da3924e2e12ff44b1cc50b176de0d2c5c7daa89ab11c208b3a454"
URL="https://api.nuget.org/v3-flatcontainer/${NUGET_PACKAGE_ID}/${ORT_VERSION}/${NUGET_PACKAGE_ID}.${ORT_VERSION}.nupkg"

ROOT="$(cd "$(dirname "$0")" && pwd)"
DEST="${ROOT}/third_party/onnxruntime"

if [ -f "${DEST}/include/onnxruntime_cxx_api.h" ] \
    && ls "${DEST}"/lib/libonnxruntime.*.dylib >/dev/null 2>&1; then
    echo "onnxruntime ${ORT_VERSION} already present in ${DEST}"
else
    TMP="$(mktemp -d)"
    trap 'rm -rf "${TMP}"' EXIT

    echo "Downloading ${URL}"
    curl -fsSL -o "${TMP}/onnxruntime.nupkg" "${URL}"

    echo "${NUGET_SHA256}  ${TMP}/onnxruntime.nupkg" | shasum -a 256 -c -

    # A .nupkg IS a zip archive despite the extension.
    unzip -q "${TMP}/onnxruntime.nupkg" -d "${TMP}/pkg"

    rm -rf "${DEST}"
    mkdir -p "${DEST}/include" "${DEST}/lib"
    cp -R "${TMP}/pkg/build/native/include/." "${DEST}/include/"
    cp "${TMP}/pkg/runtimes/osx-arm64/native/libonnxruntime.dylib" "${DEST}/lib/"
    # Recreate the release-tarball layout: CMake links the versioned name and
    # the packaging scripts copy through libonnxruntime.1.dylib (cp -L).
    ln -sf libonnxruntime.dylib "${DEST}/lib/libonnxruntime.1.dylib"
    ln -sf libonnxruntime.dylib "${DEST}/lib/libonnxruntime.${ORT_VERSION}.dylib"

    echo "onnxruntime ${ORT_VERSION} extracted to ${DEST}"
fi

# --- Prerequisites that CMake's find_package resolves ------------------------------

if [ -f /opt/homebrew/include/nlohmann/json.hpp ] \
    || [ -f /usr/local/include/nlohmann/json.hpp ] \
    || ls /opt/homebrew/opt/nlohmann-json/include/nlohmann/json.hpp >/dev/null 2>&1; then
    echo "nlohmann_json: found"
else
    echo "nlohmann_json NOT found — install it with: brew install nlohmann-json" >&2
    exit 1
fi

if [ -d /opt/homebrew/opt/openssl@3 ] || [ -d /usr/local/opt/openssl@3 ]; then
    echo "openssl@3: found"
else
    echo "openssl@3 NOT found — install it with: brew install openssl@3" >&2
    exit 1
fi

echo "libcurl: macOS system (SDK)"
