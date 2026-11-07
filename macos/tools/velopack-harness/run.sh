#!/usr/bin/env bash
# Builds and runs the Velopack libc harness (macos/tools/velopack-harness/)
# against a loopback python http.server — verifies the updater binding
# (dlopen, loopback feed gating, feed check, nupkg download) without vpk.
# The harness binary runs from a fake Harness.app carrying the vpk-style
# manifest (Contents/Resources/sq.version) + UpdateMac placeholder that vpk
# pack would normally produce.
set -euo pipefail

HARNESS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MACOS_DIR="$(cd "$HARNESS_DIR/../.." && pwd)"
GUI_SETTINGS="$MACOS_DIR/gui/AgentRedactor/UI/Settings"
DYLIB_SRC="$MACOS_DIR/third_party/velopack/lib/velopack_libc_osx.dylib"
WORK="$(mktemp -d)"
trap 'if [ -f "$WORK/server-pids" ]; then while read -r p; do kill "$p" 2>/dev/null; done < "$WORK/server-pids"; fi; rm -rf "$WORK" "$HOME/Library/Caches/velopack/dev.velopack.harness"' EXIT

[ -f "$DYLIB_SRC" ] || bash "$MACOS_DIR/fetch-velopack.sh"

# --- 1. Compile the harness against the real provider sources ---------------
APP="$WORK/Harness.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
swiftc -D SELFRELEASE -O -o "$APP/Contents/MacOS/velopack-harness" \
    "$HARNESS_DIR/main.swift" \
    "$GUI_SETTINGS/VelopackUpdateProvider.swift" \
    "$GUI_SETTINGS/UpdateProvider.swift"
chmod +x "$APP/Contents/MacOS/velopack-harness"
touch "$APP/Contents/MacOS/UpdateMac"   # locator only checks existence
chmod +x "$APP/Contents/MacOS/UpdateMac"
cat > "$APP/Contents/Resources/sq.version" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<package>
  <metadata>
    <id>dev.velopack.harness</id>
    <version>1.0.0</version>
    <title>VelopackHarness</title>
    <authors>harness</authors>
    <description>harness</description>
    <mainExe>velopack-harness</mainExe>
    <os>macos</os>
    <channel>osx-arm64</channel>
  </metadata>
</package>
EOF
cp "$DYLIB_SRC" "$WORK/velopack_libc_osx.dylib"
BIN="$APP/Contents/MacOS/velopack-harness"

PASS=0
check() { # <description> <condition...>
    local desc="$1"; shift
    if "$@"; then echo "PASS: $desc"; PASS=$((PASS+1));
    else echo "FAIL: $desc"; exit 1; fi
}

# --- 2. Loopback gating of AGENTREDACTOR_UPDATE_FEED -------------------------
out="$(env -u AGENTREDACTOR_UPDATE_FEED "$BIN" "$WORK/velopack_libc_osx.dylib" resolved-url)"
check "unset override -> default feed" grep -q "resolved=https://api.agentredactor.negativestarinnovators.com/updates/osx-arm64" <<<"$out"

out="$(AGENTREDACTOR_UPDATE_FEED="http://127.0.0.1:9999" "$BIN" "$WORK/velopack_libc_osx.dylib" resolved-url)"
check "loopback override honored" grep -q "resolved=http://127.0.0.1:9999" <<<"$out"

out="$(AGENTREDACTOR_UPDATE_FEED="http://evil.example.com/feed" "$BIN" "$WORK/velopack_libc_osx.dylib" resolved-url 2>/dev/null)"
check "non-loopback override refused" grep -q "resolved=https://api.agentredactor.negativestarinnovators.com/updates/osx-arm64" <<<"$out"

out="$(AGENTREDACTOR_UPDATE_FEED="https://127.0.0.1:9999" "$BIN" "$WORK/velopack_libc_osx.dylib" resolved-url 2>/dev/null)"
check "https override refused (loopback http only)" grep -q "resolved=https://api.agentredactor" <<<"$out"

# --- 3. Startup hooks return safely ------------------------------------------
out="$("$BIN" "$WORK/velopack_libc_osx.dylib" app-run 2>&1)"
check "vpkc_app_run returns" grep -q "PASS: app-run returned" <<<"$out"

