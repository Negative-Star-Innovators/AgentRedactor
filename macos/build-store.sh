#!/usr/bin/env bash
# Agent Redactor — macOS Mac App Store build (sandboxed channel).
#
# Usage:
#   macos/build-store.sh            # build + sign when credentials are present
#   macos/build-store.sh --no-sign  # ad-hoc signed sandboxed build (local test)
#
# Produces a sandboxed AgentRedactor.app in macos/build-store/app/ suitable for
# App Store Connect upload. Unlike the self-release channel, the AI model
# weights are BAKED IN (MSIX parity): App Store updates are delta-based, so
# the 1.6 GB onnx_data is downloaded once and later updates stay small, and
# reviewers never hit a first-run download.
#
# Flavor differences (SWIFT_ACTIVE_COMPILATION_CONDITIONS=APPSTORE):
#   - no Velopack updater / update feed (the Store updates the app)
#   - no CLI-on-PATH auto install (sandboxed apps cannot create PATH entries)
#   - everything else is identical to the self-release build
#
# Signing is driven by env vars:
#   SIGN_APP_IDENTITY         "Apple Distribution: Name (TEAMID)" — signs the
#                             app and nested code for the Mac App Store.
#   SIGN_INSTALLER_IDENTITY   "Mac Installer Distribution: Name (TEAMID)" —
#                             signs the Transporter-ready installer package.
#   APPLE_TEAM_ID             10-character Team ID, substituted into the
#                             application/team identifiers and Keychain group.
#   STORE_PROVISIONING_PROFILE
#                             Path to the Mac App Store distribution profile for
#                             this App ID, embedded as embedded.provisionprofile.
# Without SIGN_APP_IDENTITY (or with --no-sign), the app is locally signed with
# sandbox entitlements and the PKG is unsigned; neither is uploadable. Restricted
# application/Keychain entitlements are omitted without a provisioning profile.
#
# Upload macos/build-store/AgentRedactor-store-<version>.pkg through Transporter
# after the app record exists in App Store Connect. Release manually in App Store
# Connect, like the Windows MSIX flow at Partner Center.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="${ROOT}/build-store"
STAGE_ROOT="${BUILD}/app"
VERSION="$(tr -d '[:space:]' < "${ROOT}/../windows/version.txt")"

NO_SIGN=0
if [ "${1:-}" = "--no-sign" ]; then
    NO_SIGN=1
fi

ARCH="$(uname -m)"
if [ "${ARCH}" != "arm64" ]; then
    echo "ERROR: the App Store build is arm64-only and must run on an" >&2
    echo "       Apple silicon Mac (got '${ARCH}')." >&2
    exit 1
fi

echo "==> Fetching dependencies"
bash "${ROOT}/fetch-deps.sh"

echo "==> Building engine (Release, version ${VERSION})"
cmake -S "${ROOT}" -B "${ROOT}/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DAR_SELFRELEASE=OFF
cmake --build "${ROOT}/build"

echo "==> Building + assembling AgentRedactor.app (build-app.sh --appstore:"
echo "    APPSTORE flavor — no updater, no CLI-install card, sandboxed)"
bash "${ROOT}/build-app.sh" --appstore

APP_BUNDLE_SRC="${ROOT}/build/AgentRedactor.app"
[ -d "${APP_BUNDLE_SRC}" ] || { echo "ERROR: ${APP_BUNDLE_SRC} missing" >&2; exit 1; }

echo "==> Staging app at ${STAGE_ROOT}"
rm -rf "${STAGE_ROOT}"
mkdir -p "${STAGE_ROOT}"
cp -R "${APP_BUNDLE_SRC}" "${STAGE_ROOT}/AgentRedactor.app"
RES="${STAGE_ROOT}/AgentRedactor.app/Contents/Resources"
ENGINE="${RES}/agentredactor"

