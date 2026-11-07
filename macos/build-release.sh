#!/usr/bin/env bash
# Agent Redactor — macOS release build + Velopack/DMG packaging.
#
# Usage:
#   macos/build-release.sh            # build + pack into macos/build-release/velopack
#   macos/build-release.sh --no-sign  # force an unsigned pack even if signing env vars are set
#
# Produces the Velopack channel artifacts for the osx-arm64 channel in
# macos/build-release/velopack/:
#   AgentRedactor-osx-arm64.dmg         (drag-to-Applications installer)
#   *-osx-arm64-full.nupkg              (update package)
#   releases.osx-arm64.json             (update feed)
# (vpk also emits AgentRedactor-Portable.zip / RELEASES / assets.osx-arm64.json
# bookkeeping; the CI publish job prunes the non-client files from R2, same as
# the Windows and Linux flows.) Upload with (same bucket/secrets as the other
# platforms):
#
#   vpk upload s3 --bucket agentredactor-releases \
#     --endpoint https://$R2_ACCOUNT_ID.r2.cloudflarestorage.com \
#     --keyId "$R2_ACCESS_KEY_ID" --secret "$R2_SECRET_ACCESS_KEY" \
#     -c osx-arm64 --prefix osx-arm64 --outputDir macos/build-release/velopack
#
# Signing is optional and driven by env vars (CI: imported into a temporary
# keychain by release-macos.yml; locally: present in your login keychain):
#   SIGN_APP_IDENTITY       "Developer ID Application: ..."  -> app + DMG signing
#   SIGN_ENTITLEMENTS       path to an entitlements plist    -> app signing
#   NOTARY_PROFILE          notarytool profile name          -> DMG notarization
# Without them (or with --no-sign) the pack is unsigned — fine for local runs
# and PR CI; Gatekeeper will not open the DMG on other machines.
#
# Prereqs: brew deps from macos/README.md (cmake ninja nlohmann-json openssl@3
# xcodegen), dotnet SDK + `vpk` 1.2.0 (dotnet tool install -g vpk --version
# 1.2.0 — keep it in sync with macos/fetch-velopack.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="${ROOT}/build-release"
STAGE_ROOT="${BUILD}/app"
OUT="${BUILD}/velopack"
VERSION="$(tr -d '[:space:]' < "${ROOT}/../windows/version.txt")"

NO_SIGN=0
if [ "${1:-}" = "--no-sign" ]; then
    NO_SIGN=1
fi

# vpk is typically installed via `dotnet tool install -g`.
export PATH="${PATH}:${HOME}/.dotnet/tools"

# Arch-aware, mirroring the win / win-arm64 and linux / linux-arm64 splits:
# Apple silicon builds use 'osx-arm64'; the bare 'osx' channel name is
# reserved for a future x64/universal build. This script only packs arm64.
ARCH="$(uname -m)"
if [ "${ARCH}" != "arm64" ]; then
    echo "ERROR: this script packs the osx-arm64 channel and must run on an" >&2
    echo "       Apple silicon Mac (got '${ARCH}')." >&2
    exit 1
fi
CHANNEL="osx-arm64"
VPK_RID="osx-arm64"

echo "==> Fetching dependencies"
bash "${ROOT}/fetch-deps.sh"
bash "${ROOT}/fetch-velopack.sh"

# Release build into the canonical macos/build tree — build-app.sh expects the
# engine at macos/build/agentredactor (or macos/build/engine/agentredactor).
echo "==> Building engine (Release, AR_SELFRELEASE=ON, version ${VERSION})"
cmake -S "${ROOT}" -B "${ROOT}/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DAR_SELFRELEASE=ON
cmake --build "${ROOT}/build"

echo "==> Building + assembling AgentRedactor.app (build-app.sh --selfrelease:"
echo "    engine is AR_SELFRELEASE=ON, GUI compiles the updater in + bundles"
echo "    velopack_libc_osx.dylib)"
bash "${ROOT}/build-app.sh" --selfrelease

APP_BUNDLE_SRC="${ROOT}/build/AgentRedactor.app"
[ -d "${APP_BUNDLE_SRC}" ] || { echo "ERROR: ${APP_BUNDLE_SRC} missing" >&2; exit 1; }

echo "==> Staging app at ${STAGE_ROOT}"
rm -rf "${STAGE_ROOT}" "${OUT}"
mkdir -p "${STAGE_ROOT}"
cp -R "${APP_BUNDLE_SRC}" "${STAGE_ROOT}/AgentRedactor.app"
RES="${STAGE_ROOT}/AgentRedactor.app/Contents/Resources"
ENGINE="${RES}/agentredactor"

