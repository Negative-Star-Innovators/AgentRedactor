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
- Per-user runtime state beyond settings.json (currently `redaction_state.json` — placeholder label counters, no PII) lives in the config dir next to settings.json. Keep any new per-user state file there so the uninstaller (`RemoveAll(GetAppDataPath())`) and `scripts/linux-clean-slate.sh` pick it up automatically.
- AppImage entrypoint dispatch: the packed `agentredactor-gui` is a bash wrapper (generated in `build-release.sh`), not the ELF. Known CLI subcommands (`status`, `get`, `set`, `keywords`, `download-model`, `update`, …) always dispatch to the engine/CLI binary, and when no `DISPLAY`/`WAYLAND_DISPLAY` exists EVERYTHING dispatches to it (headless/WSL/SSH); a bare headless launch prints a hint instead of starting Qt. Keep the dispatch list in the wrapper in sync with the CLI commands in `core/src/cli.cpp`.
- Headless operation needs no GUI: `agentredactor --console` runs the engine, the model download is driven by `agentredactor download-model` (ungated so a fresh install can bootstrap), and `agentredactor update` swaps the AppImage for the latest channel release (uses `$APPIMAGE`; a running process keeps the old inode until restarted). The install script symlinks `agentredactor` into `~/.local/bin`.
- Headless install end-state (`install.sh`): no display → the script installs and enables a **systemd user service** (`~/.config/systemd/user/agentredactor.service`, ExecStart `~/.local/bin/agentredactor --console`) plus `loginctl enable-linger` so the engine starts at boot without a login; without systemd it falls back to a direct `nohup` start (survives neither logout nor reboot). Opt out with `AGENTREDACTOR_NO_SERVICE=1`. The uninstaller disables/removes the unit and the `~/.local/bin` symlink. The engine is per-user (settings/API keys/control token live in the user's home) — correct for WSL, dev boxes, and single-user servers; on a multi-user server only one user's engine can bind the default ports, so it is effectively one engine per machine unless ports are customized.
- WSL/xcb: the AppImage bundles the `libxcb-*` EXTENSION libraries (icccm, image, keysyms, randr, render-util, xinerama, cursor) and `libxkbcommon-x11` so the GUI works on WSLg/minimal images out of the box; core `libxcb.so.1`/`libX11`/`libxkbcommon` stay host-side. The AppImage entrypoint still `ldd`-checks the xcb plugin and falls back to headless dispatch with an `apt install` hint when host deps are unresolvable, and Build Linux CI fails when the staged plugin has unresolved deps.

## General rules

- Keep Linux-specific code in `linux/`; share logic through `core/` when it is not platform-specific.
- Match the existing code style (no unnecessary comments, clear names).
- Update this file if the build/test commands or project layout change.
