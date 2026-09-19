# PR #38 CI triage — handoff notes (2026-09-17)

Branch: `fix/linux-model-download-dialog-and-resume` → PR #38.

## 1. Original failure: Linux build compile error — FIXED ✅

**Symptom:** `Build Linux` workflow failed on both arches:
```
linux/gui/theme.cpp:19:56: error: 'class QStyleHints' has no member named 'colorScheme'
linux/gui/theme.cpp:20:23: error: 'Qt::ColorScheme' has not been declared
linux/gui/theme.cpp:240:54: error: 'colorSchemeChanged' is not a member of 'QStyleHints'
```

**Root cause:** Qt version mismatch. The PR uses `QStyleHints::colorScheme()` /
`colorSchemeChanged` (Qt 6.8+) and `Qt::ColorScheme` (6.5+), but Linux CI builds
on `ubuntu-24.04` with apt `qt6-base-dev` = **Qt 6.4** (`.github/workflows/build-linux.yml:57`),
which is also what the AppImage ships. Dev box runs Qt 6.10, so it compiled locally.

**Fix (committed `8529d95`, pushed):** `#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)`
guards in `linux/gui/theme.cpp` (`isDark()` and the `colorSchemeChanged` connect),
falling back to palette lightness + the existing delayed re-check timers and
PaletteChangeFilter on Qt 6.4. Docs updated (`linux/README.md`, `linux/AGENTS.md`)
to state the Qt 6.4 floor. Verified: full build on Qt 6.10 (6.8+ path) + forced-off
compile of the 6.4 fallback path; CI run 35151806762 now **compiles on both arches**
and passes CLI (40), migration (5), offscreen smoke (11), AT-SPI 20/21.

## 2. Remaining failure: `test_pii_type_toggle` (AT-SPI) — NOT FIXED, needs work

Fails deterministically on **both** arches in run 35151806762 (and would on any rerun):

```
FAILED tests/linux/test_gui_atspi.py::test_pii_type_toggle
AssertionError: assert 'Alice Smith' not in 'My name is Alice Smith.'
```

The test is **new in this PR**; it never ran on CI before because the build failed
at compile first. `test_pii_master_switch` (same card, same save flow) passes.

### What the test does
Phase 1: uncheck Person → Save → "My name is Bob Marley." must pass through
unredacted (**passes**). Phase 2: re-check Person → Save → "My name is Alice
Smith." must be redacted (**fails** — raw text reached the mock LLM).

### Evidence gathered (from CI artifact `atspi-diagnostics-arm64` of run 35151806762)
- Final `config/settings.json` on the engine: `enabled_pii_types` =
  `[account_number, private_address, private_date, private_email, private_phone,
  private_url, secret]` — **`private_person` missing**. The re-enable never
  persisted to the engine.
- Engine log: PIIDetector RUNs for both requests and detects
  `B/E-private_person` both times (model runs regardless; entities are filtered
  by enabled types downstream in `LabelsToEntities`), consistent with person
  still disabled at request time.
- GUI stdout log: only font/DBus noise, no validation/dialog errors.
- Upstream URL is `http://127.0.0.1` → no HTTP security-warning dialog
  (`validateForm` only warns for non-local http).

### Relevant code facts
- PII checkboxes **autosave on toggle**: `connect(check, &QCheckBox::toggled,
  this, autosave)` at `linux/gui/main_window.cpp:471` — so re-checking should PUT
  immediately, before the test's explicit `save_profile()` (which presses the
  API Proxy card's Save + sleeps 1s).
- `onSaveProfile` (`main_window.cpp:1246`): `gatherProfileFromForm()` reads the
  live `piiChecks_` widgets → synchronous `PutProfile` → `reloadProfiles(true)`.
- Engine reads the profile **per request** (`EngineApp::HandleProxyRequest` →
  `settings_->GetProfileByPort`), so no engine-side caching of enabled types;
  if the PUT carried `private_person`, redaction would happen.
- Settings poll: profile mutations bump `profilesRevision`; the poll calls
  `reloadProfiles(true)` when `!dirty_` (`main_window.cpp:843-848`), rebuilding
  form widgets.

