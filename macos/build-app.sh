#!/bin/bash
# Build the AgentRedactor macOS GUI and assemble AgentRedactor.app with the
# engine binary bundled at Contents/Resources/agentredactor.
#
#   macos/build-app.sh                release build into macos/build/
#   macos/build-app.sh --run          open the app after building
#   macos/build-app.sh --selfrelease  self-release build: compiles the
#                                     Velopack updater in (SELFRELEASE) and
#                                     bundles velopack_libc_osx.dylib into
#                                     Contents/Frameworks (build-release.sh
#                                     uses this; dev builds stay updater-free,
#                                     mirroring the MSIX build excluding
#                                     Update.exe)
#
# Prereqs: Xcode + `brew install xcodegen` (for the GUI) and the engine built
# with CMake (see macos/README.md).

set -euo pipefail

MACOS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$MACOS_DIR/.." && pwd)"
GUI_DIR="$MACOS_DIR/gui"
BUILD_DIR="$MACOS_DIR/build"
DERIVED_DIR="$BUILD_DIR/derived"
APP_BUNDLE="$BUILD_DIR/AgentRedactor.app"
RUN_AFTER=0
SELFRELEASE=0
APPSTORE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --run) RUN_AFTER=1 ;;
        --selfrelease) SELFRELEASE=1 ;;
        --appstore) APPSTORE=1 ;;
        *) echo "error: unknown argument: $1" >&2; exit 1 ;;
    esac
    shift
done
if [[ "$SELFRELEASE" == "1" && "$APPSTORE" == "1" ]]; then
    echo "error: --selfrelease and --appstore are mutually exclusive" >&2
    exit 1
fi

# --- 1. Engine binary -------------------------------------------------------
# Canonical path: macos/build/agentredactor. The CMake dev build produces
# macos/build/engine/agentredactor; refresh the canonical copy from there
# whenever the CMake output is newer (or the copy is missing) so a rebuilt
# engine never goes stale inside the app bundle.
ENGINE_SRC="$BUILD_DIR/agentredactor"
if [[ -x "$BUILD_DIR/engine/agentredactor" ]]; then
    if [[ ! -x "$ENGINE_SRC" || "$BUILD_DIR/engine/agentredactor" -nt "$ENGINE_SRC" ]]; then
        echo "==> Refreshing engine from $BUILD_DIR/engine/agentredactor"
        cp -f "$BUILD_DIR/engine/agentredactor" "$ENGINE_SRC"
    fi
fi
if [[ ! -x "$ENGINE_SRC" ]]; then
    cat >&2 <<'EOF'
error: the engine binary is missing.

Build it first (see macos/README.md):

    macos/fetch-deps.sh
    cmake -B macos/build -G Ninja -S macos
    cmake --build macos/build

Then re-run macos/build-app.sh (it picks the binary up from
macos/build/engine/agentredactor automatically).
EOF
    exit 1
fi

# --- 2. Version stamp --------------------------------------------------------
# Single version source of truth (same file the Windows/Linux builds stamp
# into the engine as AR_VERSION_STRING) so the GUI/engine version-mismatch
# restart check stays inert for matching builds. Applied before xcodebuild
# because Info.plist is compiled into the bundle.
APP_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/windows/version.txt" 2>/dev/null || echo 1.0.0)"
if [[ -z "$APP_VERSION" ]]; then APP_VERSION="1.0.0"; fi
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $APP_VERSION" \
    "$GUI_DIR/AgentRedactor/Info.plist" 2>/dev/null \
    || /usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string $APP_VERSION" \
    "$GUI_DIR/AgentRedactor/Info.plist"
echo "==> App version: $APP_VERSION"

# --- 3. xcodegen + xcodebuild ------------------------------------------------
if ! command -v xcodegen >/dev/null 2>&1; then
    echo "error: xcodegen not found. Install with: brew install xcodegen" >&2
    exit 1
fi

# Self-release builds compile the Velopack updater in via the SELFRELEASE
# Swift condition; dev builds leave it out entirely (the settings update
# entry falls back to the feed-check-only stub).
VELOPACK_DYLIB="$MACOS_DIR/third_party/velopack/lib/velopack_libc_osx.dylib"
XCODE_EXTRA_ARGS=()
if [[ "$SELFRELEASE" == "1" ]]; then
    echo "==> Self-release: SELFRELEASE=1, Velopack updater enabled"
    if [[ ! -f "$VELOPACK_DYLIB" ]]; then
        bash "$MACOS_DIR/fetch-velopack.sh"
    fi
    XCODE_EXTRA_ARGS+=("SWIFT_ACTIVE_COMPILATION_CONDITIONS=SELFRELEASE")
