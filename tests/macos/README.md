# macOS release tests (osx-arm64 Velopack channel)

Headless release E2Es for the macOS self-release channel. Unlike
`tests/gui/`, these do not drive the UI — they exercise the packaged release
(installer, feed, bundled engine) the way a user receives it.

Run from the repo root (one pytest process per suite — the suites share the
`conftest` module name and cannot be collected in a single invocation):

```bash
cd tests
python -m pytest macos -q
```

## `test_update_feed.py`

Ports `tests/linux/test_update_feed.py` to the osx-arm64 channel: the DMG-packed
`Agent Redactor.app` (including UpdateMac) is launched with
`AGENTREDACTOR_UPDATE_FEED` pointed at a loopback `http.server` feed. The
pre-pack staging app is used to synthesize vNext because vpk adds its helper
symlinks while packing.

- **own-feed check** — the build's own (same-version) vpk output is served on
  loopback; the app must request `releases.osx-arm64.json` and stay healthy.
- **vNext detection** — the app must detect a newer feed and stay healthy.
- **full update** — vNext feed → download → apply → relaunch, asserting
  Velopack's `sq.version` metadata (synthetic packing does not rewrite the
  existing app's Info.plist).

The GUI's updater path is `VelopackUpdateProvider` (compiled in only for
self-release builds): check → download → restart prompt →
`vpkc_wait_exit_then_apply_updates`. `AGENTREDACTOR_UPDATE_AUTOAPPLY=1` skips
the prompt, exactly like the Windows/Linux self-release builds. The full swap
test is opt-in locally via `AGENTREDACTOR_UPDATE_SWAP_TEST=1`; CI enables it in
`release-macos.yml`, so all three tests gate publishing.

Skipped unless `macos/build-release.sh` has produced both the staging and
DMG-packed apps and `vpk` is on PATH. The binding itself can also be verified
without vpk/dotnet via `macos/tools/velopack-harness/run.sh`.

## `test_selfrelease_install.py` (marker: `install`)

The fresh-install E2E for the osx-arm64 channel, mirroring
`tests/migration/test_selfrelease_install.py`:

1. Mounts the build's DMG read-only, verifies that it contains
   `Agent Redactor.app` and an `/Applications` symlink, and copies the app to
   `~/Applications` by default (CI selects `/Applications`), matching a user's
   drag-and-drop installation. `AGENTREDACTOR_INSTALL_DIR` overrides the parent.
2. Verifies the installed `Agent Redactor.app` with the engine at
   `Contents/Resources/agentredactor`.
3. Asserts the app version (`AGENTREDACTOR_EXPECT_VERSION`, via the bundle's
   `CFBundleShortVersionString`).
4. Asserts the bundled engine is a native arm64 Mach-O (guards against
   packing the wrong arch into the osx-arm64 channel).
5. Runs `--selftest-migrate-settings` on the installed engine against an
   isolated config dir.
6. With `AGENTREDACTOR_FEED_DIR`: the installed app's own update check is
   pointed at its own feed over localhost and must fetch
   `releases.<channel>.json` (the macOS GUI has no "[UpdateManager] Up to
   date" lifecycle-log contract yet — the assertion is the fetch itself).
7. With `AGENTREDACTOR_MODEL_SMOKE=1`: launches the app with an isolated
   `HOME` and asserts the first-run model download starts fetching from R2
   (growing `.partial`/`.partN` files under
   `<HOME>/Library/Application Support/AgentRedactor/models`), then kills it.
8. Removes the installed app again unless `AGENTREDACTOR_KEEP_INSTALL=1`.

It is **opt-in** and skipped unless `AGENTREDACTOR_INSTALL_TEST=1`:

```bash
export AGENTREDACTOR_INSTALL_TEST=1
export AGENTREDACTOR_DMG="$PWD/../macos/build-release/velopack/AgentRedactor-osx-arm64.dmg"
export AGENTREDACTOR_EXPECT_VERSION="1.2.15"   # optional
cd tests
pytest -v macos/test_selfrelease_install.py
```

Caveat: it kills running Agent Redactor app/engine copies to avoid the
single-instance guard intercepting the test — don't run it while using the app.

## CI wiring

`.github/workflows/release-macos.yml` runs this suite on the `macos-15`
(arm64) runner after `macos/build-release.sh`: the fresh-install E2E against
the just-created DMG (including its Applications shortcut, feed check, and
model smoke), then the update-feed E2E. Both gate the R2 publish.

## Out of scope: Accessibility GUI harness

A macOS GUI test harness (Accessibility API / XCTest-based, driving the real
SwiftUI app) is intentionally **not** part of this suite — same rationale as
the Windows FlaUI harness living in `tests/gui/`, but no macOS driver exists
yet. These tests stay headless: installer, feed, bundled engine.