### Leading theory (test-driver race, unproven)
Phase-2's AT-SPI click on the Person checkbox is swallowed by the
poll-triggered `reloadProfiles` rebuild (widget replaced between find/press and
Qt processing the action), so `toggled` never fires, no autosave PUT happens,
and the subsequent Save PUT still gathers the box as unchecked. Supporting
detail: the driver already knows this race class — row buttons use
`press_until` (press + verify observable state + retry, `tests/linux/gui_atspi.py`)
precisely because "a press that lands while the settings-poll reload is
rebuilding the rows can be swallowed" — but `set_checkbox` (`gui_atspi.py:489`)
uses the weaker `press_named` and **never verifies the state changed**.

### Candidate fixes, in order
1. **Test-side (most likely, minimal):** make `set_checkbox` verify state after
   pressing and retry via the existing `press_until`, e.g. press until
   `_checked(self.checkbox(name)) == on`.

### Resolution (2026-09-17, applied)
Candidate #1 was implemented. `AtspiGui.set_checkbox` in
`tests/linux/gui_atspi.py` now uses the same verify-and-retry mechanism as the
proven row-button helpers: it re-finds the checkbox per attempt via `press_until`
and re-presses until the observable `CHECKED` state equals the target, instead of
issuing one blind `press_named`. If the box is already in the target state it
skips the press (PII boxes autosave on toggle, so no extra PUT). Clean `py_compile`.
This is a test-driver-only change; no product code touched.

Local validation under an expanded sandbox: the AT-SPI a11y bus starts, the
single-instance socket is bypassed (uid-shim creates `-4242` socket), and the
GUI stays alive with `XDG_DATA_HOME` set. The GUI never registers on the a11y
bus here because this sandbox's Debian Qt 6.10 lacks the
**`libqt6_linuxaccessibility` / `accessiblebridge` plugin** (absent from apt; the
CI runner's Ubuntu 24.04 / Qt 6.4 includes it). So the full test cannot be
executed sandboxed; verification is delegated to CI run on the next push.

2. If that doesn't hold: add temporary `qWarning` logging in
   `onSaveProfile`/`gatherProfileFromForm` dumping `enabled_pii_types`, rerun CI,
   and see what the phase-2 autosave PUT actually carried.
3. Only then suspect product code (PR touches `core/src/settings_manager.cpp`,
   `pii_detector.cpp`, `proxy_engine.cpp`).

### Local reproduction blockers (environment, pre-existing)
- A **live `agentredactor-gui --tray-only` instance runs on this dev machine**
  (check `pgrep -fa agentredactor`). The GUI single-instance guard uses a
  filesystem socket `/tmp/agentredactor-gui-<uid>` (`linux/gui/main.cpp:166`),
  NOT scoped to the test's isolated config dir — so every test-launched GUI
  forwards to it and exits rc=0. This also explains the 6 pre-existing local
  `test_gui_smoke.py` failures ("GUI exited while the engine was up"), which
  fail identically WITHOUT any of my changes (verified via stash).
- Workaround attempt: `LD_PRELOAD` shim returning uid 4242 (`getuid`/`geteuid`)
  via `AGENTREDACTOR_GUI_BIN` wrapper changes the socket name and bypasses the
  guard, but in a manual Xvfb/dbus env the GUI then still self-exits rc=0 via
  normal `main` return (caught with `gdb -ex 'catch syscall exit_group'` —
  backtrace only shows exit handlers, not the return site). Unresolved; the
  fixture's real env may differ from my manual one. **Simplest path: ask the
  user to quit the tray instance, then run the standard command:**
  ```
  cd tests
  PYTHONPATH=/usr/lib/python3/dist-packages \
    dbus-run-session -- xvfb-run -a -s "-screen 0 1280x800x24" \
    python -m pytest linux/test_gui_atspi.py::test_pii_type_toggle -q
  ```

## 3. Local env notes for the next agent
- Local Qt is 6.10 (compiles 6.8+ APIs — always think about the 6.4 CI floor).
- Do NOT delete `/tmp/agentredactor-gui-1000` — it belongs to the user's
  running instance.
- `debug_out/repro_atspi_stress.py` and the `gdb_*.bt` files predate this
  triage (dev's own AT-SPI crash investigation from 2026-09-13).
