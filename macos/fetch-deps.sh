#!/usr/bin/env bash
# Downloads the pinned ONNX Runtime prebuilt library into macos/third_party/onnxruntime/.
# The binary blob is gitignored; this script is the source of truth for the version.
#
# Also verifies the other macOS build prerequisites that CMake needs:
#   - nlohmann-json (header-only; `brew install nlohmann-json`)
#   - OpenSSL 3 (`brew install openssl@3`) — used for the AES-GCM/PBKDF2 secure-storage
#     envelope, byte-identical to the Linux OpenSSL envelope
#   - libcurl — the macOS SDK ships it; no action needed
set -euo pipefail

ORT_VERSION="1.29.0"
ORT_SHA256="d0706fc34f315d8c88639d0a8c81f2e09e815f282cabed3493c06a054352cf92"
URL="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-arm64-${ORT_VERSION}.tgz"

ROOT="$(cd "$(dirname "$0")" && pwd)"
DEST="${ROOT}/third_party/onnxruntime"

if [ -f "${DEST}/include/onnxruntime_cxx_api.h" ] \
    && ls "${DEST}"/lib/libonnxruntime.*.dylib >/dev/null 2>&1; then
    echo "onnxruntime ${ORT_VERSION} already present in ${DEST}"
else
    TMP="$(mktemp -d)"
    trap 'rm -rf "${TMP}"' EXIT

    echo "Downloading ${URL}"
    curl -fsSL -o "${TMP}/onnxruntime.tgz" "${URL}"

    echo "${ORT_SHA256}  ${TMP}/onnxruntime.tgz" | shasum -a 256 -c -

    rm -rf "${DEST}"
    mkdir -p "${DEST}/include" "${DEST}/lib"
    # The tarball root is a versioned directory (with a leading ./ that
    # --strip-components mishandles on bsdtar); extract, then copy the pieces
    # this project links/ships.
    tar -xzf "${TMP}/onnxruntime.tgz" -C "${TMP}"
    INNER="${TMP}/onnxruntime-osx-arm64-${ORT_VERSION}"
    cp -R "${INNER}/include/." "${DEST}/include/"
    cp -R "${INNER}/lib/." "${DEST}/lib/"
    # Debug symbols are ~hundreds of MB and unneeded for linking/running.
    rm -rf "${DEST}/lib/libonnxruntime.${ORT_VERSION}.dylib.dSYM"

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
