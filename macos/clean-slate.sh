#!/usr/bin/env bash
# Agent Redactor — clean slate for local testing.
#
# Wipes EVERYTHING the app persists on this Mac across all three flavors
# (dev, Velopack self-release, sandboxed App Store build) so the next launch
# behaves like a fresh install:
#
#   - quits the app and any running engine
#   - dev + self-release config:  ~/Library/Application Support/AgentRedactor
#     (settings, profiles, API keys, logs, machine key, downloaded models)
#   - store build container:      ~/Library/Containers/com.negativestarinnovators.agentredactor
#   - Keychain items:             the legacy machine-key item + the GUI's
#     protection secret (both flavors)
#   - CLI command:                ~/.agentredactor/bin symlink + the PATH block
#     in ~/.zshrc / ~/.zprofile / ~/.bash_profile, and /usr/local/bin/
#     agentredactor when it points into an Agent Redactor.app. The app
#     re-creates the user-space command on next launch, so wipe + relaunch
#     tests the first-run CLI install.
#
# Does NOT touch: the repo, built apps/binaries, the Apple Keychain itself,
# or the login-item registration (System Settings -> Login Items if needed).
#
# Usage: macos/clean-slate.sh        (asks for confirmation)
#        macos/clean-slate.sh --yes  (no prompt; for scripts)
set -euo pipefail

if [ "${1:-}" != "--yes" ]; then
    echo "This deletes all Agent Redactor settings, profiles, API keys, logs,"
    echo "downloaded models, and Keychain items for ALL local flavors."
    read -r -p "Type 'wipe' to continue: " reply
    [ "$reply" = "wipe" ] || { echo "aborted"; exit 1; }
fi

echo "==> Quitting app and engines"
pkill -f "AgentRedactor.app" 2>/dev/null || true
pkill -x agentredactor 2>/dev/null || true
sleep 1

APP_SUPPORT="${HOME}/Library/Application Support/AgentRedactor"
CONTAINER="${HOME}/Library/Containers/com.negativestarinnovators.agentredactor"

echo "==> Removing dev / self-release data: ${APP_SUPPORT}"
rm -rf "${APP_SUPPORT}"

echo "==> Removing App Store container: ${CONTAINER}"
rm -rf "${CONTAINER}"

echo "==> Removing Keychain items (best effort)"
security delete-generic-password -s "com.negativestarinnovators.AgentRedactor" -a "machine-key" >/dev/null 2>&1 \
    && echo "    machine key: deleted" || echo "    machine key: none found"
security delete-generic-password -s "com.negativestarinnovators.agentredactor" -a "master-password" >/dev/null 2>&1 \
    && echo "    protection secret: deleted" || echo "    protection secret: none found"

# The CLI command is install state, not user data, but wiping it lets the
# next launch re-test the app's first-run auto-install.
echo "==> Removing CLI command"
USER_BIN="${HOME}/.agentredactor/bin"
if [ -L "${USER_BIN}/agentredactor" ]; then
    rm -f "${USER_BIN}/agentredactor"
    rmdir "${USER_BIN}" "${HOME}/.agentredactor" 2>/dev/null || true
    echo "    user-space symlink: deleted"
else
    echo "    user-space symlink: none found"
fi
for rc in "${HOME}/.zshrc" "${HOME}/.zprofile" "${HOME}/.bash_profile"; do
    [ -f "$rc" ] || continue
    if grep -q '^# Added by Agent Redactor$' "$rc"; then
        sed -i '' -e '/^# Added by Agent Redactor$/d' \
                  -e '/^export PATH="\$HOME\/.agentredactor\/bin:\$PATH"$/d' "$rc"
        echo "    PATH block: removed from ${rc}"
    fi
done
if [ -L /usr/local/bin/agentredactor ]; then
    target="$(readlink /usr/local/bin/agentredactor)"
    case "$target" in
        *AgentRedactor.app*)
            rm -f /usr/local/bin/agentredactor
            echo "    /usr/local/bin/agentredactor: deleted" ;;
        *)
            echo "    /usr/local/bin/agentredactor: left alone (points at ${target})" ;;
    esac
fi

echo "==> Clean slate complete. Next launch is a fresh install (model will"
echo "    re-download for dev/self-release; the store build has it baked in)."
