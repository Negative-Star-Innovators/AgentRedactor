#!/usr/bin/env bash
# Agent Redactor — return a Linux machine to a clean slate for testing.
#
# Removes every trace the app leaves behind:
#   - running GUI/engine processes (including AppImage-mounted ones)
#   - stale /tmp/.mount_AgentR* FUSE mounts from killed AppImage runs
#   - config, logs, sessions, lock and control files (~/.config/agentredactor)
#   - downloaded AI model files (~/.local/share/agentredactor, incl. the
#     ~1.6 GB ONNX weights — the next run re-downloads them)
#   - the ~/.local/bin/agentredactor CLI shim
#   - the XDG autostart entry (~/.config/autostart/agentredactor.desktop)
#   - Velopack update staging (/var/tmp/velopack/AgentRedactor)
#
# It does NOT delete any .AppImage file you downloaded — remove that yourself.
#
# Usage:
#   scripts/linux-clean-slate.sh           # asks for confirmation
#   scripts/linux-clean-slate.sh --yes     # no prompt
set -u

XDG_CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}"
XDG_DATA="${XDG_DATA_HOME:-$HOME/.local/share}"

if [ "${1:-}" != "--yes" ]; then
    echo "This removes ALL Agent Redactor state from this machine, including"
    echo "settings, logs and the downloaded AI model (re-downloaded on next run)."
    printf "Continue? [y/N] "
    read -r answer
    case "$answer" in y|Y|yes|YES) ;; *) echo "Aborted."; exit 1;; esac
fi

echo "==> Stopping app processes"
pkill -x agentredactor 2>/dev/null        # engine / CLI (any layout)
pkill -f '/agentredactor-gui' 2>/dev/null # GUI (comm name is truncated, so match the path)
sleep 1

echo "==> Unmounting stale AppImage mounts"
for m in /tmp/.mount_AgentR*; do
    [ -e "$m" ] || continue
    fusermount3 -u "$m" 2>/dev/null || fusermount -u "$m" 2>/dev/null
    rmdir "$m" 2>/dev/null || echo "    still busy (a running app?): $m"
done

remove() {
    if [ -e "$1" ] || [ -L "$1" ]; then
        rm -rf -- "$1" && echo "    removed $1"
    else
        echo "    absent  $1"
    fi
}

echo "==> Removing app state"
remove "$XDG_CONFIG/agentredactor"                  # settings, logs, sessions, control.json
remove "$XDG_DATA/agentredactor"                    # downloaded AI model files
remove "$HOME/.local/bin/agentredactor"             # CLI shim (symlink or wrapper)
remove "$XDG_CONFIG/autostart/agentredactor.desktop" # start-on-boot entry
remove "/var/tmp/velopack/AgentRedactor"            # Velopack update staging
remove "/tmp/velopack_AgentRedactor.log"            # Velopack log

echo "==> Done. Verify with: ls $XDG_CONFIG $XDG_DATA | grep -i redactor (expect nothing)"