# --- 4. Same-version feed -> up-to-date ---------------------------------------
FEED_SAME="$WORK/feed-same"
mkdir -p "$FEED_SAME"
cat > "$FEED_SAME/releases.osx-arm64.json" <<'EOF'
{"assets":[{"packageId":"dev.velopack.harness","version":"1.0.0","type":"Full",
"fileName":"harness-1.0.0-full.nupkg","sha1":"0000000000000000000000000000000000000000",
"sha256":"0000000000000000000000000000000000000000000000000000000000000000","size":0,
"notesMarkdown":"","notesHtml":""}]}
EOF

# --- helper: start a persistent http.server ----------------------------------
start_server() { # <dir> -> prints port (server killed on EXIT)
    local dir="$1"
    local log="$WORK/http-$(basename "$dir").log"
    local port pid
    port="$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')"
    python3 -m http.server "$port" --bind 127.0.0.1 --directory "$dir" >"$log" 2>&1 &
    pid=$!
    echo "$pid" >> "$WORK/server-pids"
    for _ in $(seq 1 50); do
        if curl -fs -o /dev/null "http://127.0.0.1:$port/" 2>/dev/null; then
            echo "$port"; return
        fi
        sleep 0.1
    done
    echo "server did not start" >&2; return 1
}
PORT_SAME="$(start_server "$FEED_SAME")"
out="$("$BIN" "$WORK/velopack_libc_osx.dylib" check "http://127.0.0.1:$PORT_SAME" 2>&1)"
echo "$out" | grep -E "Velopack|CHECK" | tail -3
check "same-version feed -> up-to-date" grep -q "CHECK-RESULT: uptodate" <<<"$out"

# --- 5. vNext feed -> detected + downloaded ----------------------------------
FEED_NEXT="$WORK/feed-next"
mkdir -p "$FEED_NEXT"
python3 - "$FEED_NEXT" <<'PYEOF'
import hashlib, json, sys, zipfile, os
feed = sys.argv[1]
pkg = os.path.join(feed, "harness-1.2.3-full.nupkg")
with zipfile.ZipFile(pkg, "w") as z:
    z.writestr("lib/app.txt", "harness fake nupkg payload\n")
data = open(pkg, "rb").read()
entry = {
    "packageId": "dev.velopack.harness",
    "version": "1.2.3",
    "type": "Full",
    "fileName": "harness-1.2.3-full.nupkg",
    "sha1": hashlib.sha1(data).hexdigest(),
    "sha256": hashlib.sha256(data).hexdigest(),
    "size": len(data),
    "notesMarkdown": "harness release notes",
    "notesHtml": "<p>harness release notes</p>",
}
with open(os.path.join(feed, "releases.osx-arm64.json"), "w") as fh:
    json.dump({"assets": [entry]}, fh)
PYEOF
PORT_NEXT="$(start_server "$FEED_NEXT")"
rm -rf "$HOME/Library/Caches/velopack/dev.velopack.harness"
out="$("$BIN" "$WORK/velopack_libc_osx.dylib" check "http://127.0.0.1:$PORT_NEXT" 2>&1)"
echo "$out" | grep -E "Velopack|CHECK" | tail -5
check "vNext feed -> update ready with version" grep -q "CHECK-RESULT: ready 1.2.3 " <<<"$out"
check "release notes surfaced" grep -q "notes=harness release notes" <<<"$out"
PKG_DIR="$("$BIN" "$WORK/velopack_libc_osx.dylib" packages-dir | sed 's/^packages=//')"
check "nupkg downloaded to Velopack package cache" test -f "$PKG_DIR/harness-1.2.3-full.nupkg"
ACTUAL="$(wc -c < "$PKG_DIR/harness-1.2.3-full.nupkg")"
EXPECTED="$(wc -c < "$FEED_NEXT/harness-1.2.3-full.nupkg")"
check "downloaded size matches feed ($ACTUAL bytes)" test "$ACTUAL" = "$EXPECTED"
check "loopback server saw the feed request" grep -q "GET /releases.osx-arm64.json" "$WORK/http-feed-next.log"
check "loopback server saw the nupkg download" grep -q "GET /harness-1.2.3-full.nupkg" "$WORK/http-feed-next.log"

echo "==> harness: $PASS checks passed"
