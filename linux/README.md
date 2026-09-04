# Agent Redactor — Linux build

Build the engine/CLI (`agentredactor`) and the Qt GUI (`agentredactor-gui`)
on Linux:

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config \
  libsecret-1-dev libcurl4-openssl-dev libssl-dev nlohmann-json3-dev \
  qt6-base-dev qt6-l10n-tools libgl1-mesa-dev patchelf \
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

The AT-SPI UI tests in `linux/test_gui_atspi.py` (marker `atspi`) drive the
real GUI through the accessibility bus, so they need a display (a real X
session, or Xvfb) plus the distro `gi`/AT-SPI bindings
(`python3-gi gir1.2-atspi-2.0 at-spi2-core`; Qt only registers on the bus
under `QT_QPA_PLATFORM=xcb`, never offscreen). They skip cleanly without
those. Headless run:

```bash
sudo apt install xvfb python3-gi gir1.2-atspi-2.0 at-spi2-core dbus-x11
cd tests
PYTHONPATH=/usr/lib/python3/dist-packages \
  dbus-run-session -- xvfb-run -a -s "-screen 0 1280x800x24" \
  python -m pytest linux/test_gui_atspi.py -q
# or exclude them from the full Linux run:
python -m pytest linux -q -m "not atspi"
```

## GUI, tray and autostart

`build/gui/agentredactor-gui` is the desktop app (Qt6 Widgets, translated
into every language Windows supports — see "Translations" below). It spawns
the engine (`agentredactor`, found next to it or in the sibling `engine/`
build dir) when none is running, and stops it on quit only when it spawned
it.

- Tray: `QSystemTrayIcon` (StatusNotifierItem). On desktops without a tray
  (plain Wayland GNOME without the AppIndicator extension) the app runs as a
  control panel: closing the window exits the GUI but leaves the engine
  running. `agentredactor-gui --tray-only` starts hidden (the autostart
  mode); it is ignored when no tray exists.
- Autostart: the "Start on boot" toggle writes/removes
  `$XDG_CONFIG_HOME/autostart/agentredactor.desktop` (the GUI reconciles the
  file with the persisted setting on startup, so CLI changes apply too).
- Language: switchable from the tray Language submenu, the Settings card
  combo, or the CLI (`agentredactor set app-language <tag>`); applies live
  with no restart (Qt retranslation, RTL included).
- Headless/boot-time startup without a desktop session: install the systemd
  user unit from `linux/systemd/agentredactor.service` (instructions in the
  file's header comment).
- Password protection on Linux is a typed master password chosen inside the
  app (not your OS login password). The GUI shows a lock overlay with a
  password field when the session is locked.

## Translations

The GUI supports the same languages as Windows (`SUPPORTED_LANGUAGES` in
`core/include/constants.h` is the shared source of truth). Catalogs live in
`linux/gui/i18n/agentredactor_<locale>.ts` and are compiled into `:/i18n`
with `lrelease` (package `qt6-l10n-tools`; without it the build is
English-only with a CMake warning). Two scripts maintain them:

- `i18n/sync_ts.py` — scans the GUI sources for `tr()` strings and fills
  each catalog from the matching `windows/Strings/<tag>/Resources.resw`
  translations (normalizing `&` accelerators and `{0}` ↔ `%1` placeholders).
  Idempotent; re-run it after changing any GUI string.
- `i18n/bootstrap_translations.py` — machine-translates the Linux-only
  strings (typed-password flow etc.) with Google Translate, like the Windows
  bootstrap (`windows/generate_new_languages.py`); review by native speakers
  is still needed.

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

Artifacts land in `linux/build-release/velopack/`:
- x64: `AgentRedactor.AppImage`
- ARM64: `AgentRedactor-linux-arm64.AppImage`
- `*-linux-full.nupkg` and `releases.<channel>.json` (the update feed)

### Running the AppImage locally

AppImages need a FUSE 2 runtime. On Ubuntu 24.04 and other distros that ship
FUSE 3 only, install `libfuse2` (package name may be `libfuse2t64`):

```bash
sudo apt install libfuse2   # or libfuse2t64 on Ubuntu 24.04
```

Then make the AppImage executable and run it:

```bash
chmod +x linux/build-release/velopack/AgentRedactor.AppImage
./linux/build-release/velopack/AgentRedactor.AppImage
# Start hidden to the system tray:
./linux/build-release/velopack/AgentRedactor.AppImage --tray-only
# Use the bundled CLI:
./linux/build-release/velopack/AgentRedactor.AppImage --cli status
```

If FUSE is unavailable, extract and run as a fallback:

```bash
./AgentRedactor.AppImage --appimage-extract-and-run
```

### Publishing

Upload mirrors the Windows workflow:

```bash
vpk upload s3 --bucket agentredactor-releases \
  --endpoint https://$R2_ACCOUNT_ID.r2.cloudflarestorage.com \
  --keyId "$R2_ACCESS_KEY_ID" --secret "$R2_SECRET_ACCESS_KEY" \
  --prefix linux -c linux --outputDir linux/build-release/velopack
```

### Update behavior

The app checks for updates at startup and offers a "Check for updates" button
in Settings (self-release builds only). When an update is downloaded it shows
"Restart now / later"; choosing restart applies the update via Velopack and
relaunches the AppImage, preserving the original launch arguments such as
`--tray-only`.

The engine binary ships inside the AppImage next to the GUI; on version
mismatch the GUI stops and respawns it. Qt is bundled dynamically linked
inside the AppImage with `LGPL-Qt-notice.txt`.

First run exposes the CLI as `~/.local/bin/agentredactor`: a plain symlink in
installed layouts, and under an AppImage a two-line wrapper that re-runs the
AppImage file with `--cli` (the GUI binary then execs the bundled dual-mode
engine/CLI binary; a symlink cannot reach inside the ephemeral mount). The
wrapper is rewritten on every launch, so moving the AppImage self-heals on the
next run.

Model files follow the Windows self-release split: the small companions
(`config.json`, `tokenizer.json`, `viterbi_calibration.json`,
`onnx/model_quantized.onnx` from `windows/models/`) ship inside the package
next to the binaries, while the ~1.6 GB `onnx/model_quantized.onnx_data`
weights download on first run from the R2 endpoint into
`~/.local/share/agentredactor/models/` (see `core/src/model_downloader.cpp`).

### Tests

Test hooks (self-release builds only, same contract as Windows):
`AGENTREDACTOR_UPDATE_FEED` overrides the feed URL (loopback http only) and
`AGENTREDACTOR_UPDATE_AUTOAPPLY=1` skips the restart prompt.

- `tests/linux/test_update_feed.py` — packs a vNext release from the same
  AppDir and asserts the shipped AppImage swaps itself, mounts, and runs.
- `tests/linux/test_update_from_live.py` — downloads the previous live AppImage
  from R2 and asserts it upgrades to the just-built release. Wired into the
  `build-linux.yml` workflow.