# Model companions ship inside the bundle next to the engine binary
# (core/src/model_downloader.cpp resolves <exeDir>/models), same split as the
# Windows/Linux self-release channels: the 1.6 GB weights download on first
# run from the R2 endpoint, but the tokenizer / config / calibration / model
# graph must be present or EnsureModelFiles cannot start.
MODELS_SRC="${ROOT}/../windows/models"
mkdir -p "${RES}/models/onnx"
cp "${MODELS_SRC}/config.json" "${MODELS_SRC}/tokenizer.json" \
    "${MODELS_SRC}/viterbi_calibration.json" "${RES}/models/"
cp "${MODELS_SRC}/onnx/model_quantized.onnx" "${RES}/models/onnx/"

# Bundle the non-system shared libraries the engine resolves to, and rewrite
# the engine's references to @loader_path — the macOS equivalent of the Linux
# AppDir ldd pass (the engine's INSTALL_RPATH is @loader_path, so bundled
# copies next to the binary win). libcurl stays the macOS SDK copy.
echo "==> Bundling dylibs, rewriting install names"
# onnxruntime: the engine records @rpath/libonnxruntime.1.dylib (symlink in
# the tarball); ship the resolved library under that name.
cp -L "${ROOT}/third_party/onnxruntime/lib/libonnxruntime.1.dylib" "${RES}/libonnxruntime.1.dylib"
chmod 755 "${RES}/libonnxruntime.1.dylib"
install_name_tool -id "@loader_path/libonnxruntime.1.dylib" "${RES}/libonnxruntime.1.dylib" 2>/dev/null || true

