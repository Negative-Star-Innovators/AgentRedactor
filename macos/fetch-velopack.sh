#!/usr/bin/env bash
# Downloads the pinned Velopack C/C++ prebuilt library into macos/third_party/velopack/.
# The binary blob is gitignored; this script is the source of truth for the version.
#
# Uses the exact same release zip as linux/fetch-velopack.sh (same sha256 pin);
# that zip ships a universal (x86_64 + arm64) macOS dylib —
# lib/velopack_libc_osx.dylib — verified by inspecting the 1.2.0 archive.
set -euo pipefail

VP_VERSION="1.2.0"
VP_SHA256="547262ed7a1ab1ff62f580aa53851ede2f1a451ac61b8974eb7bc01117488835"
URL="https://github.com/velopack/velopack/releases/download/${VP_VERSION}/velopack_libc_${VP_VERSION}.zip"

ROOT="$(cd "$(dirname "$0")" && pwd)"
DEST="${ROOT}/third_party/velopack"

if [ -f "${DEST}/include/Velopack.h" ] \
    && [ -f "${DEST}/lib/velopack_libc_osx.dylib" ]; then
    echo "velopack_libc ${VP_VERSION} already present in ${DEST}"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

echo "Downloading ${URL}"
curl -fsSL -o "${TMP}/velopack_libc.zip" "${URL}"

echo "${VP_SHA256}  ${TMP}/velopack_libc.zip" | shasum -a 256 -c -

rm -rf "${DEST}"
mkdir -p "${DEST}"
# Only the macOS pieces this project links/ships; the zip also contains the
# Windows/Linux libs (fetched by linux/fetch-velopack.sh) and per-arch static
# libs we don't use.
unzip -q "${TMP}/velopack_libc.zip" \
    'include/*' \
    'lib/velopack_libc_osx.dylib' \
    -d "${DEST}"

echo "velopack_libc ${VP_VERSION} extracted to ${DEST}"
