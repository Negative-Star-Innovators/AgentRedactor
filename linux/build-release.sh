#!/usr/bin/env bash
# Agent Redactor — Linux release build + Velopack AppImage packaging.
#
# Usage:
#   linux/build-release.sh            # build + pack into linux/build-release/velopack/
#
# Produces the Velopack channel artifacts (AppImage, nupkg, feed JSON) for the
# host architecture: channel 'linux' on x64, 'linux-arm64' on aarch64 (mirrors
# the win / win-arm64 split). Upload to R2 with (same secrets as the Windows
# flow):
#
#   vpk upload s3 --bucket agentredactor-releases \
#     --endpoint https://$R2_ACCOUNT_ID.r2.cloudflarestorage.com \
#     --keyId "$R2_ACCESS_KEY_ID" --secret "$R2_SECRET_ACCESS_KEY" \
#     --prefix <channel> -c <channel> --outputDir linux/build-release/velopack
#
# Prereqs: build deps from linux/README.md, dotnet SDK + `vpk` 1.2.0
# (dotnet tool install -g vpk --version 1.2.0), onnxruntime tarball.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="${ROOT}/build-release"
STAGE="${BUILD}/appdir"
OUT="${BUILD}/velopack"
VERSION="$(tr -d '[:space:]' < "${ROOT}/../windows/version.txt")"
ONNX_INCLUDE="${ONNXRUNTIME_INCLUDE_DIR:-$HOME/onnxruntime/include}"
ONNX_LIB="${ONNXRUNTIME_LIB:-$HOME/onnxruntime/lib/libonnxruntime.so}"

# Arch-aware: mirrors the Windows win / win-arm64 channel split. The x64
# channel keeps the original 'linux' name; ARM64 uses 'linux-arm64'.
ARCH="$(uname -m)"
if [ "${ARCH}" = "aarch64" ]; then
    VP_ARCH="arm64"
    VPK_RID="linux-arm64"
    CHANNEL="linux-arm64"
    LIB_DIR="/usr/lib/aarch64-linux-gnu"
else
    VP_ARCH="x64"
    VPK_RID="linux-x64"
    CHANNEL="linux"
    LIB_DIR="/usr/lib/x86_64-linux-gnu"
fi

VP_QT_PLUGIN_DIR="${QT6_PLUGIN_DIR:-${LIB_DIR}/qt6/plugins}"

echo "==> Fetching Velopack lib"
bash "${ROOT}/fetch-velopack.sh"

echo "==> Building (Release, AR_SELFRELEASE=ON, version ${VERSION})"
cmake -S "${ROOT}" -B "${BUILD}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DAR_SELFRELEASE=ON \
    -DONNXRUNTIME_INCLUDE_DIR="${ONNX_INCLUDE}" \
    -DONNXRUNTIME_LIB="${ONNX_LIB}"
cmake --build "${BUILD}"

echo "==> Staging AppDir at ${STAGE}"
rm -rf "${STAGE}" "${OUT}"
mkdir -p "${STAGE}/plugins"

cp "${BUILD}/gui/agentredactor-gui" "${STAGE}/"
cp "${BUILD}/engine/agentredactor" "${STAGE}/"
# DT_NEEDED records the lib-prefixed name (see gui/CMakeLists.txt).
cp "${ROOT}/third_party/velopack/lib/velopack_libc_linux_${VP_ARCH}_gnu.so" \
    "${STAGE}/libvelopack_libc_linux_${VP_ARCH}_gnu.so"
cp "${ONNX_LIB}" "${STAGE}/"

# Model companions ship in the package next to the exe (same split as the
# Windows self-release channel, build.ps1 /XF *.onnx_data): the 1.6 GB weights
# download on first run from the R2 endpoint, but the tokenizer / config /
# calibration / model graph must be present or EnsureModelFiles cannot start
# (core/src/model_downloader.cpp kCompanionFiles).
MODELS_SRC="${ROOT}/../windows/models"
mkdir -p "${STAGE}/models/onnx"
cp "${MODELS_SRC}/config.json" "${MODELS_SRC}/tokenizer.json" \
    "${MODELS_SRC}/viterbi_calibration.json" "${STAGE}/models/"
