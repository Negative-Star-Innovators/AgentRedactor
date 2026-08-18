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