elif [[ "$APPSTORE" == "1" ]]; then
    # App Store (sandboxed): no updater (Store updates), no Velopack hooks.
    # (The CLI-on-PATH auto install is also skipped: sandboxed apps cannot
    # create PATH entries.)
    echo "==> App Store: APPSTORE=1, sandboxed build"
    XCODE_EXTRA_ARGS+=("SWIFT_ACTIVE_COMPILATION_CONDITIONS=APPSTORE")
fi

echo "==> Generating Xcode project"
(cd "$GUI_DIR" && xcodegen)

# macOS ships bash 3.2, where "${arr[@]}" on an empty array trips `set -u` —
# branch instead of expanding the (possibly empty) array inline (same
# pattern as macos/build-release.sh).
echo "==> Building AgentRedactor (Release)"
if [[ "${#XCODE_EXTRA_ARGS[@]}" -gt 0 ]]; then
    xcodebuild \
        -project "$GUI_DIR/AgentRedactor.xcodeproj" \
        -scheme AgentRedactor \
        -configuration Release \
        -derivedDataPath "$DERIVED_DIR" \
        -destination 'platform=macOS' \
        build \
        "${XCODE_EXTRA_ARGS[@]}"
else
    xcodebuild \
        -project "$GUI_DIR/AgentRedactor.xcodeproj" \
        -scheme AgentRedactor \
        -configuration Release \
        -derivedDataPath "$DERIVED_DIR" \
        -destination 'platform=macOS' \
        build
fi

BUILT_APP="$DERIVED_DIR/Build/Products/Release/AgentRedactor.app"
[[ -d "$BUILT_APP" ]] || { echo "error: build product missing at $BUILT_APP" >&2; exit 1; }

# --- 4. Assemble the app bundle ----------------------------------------------
echo "==> Assembling $APP_BUNDLE"
rm -rf "$APP_BUNDLE"
cp -R "$BUILT_APP" "$APP_BUNDLE"

cp "$ENGINE_SRC" "$APP_BUNDLE/Contents/Resources/agentredactor"
chmod +x "$APP_BUNDLE/Contents/Resources/agentredactor"

# --- 5. Velopack updater dylib (self-release only) -----------------------------
# The GUI dlopens this at runtime (it is NOT linked at build time, so dev
# builds compile and run without it). vpk pack places UpdateMac + the
# sq.version manifest into the bundle; here we only add the library the
# updater code binds to. Release packaging re-signs everything with the
# Developer ID + entitlements (incl. disable-library-validation, required to
# load this third-party dylib under the hardened runtime).
if [[ "$SELFRELEASE" == "1" ]]; then
    echo "==> Bundling velopack_libc_osx.dylib"
    mkdir -p "$APP_BUNDLE/Contents/Frameworks"
    cp "$VELOPACK_DYLIB" "$APP_BUNDLE/Contents/Frameworks/velopack_libc_osx.dylib"
    chmod 755 "$APP_BUNDLE/Contents/Frameworks/velopack_libc_osx.dylib"
    codesign --force --sign - "$APP_BUNDLE/Contents/Frameworks/velopack_libc_osx.dylib" 2>/dev/null || true
fi

# Sign for local runs. Prefer the stable self-signed identity created by
# tools/dev-signing-setup.sh ("Agent Redactor Dev"): the SAME signer across
# rebuilds keeps Keychain items stable, which makes the Hello-style master
# password (Touch ID / Mac password) testable without the Apple Developer
# account. Falls back to ad-hoc when the identity is absent; override with
# DEV_SIGN_IDENTITY=ad-hoc to force ad-hoc.
DEV_IDENTITY="${DEV_SIGN_IDENTITY:-Agent Redactor Dev}"
if [ "${DEV_IDENTITY}" != "ad-hoc" ] && security find-certificate -c "${DEV_IDENTITY}" >/dev/null 2>&1; then
    echo "==> Signing with local identity: ${DEV_IDENTITY}"
    codesign --force --sign "${DEV_IDENTITY}" "$APP_BUNDLE" 2>/dev/null || true
else
    codesign --force --sign - "$APP_BUNDLE" 2>/dev/null || true
fi

echo "==> Done: $APP_BUNDLE"
echo "    Engine bundled: $APP_BUNDLE/Contents/Resources/agentredactor"

if [[ "$RUN_AFTER" == "1" ]]; then
    open "$APP_BUNDLE"
fi
