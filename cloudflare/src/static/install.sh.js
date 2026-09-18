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
if start_app; then
    echo 'AgentRedactor is starting — you can also launch it from your desktop menu.'
elif start_app --appimage-extract-and-run; then
    echo 'AgentRedactor is starting via --appimage-extract-and-run (no FUSE detected; startup is slower).'
    echo 'For normal launches install a FUSE runtime, e.g.: sudo apt install fuse3'
else
    echo 'AgentRedactor failed to start. Try running it manually:' >&2
    echo "  $target" >&2
    exit 1
fi
`;
