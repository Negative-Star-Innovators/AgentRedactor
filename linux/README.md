# Agent Redactor — Linux build

Build the engine/CLI (`agentredactor`) and the Qt GUI (`agentredactor-gui`)
on Linux:

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config \
  libsecret-1-dev libcurl4-openssl-dev libssl-dev nlohmann-json3-dev \
  qt6-base-dev libgl1-mesa-dev \
  python3-pytest python3-pytest-asyncio python3-aiohttp python3-psutil

# onnxruntime is not packaged in apt; use the official linux-x64 tarball
# (developed/tested against 1.29.0):
mkdir -p ~/onnxruntime
curl -sL https://github.com/microsoft/onnxruntime/releases/download/v1.29.0/onnxruntime-linux-x64-1.29.0.tgz \
  | tar xz -C ~/onnxruntime --strip-components=1

cmake -B build -G Ninja \
  -DONNXRUNTIME_INCLUDE_DIR=~/onnxruntime/include \
  -DONNXRUNTIME_LIB=~/onnxruntime/lib/libonnxruntime.so
cmake --build build
```

The engine also needs the NER model files. `config.json`, `tokenizer.json`,
`viterbi_calibration.json` and `onnx/model_quantized.onnx` live in
`windows/models/`; the ~1.6 GB `onnx/model_quantized.onnx_data` weights are
downloaded automatically on first run (or grab them from the models endpoint
used by the Windows CI).

Run the tests from the repo root (one pytest process per suite — the suites
share the `conftest` module name and cannot be collected together):

```bash
cd tests
python -m pytest cli -q
python -m pytest migration/test_settings_migration.py -q
python -m pytest linux -q
```

## GUI, tray and autostart

`build/gui/agentredactor-gui` is the desktop app (Qt6 Widgets, English-only
UI structured for later `.ts` translations). It spawns the engine
(`agentredactor`, found next to it or in the sibling `engine/` build dir)
when none is running, and stops it on quit only when it spawned it.

- Tray: `QSystemTrayIcon` (StatusNotifierItem). On desktops without a tray
  (plain Wayland GNOME without the AppIndicator extension) the app runs as a
  control panel: closing the window exits the GUI but leaves the engine
  running. `agentredactor-gui --tray-only` starts hidden (the autostart
  mode); it is ignored when no tray exists.
- Autostart: the "Start on boot" toggle writes/removes
  `$XDG_CONFIG_HOME/autostart/agentredactor.desktop` (the GUI reconciles the
  file with the persisted setting on startup, so CLI changes apply too).
- Headless/boot-time startup without a desktop session: install the systemd
  user unit from `linux/systemd/agentredactor.service` (instructions in the
  file's header comment).
- Password protection on Linux is a typed master password chosen inside the
  app (not your OS login password). The GUI shows a lock overlay with a
  password field when the session is locked.

## Release packaging and self-update (Velopack)

Self-release builds package the app as a Velopack AppImage with an in-app
updater, mirroring the Windows self-release flow (same R2 bucket, same feed
worker). Channels are arch-aware, mirroring `win` / `win-arm64`: x64 builds
use `linux`, ARM64 builds use `linux-arm64`. `build-release.sh` derives the
architecture from the host (`uname -m`); on aarch64 it packs
`-r linux-arm64 -c linux-arm64` and uses the aarch64 Qt plugin dir. Prereqs:
the .NET SDK and the pinned Velopack CLI
(`dotnet tool install -g vpk --version 1.2.0` — keep it in sync with
`linux/fetch-velopack.sh`). On aarch64, point `ONNXRUNTIME_INCLUDE_DIR` /
`ONNXRUNTIME_LIB` at the linux-aarch64 onnxruntime tarball instead.

```bash
linux/build-release.sh    # Release build (-DAR_SELFRELEASE=ON) + AppDir + vpk pack
```

Artifacts land in `linux/build-release/velopack/`: `AgentRedactor.AppImage`
(fixed-name installer/portable binary), `*-linux-full.nupkg` and
`releases.linux.json` (the update feed). Upload mirrors the Windows workflow:

```bash
vpk upload s3 --bucket agentredactor-releases \
  --endpoint https://$R2_ACCOUNT_ID.r2.cloudflarestorage.com \
  --keyId "$R2_ACCESS_KEY_ID" --secret "$R2_SECRET_ACCESS_KEY" \
  --prefix linux -c linux --outputDir linux/build-release/velopack
```

Updater behavior: check at startup plus a "Check for updates" button in
Settings (self-release builds only); when an update is downloaded the app
offers "Restart now / later", applies via Velopack, and restarts. The engine
binary ships inside the AppImage next to the GUI; on version mismatch the GUI
stops and respawns it. Qt is bundled dynamically linked inside the AppImage
with `LGPL-Qt-notice.txt`. First run symlinks the CLI to
`~/.local/bin/agentredactor`.

Test hooks (self-release builds only, same contract as Windows):
`AGENTREDACTOR_UPDATE_FEED` overrides the feed URL (loopback http only) and
`AGENTREDACTOR_UPDATE_AUTOAPPLY=1` skips the restart prompt.
`tests/linux/test_update_feed.py` runs the full pack-vNext → update → swap
cycle against a local feed; it skips when the pack output or vpk is missing.
