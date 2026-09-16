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

   Note: CI builds on Ubuntu 24.04 with the distro Qt 6.4 (apt
   `qt6-base-dev`), which is also what the AppImage ships. A newer local Qt
   silently accepts post-6.4 APIs — guard anything newer (e.g.
   `QStyleHints::colorScheme`, Qt 6.8) with `#if QT_VERSION >=
   QT_VERSION_CHECK(...)` and a 6.4-compatible fallback, or the PR fails on
   the Linux build leg.

4. If your change touches the Linux codebase at all (GUI widgets/strings, engine,
   libraries Qt the release bundles), also check whether `linux/build-release.sh`
   (the Velopack AppImage packager) must change to stay consistent with it. Do not
   assume it is frozen: GUI source and `.ts`/i18n changes are compiled into the
   GUI binary by CMake and need no script edit, but anything that adds a runtime
   Qt plugin group, shared library, model companion file, or an expected output
   artifact naming does. When in doubt, run `linux/build-release.sh` and confirm it
   still produces a valid AppImage, then update the notes below if the flow changes.

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