# Model BAKED IN for the Store channel (MSIX parity): the full 1.6 GB weights
# ship in the bundle; the engine resolves <exeDir>/models first, so no
# first-run download happens. The weights are not committed to the repo
# (same as the MSIX CI): fetch them into the build dir when absent.
MODELS_SRC="${ROOT}/../windows/models"
WEIGHTS_SRC="${MODELS_SRC}/onnx/model_quantized.onnx_data"
if [ ! -f "${WEIGHTS_SRC}" ]; then
    WEIGHTS_SRC="${BUILD}/models-cache/model_quantized.onnx_data"
    if [ ! -f "${WEIGHTS_SRC}" ]; then
        echo "==> Fetching model weights (1.6 GB, cached in ${BUILD}/models-cache)"
        mkdir -p "${BUILD}/models-cache"
        curl -fSL --retry 3 --retry-delay 2 -o "${WEIGHTS_SRC}.part" \
            "https://api.agentredactor.negativestarinnovators.com/models/model_quantized.onnx_data"
        EXPECTED=1618042064
        ACTUAL=$(stat -f%z "${WEIGHTS_SRC}.part")
        [ "${ACTUAL}" = "${EXPECTED}" ] || { echo "ERROR: weights size ${ACTUAL} != ${EXPECTED}" >&2; exit 1; }
        mv "${WEIGHTS_SRC}.part" "${WEIGHTS_SRC}"
    fi
fi
mkdir -p "${RES}/models/onnx"
cp "${MODELS_SRC}/config.json" "${MODELS_SRC}/tokenizer.json" \
    "${MODELS_SRC}/viterbi_calibration.json" "${RES}/models/"
cp "${MODELS_SRC}/onnx/model_quantized.onnx" "${RES}/models/onnx/"
cp "${WEIGHTS_SRC}" "${RES}/models/onnx/model_quantized.onnx_data"

