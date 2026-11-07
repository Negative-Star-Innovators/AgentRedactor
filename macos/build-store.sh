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
#   SIGN_APP_IDENTITY  "Apple Distribution: Name (TEAMID)" — the Mac App Store
#                      distribution certificate. Without it (or with
#                      --no-sign) the bundle is ad-hoc signed WITH the store
#                      entitlements — a sandboxed build you can run locally to
#                      test sandbox behavior before the $99 account exists.
#   APPLE_TEAM_ID      10-character Team ID, substituted into the
#                      keychain-access-groups entitlement (required for the
#                      sandboxed build to share Keychain items between the GUI
#                      and the bundled engine binary).
#
# Upload (after the app record exists in App Store Connect):
#   cd macos/build-store && ditto -c -k --sequesterRsrc --keepParent \
#     app/AgentRedactor.app AgentRedactor-store.zip
#   xcrun altool --validate-app -f AgentRedactor-store.zip -t macos \
#     --apiKey <key> --apiIssuer <issuer>
#   xcrun altool --upload-app -f AgentRedactor-store.zip -t macos \
#     --apiKey <key> --apiIssuer <issuer>
# (or drag the .app into Transporter). Release manually in App Store Connect,
# like the Windows MSIX flow at Partner Center.
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

# Entitlements: substitute the Team ID into the keychain access group. Ad-hoc
# local builds have no Team ID — substitute an EMPTY one so the group becomes
# the app identifier, which is the sandbox's default keychain access group
# (a literal "TEAMID." prefix is an invalid entitlement and makes the
# Keychain reject every write, including access-controlled items).
ENTITLEMENTS="${BUILD}/AgentRedactor-Store.entitlements"
mkdir -p "${BUILD}"
TEAM_ID="${APPLE_TEAM_ID:-}"
sed "s/TEAMID\./${TEAM_ID:+$TEAM_ID.}/g; s/TEAMID$/${TEAM_ID}/g" \
    "${ROOT}/packaging/AgentRedactor-Store.entitlements" > "${ENTITLEMENTS}"
if [ -z "${TEAM_ID}" ]; then
    echo "    ad-hoc: keychain access group = app identifier (no Team ID)"
else
    echo "    keychain access group = ${TEAM_ID}.com.negativestarinnovators.agentredactor"
fi

# Sign innermost-first. The App Store rejects disable-library-validation, so
# the bundled third-party dylibs (ONNX Runtime, OpenSSL) are re-signed with
# the SAME distribution identity — with ad-hoc they still load under the
# sandbox locally, but a Distribution-signed build is required for upload.
# Nested code (the engine) is signed without entitlements: it inherits the
# sandbox from the GUI process at spawn time.
if [ "${NO_SIGN}" -eq 0 ] && [ -n "${SIGN_APP_IDENTITY:-}" ]; then
    echo "==> Signing with '${SIGN_APP_IDENTITY}'"
    for dylib in "${RES}"/*.dylib; do
        [ -e "$dylib" ] || continue
        codesign --force --sign "${SIGN_APP_IDENTITY}" --timestamp \
            --options runtime "$dylib" 2>/dev/null \
            || codesign --force --sign "${SIGN_APP_IDENTITY}" "$dylib"
    done
    codesign --force --sign "${SIGN_APP_IDENTITY}" \
        --options runtime "${ENGINE}"
    codesign --force --sign "${SIGN_APP_IDENTITY}" \
        --options runtime --entitlements "${ENTITLEMENTS}" \
        "${STAGE_ROOT}/AgentRedactor.app"
else
    echo "==> Ad-hoc-equivalent signing WITH store entitlements (local sandbox test)"
    echo "    WARNING: this build is NOT uploadable to App Store Connect"
    echo "    (no Apple Distribution identity / hardened runtime). Uploads need:"
    echo "      APPLE_TEAM_ID=<TEAMID> SIGN_APP_IDENTITY='Apple Distribution: ...' $0"
    # The sandbox entitlement is mandatory for this flavor; the local dev
    # identity (when present) just keeps the signer stable across rebuilds.
    # Note: sandboxed Keychain access needs real Apple signing/provisioning;
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
    codesign --force --sign "${SIGNER}" "${ENGINE}" 2>/dev/null || true
    # --entitlements applies with any signer: the app runs sandboxed.
    codesign --force --sign "${SIGNER}" --entitlements "${ENTITLEMENTS}" \
        "${STAGE_ROOT}/AgentRedactor.app"
fi

# Sanity: the sandbox entitlement must be present, and (when signed) the
# signature must verify.
codesign -d --entitlements :- "${STAGE_ROOT}/AgentRedactor.app" 2>/dev/null \
    | grep -q "com.apple.security.app-sandbox" \
    || { echo "ERROR: app-sandbox entitlement missing" >&2; exit 1; }
codesign --verify --strict --verbose=2 "${STAGE_ROOT}/AgentRedactor.app" \
    || { echo "ERROR: signature verification failed" >&2; exit 1; }

echo "==> Done. Sandbox-signed app at ${STAGE_ROOT}/AgentRedactor.app"
ls -la "${STAGE_ROOT}"