bundle_non_system_deps() {
    local target="$1" dep base
    otool -L "$target" | awk '/^\t\/[^ ]+/ {print $1}' | while IFS= read -r dep; do
        case "$dep" in
            /System/*|/usr/lib/*) continue ;;
        esac
        [ -f "$dep" ] || continue
        base="$(basename "$dep")"
        if [ ! -f "${RES}/${base}" ]; then
            cp "$dep" "${RES}/${base}"
            chmod 755 "${RES}/${base}"
            install_name_tool -id "@loader_path/${base}" "${RES}/${base}" 2>/dev/null || true
        fi
        install_name_tool -change "$dep" "@loader_path/${base}" "$target"
    done
}
# Homebrew OpenSSL (libssl/libcrypto) and any other absolute non-system refs.
bundle_non_system_deps "${ENGINE}"
# Copied libraries can reference each other (libssl -> libcrypto).
for dylib in "${RES}"/*.dylib; do
    [ -e "$dylib" ] || continue
    [ "$dylib" = "${RES}/libonnxruntime.1.dylib" ] && continue
    bundle_non_system_deps "$dylib"
done

# install_name_tool invalidates signatures; refresh the seal on everything we
# touched. Sign innermost-first, then sign the outer staged app for the DMG.
# vpk separately signs the copy it places in update packages. Local builds
# prefer the stable dev identity (tools/dev-signing-setup.sh): the same signer
# across rebuilds keeps the Keychain stable so protection is fully testable.
if [ "${NO_SIGN}" -eq 0 ] && [ -n "${SIGN_APP_IDENTITY:-}" ]; then
    echo "==> Signing app with '${SIGN_APP_IDENTITY}'"
    for dylib in "${RES}"/*.dylib \
        "${STAGE_ROOT}/AgentRedactor.app/Contents/Frameworks/velopack_libc_osx.dylib"; do
        [ -e "${dylib}" ] || continue
        codesign --force --sign "${SIGN_APP_IDENTITY}" --timestamp \
            --options runtime "${dylib}"
    done
    codesign --force --sign "${SIGN_APP_IDENTITY}" --timestamp \
        --options runtime "${ENGINE}"
    if [ -n "${SIGN_ENTITLEMENTS:-}" ]; then
        codesign --force --sign "${SIGN_APP_IDENTITY}" --timestamp \
            --options runtime --entitlements "${SIGN_ENTITLEMENTS}" \
            "${STAGE_ROOT}/AgentRedactor.app"
    else
        codesign --force --sign "${SIGN_APP_IDENTITY}" --timestamp \
            --options runtime "${STAGE_ROOT}/AgentRedactor.app"
    fi
else
    DEV_IDENTITY="${DEV_SIGN_IDENTITY:-Agent Redactor Dev}"
    if [ "${DEV_IDENTITY}" != "ad-hoc" ] && security find-certificate -c "${DEV_IDENTITY}" >/dev/null 2>&1; then
        echo "    local seal: ${DEV_IDENTITY}"
        codesign --force --sign "${DEV_IDENTITY}" "${RES}"/*.dylib 2>/dev/null || true
        codesign --force --sign "${DEV_IDENTITY}" "${ENGINE}" 2>/dev/null || true
        codesign --force --sign "${DEV_IDENTITY}" "${STAGE_ROOT}/AgentRedactor.app" 2>/dev/null || true
    else
        codesign --force --sign - "${RES}"/*.dylib 2>/dev/null || true
        codesign --force --sign - "${ENGINE}" 2>/dev/null || true
        codesign --force --sign - "${STAGE_ROOT}/AgentRedactor.app" 2>/dev/null || true
    fi
fi
codesign --verify --deep --strict --verbose=2 "${STAGE_ROOT}/AgentRedactor.app"

echo "==> Packing Velopack updates (channel ${CHANNEL}, rid ${VPK_RID})"
sign_args=()
if [ "${NO_SIGN}" -eq 0 ] && [ -n "${SIGN_APP_IDENTITY:-}" ]; then
    sign_args+=(--signAppIdentity "${SIGN_APP_IDENTITY}")
    if [ -n "${SIGN_ENTITLEMENTS:-}" ]; then
        sign_args+=(--signEntitlements "${SIGN_ENTITLEMENTS}")
    fi
    echo "    signing updates: ${SIGN_APP_IDENTITY}"
else
    echo "    unsigned (no SIGN_APP_IDENTITY, or --no-sign)"
fi

# --exclude is additive to vpk's built-in default (.*\.pdb). The model weights
# (model_quantized.onnx_data) are downloaded on first run, not shipped. The
# initial download is a DMG, so vpk only needs to produce update artifacts.
# macOS ships bash 3.2, where "${sign_args[@]}" on an empty array trips `set
# -u` — branch instead of expanding the (possibly empty) array inline.
if [ "${#sign_args[@]}" -gt 0 ]; then
    vpk pack \
        --packId com.negativestarinnovators.agentredactor \
        --packVersion "${VERSION}" \
        --packDir "${STAGE_ROOT}/AgentRedactor.app" \
        --mainExe AgentRedactor \
        --runtime "${VPK_RID}" \
        --channel "${CHANNEL}" \
        --packTitle "Agent Redactor" \
        --packAuthors "Negative Star Innovators" \
        --icon "${ROOT}/gui/AgentRedactor/installer/icon.icns" \
        --exclude '.*\.onnx_data' \
        --noInst \
        "${sign_args[@]}" \
        --outputDir "${OUT}"
else
    vpk pack \
        --packId com.negativestarinnovators.agentredactor \
        --packVersion "${VERSION}" \
        --packDir "${STAGE_ROOT}/AgentRedactor.app" \
        --mainExe AgentRedactor \
        --runtime "${VPK_RID}" \
        --channel "${CHANNEL}" \
        --packTitle "Agent Redactor" \
        --packAuthors "Negative Star Innovators" \
        --icon "${ROOT}/gui/AgentRedactor/installer/icon.icns" \
        --exclude '.*\.onnx_data' \
        --noInst \
        --outputDir "${OUT}"
fi

echo "==> Creating drag-and-drop DMG"
DMG="${OUT}/AgentRedactor-${CHANNEL}.dmg"
DMG_ROOT="${BUILD}/dmg-root"
PORTABLE_ZIP="${OUT}/com.negativestarinnovators.agentredactor-${CHANNEL}-Portable.zip"
rm -rf "${DMG_ROOT}"
mkdir -p "${DMG_ROOT}"
# Use the packed app, not the pre-pack staging copy: Velopack adds UpdateMac
# and sq.version during packing, and both are required for self-updating.
ditto -x -k "${PORTABLE_ZIP}" "${DMG_ROOT}"
rm -rf "${DMG_ROOT}/__MACOSX"
DMG_APP="${DMG_ROOT}/Agent Redactor.app"
UPDATE_MAC="${DMG_APP}/Contents/MacOS/UpdateMac"
test -x "${UPDATE_MAC}" \
    || { echo "ERROR: packed app is missing UpdateMac" >&2; exit 1; }
# vpk's unsigned path leaves the seal stale after adding UpdateMac/sq.version;
# reseal the DMG copy. Also make the production signing contract explicit.
if [ "${NO_SIGN}" -eq 0 ] && [ -n "${SIGN_APP_IDENTITY:-}" ]; then
    codesign --force --sign "${SIGN_APP_IDENTITY}" --timestamp \
        --options runtime "${UPDATE_MAC}"
    if [ -n "${SIGN_ENTITLEMENTS:-}" ]; then
        codesign --force --sign "${SIGN_APP_IDENTITY}" --timestamp \
            --options runtime --entitlements "${SIGN_ENTITLEMENTS}" "${DMG_APP}"
    else
        codesign --force --sign "${SIGN_APP_IDENTITY}" --timestamp \
            --options runtime "${DMG_APP}"
    fi
else
    DEV_IDENTITY="${DEV_SIGN_IDENTITY:-Agent Redactor Dev}"
    if [ "${DEV_IDENTITY}" = "ad-hoc" ] || ! security find-certificate -c "${DEV_IDENTITY}" >/dev/null 2>&1; then
        DEV_IDENTITY="-"
    fi
    codesign --force --sign "${DEV_IDENTITY}" "${UPDATE_MAC}"
    codesign --force --sign "${DEV_IDENTITY}" "${DMG_APP}"
fi
codesign --verify --deep --strict --verbose=2 "${DMG_APP}"
ln -s /Applications "${DMG_ROOT}/Applications"

# Configure a conventional drag-and-drop Finder window. A read/write image is
# required while saving .DS_Store; the shipped image is compressed/read-only.
RW_DMG="${BUILD}/AgentRedactor-${CHANNEL}-rw.dmg"
DMG_MOUNT="/Volumes/Agent Redactor"
rm -f "${RW_DMG}"
if [ -e "${DMG_MOUNT}" ]; then
    # hdiutil can leave an empty mountpoint directory after detaching. Remove
    # only an empty directory; a mounted/non-empty volume still fails closed.
    rmdir "${DMG_MOUNT}" 2>/dev/null || {
        echo "ERROR: '${DMG_MOUNT}' already exists; eject the old DMG first" >&2
        exit 1
    }
fi
hdiutil create -volname "Agent Redactor" -srcfolder "${DMG_ROOT}" \
    -ov -format UDRW "${RW_DMG}" >/dev/null
# Finder reliably persists .DS_Store for a real /Volumes mount, not a custom
# build-directory mountpoint (where it may only cache the settings locally).
hdiutil attach "${RW_DMG}" -nobrowse -mountpoint "${DMG_MOUNT}" >/dev/null
DMG_MOUNTED=1
trap 'if [ "${DMG_MOUNTED:-0}" -eq 1 ]; then hdiutil detach "${DMG_MOUNT}" -force >/dev/null 2>&1 || true; fi' EXIT
osascript <<'APPLESCRIPT'
tell application "Finder"
    tell disk "Agent Redactor"
        open
        delay 1
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to true
        set bounds of container window to {100, 100, 700, 500}
        set viewOptions to icon view options of container window
        set icon size of viewOptions to 96
        set text size of viewOptions to 12
        set label position of viewOptions to bottom
        set shows item info of viewOptions to false
        set shows icon preview of viewOptions to false
        set arrangement of viewOptions to not arranged
        set position of item "Agent Redactor.app" of container window to {155, 165}
        set position of item "Applications" of container window to {445, 165}
        update without registering applications
        delay 1
        close container window
    end tell
end tell
APPLESCRIPT
test -f "${DMG_MOUNT}/.DS_Store" \
    || { echo "ERROR: Finder did not save the DMG layout" >&2; exit 1; }
rm -rf "${DMG_MOUNT}/.fseventsd"
hdiutil detach "${DMG_MOUNT}" >/dev/null
DMG_MOUNTED=0
trap - EXIT
rmdir "${DMG_MOUNT}" 2>/dev/null || true
hdiutil convert "${RW_DMG}" -format UDZO -imagekey zlib-level=9 \
    -ov -o "${DMG}" >/dev/null
rm -f "${RW_DMG}"

if [ "${NO_SIGN}" -eq 0 ] && [ -n "${SIGN_APP_IDENTITY:-}" ]; then
    echo "==> Signing DMG"
    codesign --force --sign "${SIGN_APP_IDENTITY}" --timestamp "${DMG}"
    if [ -n "${NOTARY_PROFILE:-}" ]; then
        echo "==> Notarizing DMG"
        xcrun notarytool submit "${DMG}" \
            --keychain-profile "${NOTARY_PROFILE}" --wait
        xcrun stapler staple "${DMG}"
    else
        echo "    WARNING: DMG is signed but NOT notarized (NOTARY_PROFILE unset)"
    fi
fi

# Cheap sanity check before CI spends time on install tests.
test -f "${OUT}/releases.${CHANNEL}.json" || { echo "ERROR: missing releases.${CHANNEL}.json" >&2; exit 1; }
ls "${OUT}"/*-full.nupkg >/dev/null 2>&1 || { echo "ERROR: no full nupkg in ${OUT}" >&2; exit 1; }
test -f "${DMG}" || { echo "ERROR: missing ${DMG}" >&2; exit 1; }

echo "==> Done. Artifacts in ${OUT}:"
ls -la "${OUT}"
