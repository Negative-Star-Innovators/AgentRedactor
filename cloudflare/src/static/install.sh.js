// The bash bootstrapper served at /install.sh.
// Users run it via:
//   curl -fsSL https://api.agentredactor.negativestarinnovators.com/install.sh | bash
// so it must work when piped into bash: no interactive prompts, no $0/$BASH_SOURCE.

export const INSTALL_SH = `# AgentRedactor one-line installer (Linux)
# Run with:
#   curl -fsSL https://api.agentredactor.negativestarinnovators.com/install.sh | bash

set -euo pipefail

base='https://api.agentredactor.negativestarinnovators.com/updates'

# Only Linux is supported — there is no macOS build.
if [ "$(uname -s)" != 'Linux' ]; then
    echo 'install.sh supports Linux only.' >&2
    exit 1
fi

# Pick the update channel by CPU architecture. Unlike the Windows installer
# there is no x64-under-emulation fallback: a missing build is a hard error.
case "$(uname -m)" in
    x86_64|amd64)
        channel='linux'
        appimage='AgentRedactor.AppImage'
        ;;
    aarch64|arm64)
        channel='linux-arm64'
        appimage='AgentRedactor-linux-arm64.AppImage'
        ;;
    *)
        echo "Unsupported architecture: $(uname -m) (AgentRedactor provides x64 and ARM64 Linux builds)." >&2
        exit 1
        ;;
esac

install_dir="$HOME/Applications"
target="$install_dir/$appimage"
tmp="$target.download"

echo "Downloading AgentRedactor ($channel)..."
mkdir -p "$install_dir"
# Remove the partial download if the script dies mid-transfer.
trap 'rm -f "$tmp"' EXIT

if command -v curl >/dev/null 2>&1; then
    curl -fSL --retry 3 -o "$tmp" "$base/$channel/$appimage"
elif command -v wget >/dev/null 2>&1; then
    wget -O "$tmp" "$base/$channel/$appimage"
else
    echo 'Neither curl nor wget is installed; install one and retry.' >&2
    exit 1
fi

chmod +x "$tmp"
# rename over a running AppImage is allowed on Linux (the old inode stays
# alive for the running process), so an already-running instance does not
# block the upgrade.
mv -f "$tmp" "$target"
trap - EXIT

echo "Installed: $target"

# Put the engine/CLI on PATH so headless machines (WSL, SSH, servers) can run
# 'agentredactor status' etc. without extracting the AppImage.
mkdir -p "$HOME/.local/bin"
ln -sf "$target" "$HOME/.local/bin/agentredactor"
case ":$PATH:" in
    *":$HOME/.local/bin:"*) ;;
    *) echo "Note: ~/.local/bin is not on PATH; reopen your shell or add it to use 'agentredactor' directly." ;;
esac

# Launch detached. AppImages mount via FUSE; when the mount is unavailable
# (minimal containers, WSL without fuse) the runtime exits non-zero within a
# second or two, and we relaunch with --appimage-extract-and-run, which
# extracts to /tmp and needs no FUSE at all.
start_app() {
    "$target" "$@" </dev/null >/dev/null 2>&1 &
    pid=$!
    slept=0
    while kill -0 "$pid" 2>/dev/null && [ "$slept" -lt 5 ]; do
        sleep 1
        slept=$((slept + 1))
    done
    if kill -0 "$pid" 2>/dev/null; then
        return 0
    fi
    wait "$pid"
}

echo 'Starting AgentRedactor...'
# Headless machines (WSL without WSLg, SSH, servers) cannot run the Qt GUI;
# the AppImage entrypoint would abort inside Qt platform init. Instead of
# printing instructions, leave a WORKING proxy behind: engine running, model
# downloaded, and - when systemd is available - a user service + linger so
# it comes back after logout/reboot. Opt out with AGENTREDACTOR_NO_SERVICE=1
# (engine is not started either; manual instructions are printed instead).
if [ -z "\${DISPLAY:-}" ] && [ -z "\${WAYLAND_DISPLAY:-}" ]; then
    echo 'No display detected - installing headless.'
    if [ -n "\${AGENTREDACTOR_NO_SERVICE:-}" ]; then
        echo 'AGENTREDACTOR_NO_SERVICE is set; not starting the engine.'
        echo "  Run it manually:  \$HOME/.local/bin/agentredactor --console"
        echo "  First run needs the AI model: agentredactor download-model"
        echo '  CLI overview:     agentredactor help'
        exit 0
    fi

    started=0
    if command -v systemctl >/dev/null 2>&1 && systemctl --user daemon-reload >/dev/null 2>&1; then
        mkdir -p "\$HOME/.config/systemd/user"
        cat > "\$HOME/.config/systemd/user/agentredactor.service" <<'UNIT'
[Unit]
Description=Agent Redactor engine
After=default.target

[Service]
ExecStart=%h/.local/bin/agentredactor --console
Restart=on-failure
RestartSec=2

[Install]
WantedBy=default.target
UNIT
        systemctl --user daemon-reload
        if systemctl --user enable --now agentredactor.service >/dev/null 2>&1; then
            started=1
            echo 'Installed systemd user service: agentredactor.service (starts on boot, survives logout).'
            if loginctl enable-linger "\$USER" >/dev/null 2>&1; then
                echo 'Enabled linger: the engine starts at boot without anyone logging in.'
            else
                echo "NOTE: could not enable linger - the engine stops when your last session closes."
                echo "      Fix with: loginctl enable-linger \$USER"
            fi
        else
            echo 'systemd user service could not be started; starting the engine directly.'
        fi
    else
        echo 'No systemd user session detected; starting the engine directly (will NOT survive logout/reboot).'
    fi

    if [ "\$started" -eq 0 ]; then
        state_dir="\${XDG_STATE_HOME:-\$HOME/.local/state}/agentredactor"
        mkdir -p "\$state_dir"
        nohup "\$HOME/.local/bin/agentredactor" --console >>"\$state_dir/engine-stdout.log" 2>&1 &
        echo "Engine started directly (log: \$state_dir/engine-stdout.log)."
    fi

    echo 'Waiting for the engine...'
    engine_up=0
    for _ in $(seq 1 60); do
        if "\$HOME/.local/bin/agentredactor" status >/dev/null 2>&1; then engine_up=1; break; fi
        sleep 1
    done
    if [ "\$engine_up" -eq 0 ]; then
        echo 'ERROR: the engine did not come up within 60 s. Log:' >&2
        echo "  journalctl --user -u agentredactor   (or ~/.local/state/agentredactor/engine-stdout.log)" >&2
        exit 1
    fi

    echo 'Downloading the AI model (first run only, resumable)...'
    "\$HOME/.local/bin/agentredactor" download-model || \
        echo 'Model download failed - retry anytime with: agentredactor download-model' >&2

    echo ''
    echo 'Agent Redactor is running headless.'
    "\$HOME/.local/bin/agentredactor" status || true
    echo ''
    echo 'Useful commands:'
    echo '  agentredactor help      CLI overview'
    echo '  agentredactor update    check for / install updates'
    if [ "\$started" -eq 1 ]; then
        echo '  systemctl --user status agentredactor     service status'
        echo '  systemctl --user stop agentredactor       stop the engine'
        echo '  systemctl --user disable agentredactor    undo boot start'
    fi
    exit 0
fi
if start_app; then
    echo 'AgentRedactor is starting — you can also launch it from your desktop menu.'
elif start_app --appimage-extract-and-run; then
    echo 'AgentRedactor is starting via --appimage-extract-and-run (no FUSE detected; startup is slower).'
    echo 'For normal launches install a FUSE runtime, e.g.: sudo apt install fuse3'
else
    echo 'AgentRedactor failed to start. Try running it manually:' >&2
    echo "  $target" >&2
    echo 'If the output mentions missing X libraries (libxcb-*), install them (the' >&2
    echo 'exact apt line is printed by the AppImage) or run the install script in a' >&2
    echo 'session without DISPLAY to install headless instead.' >&2
    exit 1
fi
`;