cp "${MODELS_SRC}/onnx/model_quantized.onnx" "${STAGE}/models/onnx/"

# Bundle the shared libraries the two binaries resolve to, minus the
# AppImage-standard system set that must come from the host.
EXCLUDE='^(linux-vdso|ld-linux|libc|libm|libdl|librt|libpthread|libresolv|libnsl|libutil|libz|libGL|libEGL|libX11|libxcb|libXau|libXdmcp|libdrm|libgbm|libwayland-|libxkbcommon|libfontconfig|libfreetype|libexpat|libdbus-1|libsystemd|libglib-2.0|libgobject-2.0|libgio-2.0)\.so'
for bin in "${STAGE}/agentredactor-gui" "${STAGE}/agentredactor"; do
    ldd "${bin}" | awk '/=> \// {print $1, $3}' | while read -r name path; do
        if [[ "${name}" =~ ${EXCLUDE} ]]; then continue; fi
        cp -n "${path}" "${STAGE}/${name}" || true
    done
done

# Qt plugins the GUI actually uses; qt.conf points Qt at the bundled copy.
# wayland-decoration-client provides the client-side title bar (min/max/
# close buttons) on compositors without server-side decorations (GNOME) —
# without it Qt runs with "no decorations" and the window has no title bar.
for group in platforms platformthemes wayland-shell-integration wayland-decoration-client xcbglintegrations imageformats iconengines tls; do
    if [ -d "${VP_QT_PLUGIN_DIR}/${group}" ]; then
        cp -r "${VP_QT_PLUGIN_DIR}/${group}" "${STAGE}/plugins/"
    fi
done
# Plugins drag in their own deps (xcb-cursor etc.); resolve one more pass.
find "${STAGE}/plugins" -name '*.so' -print0 | while IFS= read -r -d '' so; do
    ldd "${so}" | awk '/=> \// {print $1, $3}' | while read -r name path; do
        if [[ "${name}" =~ ${EXCLUDE} ]]; then continue; fi
        cp -n "${path}" "${STAGE}/${name}" || true
    done
done

cat > "${STAGE}/qt.conf" <<'EOF'
[Paths]
Plugins = plugins
EOF

# Qt is dynamically linked (LGPL-compliant); ship the required notice.
cat > "${STAGE}/LGPL-Qt-notice.txt" <<'EOF'
This application uses Qt (https://www.qt.io/), licensed under the GNU Lesser
General Public License v3. Qt is dynamically linked; you may replace the
bundled Qt libraries with your own build. Qt source code is available from
https://download.qt.io/official_releases/qt/ and corresponding source for the
exact bundled version on request.
EOF

echo "==> Packing Velopack release"
vpk pack \
    --packId AgentRedactor \
    --packVersion "${VERSION}" \
    --packDir "${STAGE}" \
    --mainExe agentredactor-gui \
    --runtime "${VPK_RID}" \
    --channel "${CHANNEL}" \
    --packTitle "Agent Redactor" \
    --packAuthors "Negative Star Innovators" \
    --icon "${ROOT}/gui/assets/app.png" \
    --categories "Utility;Security" \
    --outputDir "${OUT}"

# Align the portable AppImage filename with the documented download URLs:
# x64 keeps the original AgentRedactor.AppImage; ARM64 uses the arch-qualified
# name because both channels are served from the same R2 bucket under different
# prefixes. The nupkg/feed are unchanged (packId stays AgentRedactor).
if [ "${CHANNEL}" = "linux-arm64" ]; then
    mv "${OUT}/AgentRedactor.AppImage" "${OUT}/AgentRedactor-linux-arm64.AppImage"
fi

echo "==> Done. Artifacts in ${OUT}:"
ls -la "${OUT}"