# Bundle the non-system shared libraries (same @loader_path rewrite as the
# self-release build). libcurl stays the macOS SDK copy.
echo "==> Bundling dylibs, rewriting install names"
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
bundle_non_system_deps "${ENGINE}"
for dylib in "${RES}"/*.dylib; do
    [ -e "$dylib" ] || continue
    [ "$dylib" = "${RES}/libonnxruntime.1.dylib" ] && continue
    bundle_non_system_deps "$dylib"
done

# Substitute the Team ID into the profile-backed application and Keychain
# groups. Unsigned local builds omit restricted entitlement claims because an
# ad-hoc signature has no provisioning profile to authorize them.
ENTITLEMENTS="${BUILD}/AgentRedactor-Store.entitlements"
ENGINE_ENTITLEMENTS="${ROOT}/packaging/AgentRedactor-Engine-Store.entitlements"
mkdir -p "${BUILD}"
TEAM_ID=""
if [ "${NO_SIGN}" -eq 0 ] && [ -n "${SIGN_APP_IDENTITY:-}" ]; then
    TEAM_ID="${APPLE_TEAM_ID:-}"
    if [ -z "${TEAM_ID}" ]; then
        echo "ERROR: APPLE_TEAM_ID is required for signed Store builds" >&2
        exit 1
    fi
fi
sed "s/TEAMID/${TEAM_ID}/g" \
    "${ROOT}/packaging/AgentRedactor-Store.entitlements" > "${ENTITLEMENTS}"

# Sign innermost-first. The App Store rejects disable-library-validation, so
# the bundled third-party dylibs (ONNX Runtime, OpenSSL) are re-signed with
# the SAME distribution identity. The engine declares its own sandbox and
# inherits the GUI's network capabilities when launched as a child process.
if [ "${NO_SIGN}" -eq 0 ] && [ -n "${SIGN_APP_IDENTITY:-}" ]; then
    PROFILE="${STORE_PROVISIONING_PROFILE:-}"
    if [ -z "${PROFILE}" ] || [ ! -f "${PROFILE}" ]; then
        echo "ERROR: STORE_PROVISIONING_PROFILE must name a Mac App Store" >&2
        echo "       distribution .provisionprofile for signed Store builds" >&2
        exit 1
    fi
    cp "${PROFILE}" "${STAGE_ROOT}/AgentRedactor.app/Contents/embedded.provisionprofile"
    echo "==> Signing with '${SIGN_APP_IDENTITY}' and embedded Store profile"
    for dylib in "${RES}"/*.dylib; do
        [ -e "$dylib" ] || continue
        codesign --force --sign "${SIGN_APP_IDENTITY}" --timestamp \
            --options runtime "$dylib" 2>/dev/null \
            || codesign --force --sign "${SIGN_APP_IDENTITY}" "$dylib"
    done
    codesign --force --sign "${SIGN_APP_IDENTITY}" \
        --options runtime --entitlements "${ENGINE_ENTITLEMENTS}" "${ENGINE}"
    codesign --force --sign "${SIGN_APP_IDENTITY}" \
        --options runtime --entitlements "${ENTITLEMENTS}" \
        "${STAGE_ROOT}/AgentRedactor.app"
else
    /usr/libexec/PlistBuddy -c "Delete :com.apple.application-identifier" "${ENTITLEMENTS}"
    /usr/libexec/PlistBuddy -c "Delete :com.apple.developer.team-identifier" "${ENTITLEMENTS}"
    /usr/libexec/PlistBuddy -c "Delete :keychain-access-groups" "${ENTITLEMENTS}"
    echo "==> Ad-hoc-equivalent signing WITH sandbox entitlements (local test)"
    echo "    WARNING: this build is NOT uploadable to App Store Connect"
    echo "    (no Apple Distribution identity / provisioning profile)."
    # The sandbox entitlement is mandatory for this flavor; the local dev
    # identity (when present) just keeps the signer stable across rebuilds.
    # Sandboxed Keychain sharing needs real Apple signing/provisioning;
    # unsigned local builds cannot enable the native protection flow.
    DEV_IDENTITY="${DEV_SIGN_IDENTITY:-Agent Redactor Dev}"
    if [ "${DEV_IDENTITY}" != "ad-hoc" ] && security find-certificate -c "${DEV_IDENTITY}" >/dev/null 2>&1; then
        SIGNER="${DEV_IDENTITY}"
    else
        SIGNER="-"
    fi
    for dylib in "${RES}"/*.dylib; do
        [ -e "$dylib" ] || continue
        codesign --force --sign "${SIGNER}" "$dylib" 2>/dev/null || true
    done
    codesign --force --sign "${SIGNER}" \
        --entitlements "${ENGINE_ENTITLEMENTS}" "${ENGINE}"
    codesign --force --sign "${SIGNER}" --entitlements "${ENTITLEMENTS}" \
        "${STAGE_ROOT}/AgentRedactor.app"
fi

# Sanity: both executables must declare the sandbox, and the signature must
# verify after the profile and all nested code have been sealed.
codesign -d --entitlements :- "${STAGE_ROOT}/AgentRedactor.app" 2>/dev/null \
    | grep -q "com.apple.security.app-sandbox" \
    || { echo "ERROR: app-sandbox entitlement missing from GUI" >&2; exit 1; }
codesign -d --entitlements :- "${ENGINE}" 2>/dev/null \
    | grep -q "com.apple.security.app-sandbox" \
    || { echo "ERROR: app-sandbox entitlement missing from engine" >&2; exit 1; }
codesign --verify --strict --verbose=2 "${STAGE_ROOT}/AgentRedactor.app" \
    || { echo "ERROR: signature verification failed" >&2; exit 1; }

PKG="${BUILD}/AgentRedactor-store-${VERSION}.pkg"
rm -f "${PKG}"
if [ "${NO_SIGN}" -eq 0 ] && [ -n "${SIGN_APP_IDENTITY:-}" ]; then
    if [ -z "${SIGN_INSTALLER_IDENTITY:-}" ]; then
        echo "ERROR: SIGN_INSTALLER_IDENTITY is required for the App Store PKG" >&2
        exit 1
    fi
    echo "==> Creating signed App Store package with '${SIGN_INSTALLER_IDENTITY}'"
    productbuild --component "${STAGE_ROOT}/AgentRedactor.app" /Applications \
        --sign "${SIGN_INSTALLER_IDENTITY}" "${PKG}"
else
    echo "==> Creating unsigned App Store package (local validation only)"
    productbuild --component "${STAGE_ROOT}/AgentRedactor.app" /Applications "${PKG}"
fi

echo "==> Done. Sandbox-signed app at ${STAGE_ROOT}/AgentRedactor.app"
echo "    Installer package: ${PKG}"
ls -la "${STAGE_ROOT}"
