# Agent Redactor — Linux Agent Notes

## Before opening or updating a Linux pull request

Run the local build **and** the Linux test suite before pushing. This surfaces compile and runtime failures in seconds instead of waiting for the full GitHub Actions pipeline.

1. Build:

```bash
cmake -S linux -B linux/build -G Ninja \
  -DONNXRUNTIME_INCLUDE_DIR="${ONNXRUNTIME_INCLUDE_DIR:-$HOME/onnxruntime/include}" \
  -DONNXRUNTIME_LIB="${ONNXRUNTIME_LIB:-$HOME/onnxruntime/lib/libonnxruntime.so}"
cmake --build linux/build
```

2. Test:

```bash
cd tests
python3 -m pytest linux -q
```

The AT-SPI GUI tests require a display and the AT-SPI bindings; they skip cleanly without them.

3. Push only after both steps pass. Do not rely on CI as the first compile or test check.

## Key files and concepts

- `linux/gui/` — Qt6 GUI (`agentredactor-gui`)
- `linux/engine/` — dual-mode engine/CLI binary (`agentredactor`)
- `core/` — OS-agnostic proxy, redaction, settings, and model-download code shared with Windows
- `linux/build-release.sh` — release build that produces the Velopack AppImage
- Desktop integration writes to `$XDG_DATA_HOME/applications`, `$XDG_DATA_HOME/icons`, and `$XDG_CONFIG_HOME/autostart`
- The AppImage is the entire application; deleting it removes the program

## General rules

- Keep Linux-specific code in `linux/`; share logic through `core/` when it is not platform-specific.
- Match the existing code style (no unnecessary comments, clear names).
- Update this file if the build/test commands or project layout change.
