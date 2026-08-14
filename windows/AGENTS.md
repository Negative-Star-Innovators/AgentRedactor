# Agent Redactor — Agent Notes

This file is a quick reference for working on the Agent Redactor WinUI 3 C++ project.

## Project Layout

Two executables are built from this folder (both land in `build\<Platform>\Release`):

- `AgentRedactorUI.exe` — the WinUI 3 GUI (`AgentRedactor.vcxproj`). A thin frontend:
  it owns the tray, windows, and an `EngineClient` that talks to the engine.
  (Named `...UI` on purpose: the engine's `agentredactor.exe` differs only by
  case from the old name, and Windows filesystems are case-insensitive — the
  two outputs cannot share a directory otherwise.)
- `agentredactor.exe` — the engine (`AgentRedactorEngine.vcxproj`, console subsystem,
  dual-mode). Owns settings, PII detection, the proxy data plane, and a localhost
  control API (127.0.0.1 only, bearer token in `<configDir>/control.json`).
  Run the engine bare (`agentredactor.exe` with no args) or with `--console`
  for a foreground debug run; there is NO `engine run` / `engine stop` CLI
  command — engine lifecycle belongs to the GUI (spawn on startup, stop/lock
  on quit; the engine also launches directly without a console when the GUI
  CreateProcesses it).
  The same binary is also the CLI: `agentredactor <subcommand>` (status,
  get/set, profiles, regex, keywords, unlock, password) talks to
  the running engine over the control API — see "CLI" below.

Key sources:

- `engine/` — engine host: `main.cpp` (entry, mutex, console handling, CLI
  console/pipe plumbing), `EngineApp.*` (owns SettingsManager/PIIDetector/
  ProxyEngine/proxy HttpServers + the whole data plane moved out of the old
  AppState, plus the control-API router), `control_api_client.*` (WinHTTP
  client for the CLI subcommands; the engine-side mirror of the GUI's
  EngineClient — not shared because the GUI build requires pch.h as the
  unconditional first include), `engine_loc.cpp` (English-only LocString
  shim; the engine can't link MRT localization).
- `../core/src/cli.cpp` (+ `../core/include/cli.h`) — OS-agnostic CLI command
  logic: parsing, password gating, output formatting. The OS layer supplies a
  `CliTransport` (control-API calls) and `CliConsole` (print + no-echo
  password prompt); Linux will reuse this file with its own transport.
- `../core/src/control_server.cpp` — localhost-only control API server (bind + token
  auth + control.json). `../core/src/http_server.cpp` has a `loopbackOnly` bind mode.
- `EngineClient.*` — GUI-side WinHTTP client + `SettingsFacade`/`LogsFacade`/
  `ProxyFacade` mirroring the old in-process interfaces so pages barely changed.
- `AppState.*` — now a thin GUI-side holder: EngineClient, tray, message window,
  engine lifecycle (spawn hidden via CreateProcess when /status is unreachable,
  POST /engine/stop on quit if this GUI spawned it), 1-second /status poll thread.
- `AgentRedactor.vcxproj` — Main MSBuild project (WinUI 3 / C++/WinRT).
- `../core/src/` / `../core/include/` — OS-agnostic C++ core (proxy, PII detector, settings, redaction engines, etc.), compiled directly by both vcxprojs; `core/CMakeLists.txt` is scaffolding for future platform builds.
- `src/` / `include/` — Windows-only sources (system tray, secure storage/DPAPI, update manager).
- `HomePage.xaml` / `MainWindow.xaml` — WinUI 3 UI.
- `buildquick.ps1` — Fast local build (builds BOTH vcxprojs + copies models/resources).
- `build.ps1` — Full release build (MSIX packaging; much slower). With `-SelfRelease` it builds the Velopack channel instead (no MSIX).
- `build-selfrelease.ps1` — Self-release wrapper: reads `version.txt`, calls `build.ps1 -SelfRelease`, then runs `vpk pack`.
- `version.txt` — Single version source of truth for BOTH release channels.
  `build.ps1` passes it to both vcxprojs as `-p:AppVersion=<version.txt>` for
  EVERY channel, so it is stamped into the exes as `AR_VERSION_STRING` /
  `APP_VERSION` for C++ (reported by `agentredactor status` as
  `engineVersion`), as `AR_VERSION_TEXT` / `AR_VERSION_QUAD` for the
  VERSIONINFO resource, and as the MSIX `Identity Version` at pack time
  (3-part `x.y.z` → 4-part `x.y.z.0`; the hardcoded version in
  `Package.appxmanifest` is only a fallback when version.txt is absent). A
  bare msbuild invocation falls back to 1.0.0. build.ps1 also verifies the
  produced exes are newer than every source file (stale-incremental-build
  guard) and warns when the MSIX version equals the version already
  installed (sideloading the same version is rejected by Add-AppxPackage, so
  the running app would silently stay on the old build — bump version.txt).

## CLI

`agentredactor.exe` doubles as the CLI; every subcommand talks to the running
engine over the control API (so CLI and GUI always agree). Output goes to the
inherited console, or UTF-8 to stdout when piped (script/AI-agent friendly);
exit codes are 0 success, 1 runtime error, 2 usage.

```
agentredactor status                          engine + profile overview (ungated)
agentredactor get <key> [--profile P]         read a setting
agentredactor set <key> <value> [--profile P] change a setting
agentredactor profiles list                   table incl. request/redaction stats
agentredactor profiles add <alias> [--port N] [--upstream-url U] [--api-key K]
agentredactor profiles delete <id>
agentredactor pii-types list|enable|disable <type> [--profile P]
agentredactor regex list|add <p>|remove <n|p>
agentredactor keywords list|add <t>|remove <n|t> [--ignore-case]
agentredactor password enable | disable       Windows-Hello protection
```

`engine run` / `engine stop` are **not CLI commands at all**: both are
rejected as unknown. Engine lifecycle belongs to the GUI (spawn on startup,
stop/lock on quit), and the CLI test suite stops the engine by killing the
process. (For a foreground debug engine run, launch the bare exe with
`--console`.)

Global keys: `start-on-boot`, `logging`, `show-sensitive`,
`app-language`, `master-password-enabled`/`unlocked` (read-only). Profile keys:
`alias`, `upstream-url`, `api-key`, `port`, `confidence-threshold`,
`pii-types`, `use-ai-model`. `--profile P` selects by list number, id, or
alias (optional when only one profile exists). `onnx-provider` and profile
`enabled` remain engine/GUI-only for now (not exposed on the CLI); profile
`enabled` no longer appears in `status` / `profiles list` output either.

CLI input validation mirrors the GUI, so a bad value is rejected before it
reaches the engine/proxy: `port` must be 1024..65535 and not already used by
another profile; `upstream-url` must be non-empty, start with `http://` or
`https://`, and have a real host; `confidence-threshold` must be 0..1; and
`regex add` validates the pattern (invalid regex is rejected) as well as
rejecting an empty one. The CLI deals only in **single PII types** (e.g.
`secret`, `private_email`) — there are no PII categories on the CLI, matching
the GUI; a category name like `CONTACT` is rejected as an unknown type.

`profiles add` prints the created profile's id on success
("created profile <id> (<alias>)") or a failure message; without `--port` it
picks the first free port from 8080 like the GUI. `profiles delete` accepts
ONLY the profile id (never an alias or list number — those could point at a
different profile later) and refuses to delete the last profile.

Password model: with no master password everything is open. With Windows
Hello enabled there is **no `unlock` command and no session-wide unlock
step**: every gated command (get/set/profiles/regex/keywords/pii-types)
demands a fresh Windows Hello consent on the spot — `/hello/verify` when the
engine session is already unlocked, `/unlock/hello` when locked (the consent
then also unlocks the engine). `status` and bare `agentredactor` (help) stay
open; `password disable` is the ungated recovery path. The engine lock itself
remains UX-level; the only server-side enforcement is
`GET /profiles/<id>/apikey` (403 while locked) — `GET /profiles` always masks
keys as `abc...****`.

Windows Hello unlock (Windows only; Linux/CLI core stays password-only):
- Engine: `POST /unlock/hello` (always 200 with `ok`/`canceled`/
  `retriesExhausted`/`unavailable`/`helloNotEnabled`/`error` fields) and
  `POST /unlock` (SAME unlock but WITHOUT a consent prompt — the caller has
  already verified in-process), `POST /hello/verify` (standalone consent, no
  state change), `PUT /settings/lock`
  (locks the session — the GUI sends it on quit when the engine survives),
  `enableMasterPassword` accepts `hello: true` (with an empty `value` it
  creates a **Hello-only** session: a random AES key wrapped in DPAPI, no
  typed password at all — `SecureStorage::EnableMasterPasswordHelloOnly`);
  `helloEnabled` in `status`/`settings`. The hello
  secret is a DPAPI-wrapped AES key persisted under `master_password.hello`
  in settings.json (`secure_storage.cpp` — `EnableHello`/`DisableHello`/
  `UnlockWithHello`/`EnableMasterPasswordHelloOnly`/`Lock`/`DpapiProtect`).
- CLI: `password enable` (no password anywhere) prints "windows hello
  protection enabled" and no typed password exists. With protection on, every
  gated command prompts for a fresh Hello consent (see "Password model"
  above); `get api-key` therefore works like any other gated read. `password
  disable` stays usable on a locked session (the recovery path).
- GUI: ALL consent prompts are in-process (the GUI compiles
  `engine/hello_unlock.cpp` too, so the Windows Security dialog belongs to
  the GUI process and stays in front — no more background prompt from the
  engine process). Enabling protection is DIRECT (checkbox → engine, no
  dialog, no consent); disabling keeps a Hello consent before the engine
  call. When protection is on, the window shows a **lock overlay** (opaque
  black scrim + lock icon + "Windows Hello required" + Try again button) and
  the Windows Hello prompt runs immediately on every appearance: first start,
  and close-to-tray → tray Open re-open. The engine is locked on window hide
  and after **10 minutes without input** (any keyboard/mouse/touch activity
  resets the timer), at which point the overlay + prompt re-appear. A failed
  or cancelled prompt leaves the retry/exit dialog; `AppState::Shutdown`
  locks the engine (`PUT /settings/lock`) when it is left running so the next
  open must authenticate again. The overlay lives in MainWindow (covers all
  pages); `HomePage` no longer runs any startup lock flow. The overlay is
  ALSO shown for the duration of every other in-app Hello prompt (the
  disable-protection consent via `AppState::SetSessionLockOverlay`), so the
  regex/keywords/profile content is never visible behind the Windows Security
  dialog — the window always shows the padlock while Hello is on screen. As a
  hard privacy guarantee, `ShowLockOverlay`/`HideLockOverlay` ALSO collapse
  the content `frame_` itself (Visibility Collapsed/Visible): even if the
  overlay ever failed to render, nothing of the app content is in the visual
  tree while a Hello prompt is up.
- `windows/engine/hello_unlock.cpp` drives `UserConsentVerifier`
  (`IsWindowsHelloAvailable` + blocking `RequestHelloUnlock` for the engine's
  control-API handlers + coroutine `RequestHelloUnlockAsync` for the GUI):
  the consent prompt is real system UI and waits for the user, so the call
  runs a watchdog (default 60 s, overridable via `AGENTREDACTOR_HELLO_TIMEOUT_MS`
  so test harnesses fail fast) that cancels the operation and reports
  `Unavailable` instead of hanging.
  **`AGENTREDACTOR_HELLO_SUPPRESS_PROMPT` (test-only, runtime env var, no
  compile-time gate)** makes every consent prompt behave exactly as if the
  user pressed cancel: no system UI is ever shown and the result can NEVER be
  Verified. This is cancel-equivalent and therefore grants nothing — a user
  canceling the prompt was always possible in production, so the flag cannot
  be abused even though it exists in shipped binaries (env vars are settable
  by any process; that is fine because the flag only ever yields
  Canceled/false). SECURITY INVARIANT, enforced by the CLI test
  `test_hello_suppress_prompt_flag_never_grants_access` (runs against the
  release binary): the flag must never return Verified, never reach the
  engine's real unlock, and never serve the API key. Any change that lets it
  grant access is a critical vulnerability. The real unlock path (successful
  Windows Hello verification) is inherently interactive and is never
  automated by the test suite.
  `RequestHelloUnlock(HWND hwnd, msg)` attaches the prompt to `hwnd` via the
  desktop interop interface `IUserConsentVerifierInterop`
  (`RequestVerificationForWindowAsync`) — this is what makes the Windows
  Security dialog come to the foreground of the calling app instead of opening
  in the background. The HWND is supplied by whoever initiated the prompt:
  the GUI passes its main window (`AppState::MainWindow()`); the CLI transport
  passes the console window (`GetConsoleWindow()`) by tagging `?hwnd=<decimal>`
  onto `/unlock/hello` and `/hello/verify`, which `EngineApp::ParseHwndQuery`
  reads back (HWND values are valid cross-process on the same desktop). The
  window-attached interop call must run where the window's message pump lives,
  so the GUI uses the async variant on the UI thread; the engine's blocking
  variant falls back to unowned `RequestVerificationAsync` when the interop
  call fails from a worker thread. The interop COM calls are wrapped in SEH
  (raw COM only, `CreateWindowedVerification`) so a broken consent service
  degrades to the unowned fallback instead of an access violation. The async
  variant's prompt watchdog is a detached `std::thread` holding shared_ptr
  ownership — the earlier `fire_and_forget` + `winrt::resume_after` watchdog
  coroutine raced its own frame teardown and crashed (SEH 0xC0000005 at the
  `xchg` of `done->exchange` after `CloseThreadpoolTimer`, fault offset
  0x930EE in the 1.1.3 build) — do NOT reintroduce a coroutine watchdog here.
  The CLI transport special-cases `/unlock/hello` and `/hello/verify` with a
  90 s WinHTTP receive timeout (the usual 3 s would abort mid-prompt):
  `windows/engine/control_api_client.cpp`.
- Reminder: availability is NOT enrollment — `CheckAvailabilityAsync` returns
  Available on machines with no Hello configured; a headless run leaves an
  unanswered "Windows Security" prompt on the desktop until the watchdog
  fires. The GUI stays on the padlock overlay after any failed/cancelled
  attempt (no dialog, no exit); the CLI reports exit code 1 + message
  ("windows hello consent canceled", "...not available on this device", ...).

## Quick Build (for testing)

Run from the `windows` folder:

```powershell
.\buildquick.ps1
```

This will:
1. Stop any running `AgentRedactorUI.exe` / `agentredactor.exe`.
2. Find MSBuild (BuildTools / Enterprise / Professional).
3. Build `Release|x64` for the GUI and the engine (change with `-Configuration Debug` if needed).
4. Copy the `models\` folder and icon resources to `build\x64\Release\`.
5. Produce `build\x64\Release\AgentRedactorUI.exe` and `agentredactor.exe`.

The EXE can be run directly from that folder for rapid iteration.

## Full Release Build (MSI + MSIX)

Run from the `AgentRedactor` folder when you need installer packages:

```powershell
.\build.ps1
```

This performs a release build and then packages the output into:
- `build\AgentRedactor-<arch>.msix` (per architecture; pass `-Platform x64` or `-Platform ARM64`, default x64)
- `build\AgentRedactor.msixbundle` (x64 + ARM64 combined, via `.\buildbundle.ps1` after building both archs — optional; for the Store we submit the two per-arch `.msix` files in one submission instead, since the bundle exceeds GitHub's 2 GB release-asset limit)
- `build\msi\AgentRedactor.msi`

The script also:
- Generates a multilingual `resources.pri` from all `Strings\<lang>\Resources.resw`
  files using `makepri.exe`.
- Builds a multilingual MSI with embedded language transforms for every supported
  language.

This script downloads NuGet and WiX v3.14 if they are not present, so the first run may take a while.

## Release Channels

The app ships through two channels from the same codebase, both versioned from
`version.txt` (see the project layout above):

- **Microsoft Store (MSIX)** — `.\build.ps1`. The full ~1.6 GB model weights are
  packed inside the MSIX. Contains **zero** update code (Store policy). The MSIX
  `Identity Version` is stamped from `version.txt` at pack time (x.y.z → x.y.z.0),
  so a `new-release.ps1` bump also versions the next Store submission; submission
  itself is manual (download both per-arch MSIX files from the rolling `latest`
  GitHub release, upload to Partner Center).
- **Self-release (Velopack)** — `.\build-selfrelease.ps1 [-Platform x64|ARM64]`.
  Compiles with `AGENTREDACTOR_SELFRELEASE` (via `-p:SelfRelease=true
  -p:AppVersion=<version.txt>`), excludes `*.onnx_data` from the model copy
  (weights download on first run to `%LOCALAPPDATA%\windows\models`),
  skips MSIX packing, and runs `vpk pack` into `build\velopack\` (x64,
  channel `win`) or `build\velopack-arm64\` (ARM64, channel `win-arm64`).

Key pieces of the self-release channel:

- `version.txt` is the version source of truth; it becomes `AR_VERSION_STRING`
  → `APP_VERSION` (`../core/include/constants.h`) and the `vpk pack` version. For the
  exe's VERSIONINFO resource, the vcxproj passes `ResourceCompile` a separate
  quote-free define set (`AR_VERSION_TEXT` + `AR_VERSION_QUAD`, stringized in
  `resources/app.rc`) because embedded quotes do not survive the MSBuild →
  rc.exe command line (the quoted value silently compiled in empty and the exe
  read as 0.0.0.0). Keep `app.rc` to a SINGLE StringFileInfo block: version.dll
  (GetFileVersionInfo) rejects VERSIONINFO resources larger than 32 KB — the
  old fully-localized 53-block resource exceeded that and read as 0.0.0.0.
- `src/update_manager.cpp` (all inside `#ifdef AGENTREDACTOR_SELFRELEASE`):
  polls `releases.<channel>.json` from the arch-aware update feed
  (`https://api.agentredactor.negativestarinnovators.com/updates/win` on x64,
  `.../updates/win-arm64` on ARM64, selected via `_M_ARM64`),
  downloads newer full packages, and applies them via the bundled
  `Update.exe apply --package <nupkg> --waitPid <pid>` after a restart prompt.
  `App.cpp` exits immediately on `--veloapp-*` lifecycle args.
- `../core/src/model_downloader.cpp` (both channels): first-run download of
  `model_quantized.onnx_data` exclusively from the Cloudflare R2 endpoint
  (`https://api.agentredactor.negativestarinnovators.com/models/...`; R2 is
  the single host for model downloads — no fallback). The download is parallel
  segmented + resumable (`Utils::HttpDownloadFileSegmented`, 8 ranges to
  `.partN` files, falling back to a resumable single stream); interrupted
  downloads keep `.partial`/`.partN` files so a retry resumes. Weights are
  only accepted at exactly `kWeightsExpectedBytes` — a wrong-sized file is
  deleted and re-downloaded (self-heal for old corrupt installs). Only fires
  when the weights are missing. `AppState` resolves the model dir via
  `ModelDownloader::ResolveModelDir()` (exe-dir first, fallback second), and
  deletes fallback-dir weights (never MSIX exe-dir weights) when the detector
  fails to initialize right after a successful download.
- To cut a self-release: bump `version.txt`, tag `v<version>`, push — the
  `release-selfrelease.yml` workflow builds and packs both arches, then gates
  the R2 publish on the settings-migration tests, the fresh-install E2E
  (incl. a first-run model-download smoke test), the previous-live-release
  upgrade E2E (`tests/migration/`, per channel; skips with a warning when the
  channel has no live release yet), and the FlaUI GUI suite on both arches
  (`tests-gui.yml`, shared with `tests.yml`). After publishing, a
  `verify-live` job runs the public `install.ps1` one-liner on fresh
  x64/ARM64 runners as a canary. The workflow also runs on PRs as a dry-run
  (no publish, no GUI suite — the build is stamped live+1 patch so the
  upgrade E2E still runs). Local loop: `.\build-selfrelease.ps1` then install
  `build\velopack\*-Setup.exe`.

## Common Files to Know

| Area | Key files |
|------|-----------|
| Startup / tray-only launch | `App.cpp`, `../core/include/constants.h` (`RegisterStartupTask`) |
| Main window sizing | `MainWindow.cpp` |
| Home page UI layout | `HomePage.xaml`, `HomePage.cpp` |
| Settings / start-on-boot | `SettingsPage.xaml`, `SettingsPage.cpp`, `AppState.cpp` |
| Proxy engine | `../core/src/proxy_engine.cpp`, `../core/src/http_server.cpp` |
| Settings persistence | `../core/src/settings_manager.cpp`, `../core/src/migrations/settings_migrator.cpp` |

## Changing the settings schema

`settings.json` is versioned via `settings_version` (absent == 1, the original
unversioned layout). The current version is `SETTINGS_SCHEMA_VERSION` in
`../core/include/migrations/settings_migrator.h`. To change the schema:

1. Bump `SETTINGS_SCHEMA_VERSION` by one.
2. Add a `MigrateNToN+1` step to the ordered table in
   `../core/src/migrations/settings_migrator.cpp` (marked "ADD FUTURE MIGRATIONS
   HERE"). Never edit or reorder steps that have shipped — older installs
   replay them verbatim.
3. Add a fixture under `tests/migration/fixtures/settings/` (a pre-migration
   `settings.json` plus its expected post-migration form) so the
   `--selftest-migrate-settings` migration tests cover the new step.

`SettingsManager` runs `SettingsMigrator::MigrateInPlace` right after every
load and saves when anything changed. On a JSON parse failure the corrupt file
is first backed up to `<name>.corrupt-<timestamp>.bak` and only then reset.

## Build Requirements

- Windows 10/11
- Visual Studio 2022 Build Tools (or higher) with the **Desktop development with C++** workload
- Windows SDK 10.0.17763.0 or later (the SDK's `makepri.exe`, `MakeAppx.exe`, `wilangid.vbs`, and `MsiTran.exe` are used for packaging)
- NuGet packages are already checked in under `packages\`; MSBuild will use them directly

## End-to-End Tests

Testing is done through the GUI E2E suite in `tests/gui/`. It drives the **real** `AgentRedactorUI.exe` Windows UI with a FlaUI/C# helper and verifies proxy behaviour against an `aiohttp` mock LLM. The CLI suite in `tests/cli/` runs `agentredactor.exe` standalone against an isolated config dir (`AGENTREDACTOR_CONFIG_DIR`) and drives the CLI subcommands as subprocesses — fast, no GUI, no FlaUI helper needed (`pytest -v cli/`).

### Build for testing

The GUI tests use the normal Release binary; no test-only build flag is required.

```powershell
.\buildquick.ps1 -Configuration Release
```

### Run the tests

```powershell
cd ..\tests
pip install -r requirements.txt
pytest -v gui/
```

## GUI End-to-End Tests

A small FlaUI/C# helper drives the real Windows UI from Python. The goal is to exercise proxy redaction, statistics, and profiles through the actual application window and settings persistence path. Protocol-mode translation was removed; the proxy always forwards requests unchanged (`none` mode).

### Layout

- `tests/gui/windows/FlaUIHelper/` — C# console app using **FlaUI.UIA3**.
- `tests/gui/windows/build.ps1` — builds the helper with the same MSBuild toolchain used for AgentRedactor.
- `tests/gui/windows/gui_driver.py` — Python wrapper that invokes `FlaUIHelper.exe`.
- `tests/gui/gui_process.py` — starts/stops `AgentRedactorUI.exe` (and the `agentredactor.exe` engine it spawns) using the real `%APPDATA%` / `%LOCALAPPDATA%` directories.
- `tests/gui/conftest.py` — backs up the user's real `AgentRedactor` data, creates a clean test profile, and restores the original data after the test.
- `tests/gui/test_gui_keyword_redaction.py` — focused keyword scenarios: case-insensitive/case-sensitive matching, deletion, toggling case sensitivity, modifying keyword text, statistics/session redactions, and log output.
- `tests/gui/test_gui_regex_redaction.py` — focused regex scenarios: adding, deleting, modifying, enabling/disabling, statistics/session redactions, and log output.
- `tests/gui/test_gui_pii_controls.py` — toggles PII types through the Home page grid and asserts person/email/disabled/toggle behaviour.
- `tests/gui/test_gui_statistics.py` — verifies request counts and per-session redaction labels are surfaced in the UI.
- `tests/gui/test_gui_protocol_modes.py` — **removed/deprecated**: protocol-mode translation is no longer available.
- `tests/gui/test_gui_profiles.py` — adds a second profile through the UI.
- `tests/gui/test_gui_verbose_logging.py` — toggles verbose logging through the UI.
- `tests/gui/test_gui_master_password.py` — the Windows-Hello lock scenarios
  (enable direct, startup lock padlock overlay, disable via CLI while
  locked). These tests set `AGENTREDACTOR_HELLO_SUPPRESS_PROMPT=1` (plus
  `AGENTREDACTOR_HELLO_TIMEOUT_MS=1500` as belt-and-braces) so the auto-run
  Hello prompt behaves as cancelled: no system "Windows Security" dialog ever
  appears, so the harness is never blocked by it on Hello-enabled machines
  and the lock flow is deterministic anywhere.

### Build the helper

```powershell
cd tests\gui\windows
.\build.ps1
```

This uses the existing `windows\build\tools\nuget.exe` to restore `FlaUI.Core`, `FlaUI.UIA3`, and `Microsoft.NETFramework.ReferenceAssemblies.net48`, then compiles with MSBuild.

### Run the GUI tests

```powershell
cd tests
pytest -v gui/
```

Each test:
1. Backs up `%APPDATA%\AgentRedactor` and `%LOCALAPPDATA%\AgentRedactor`.
2. Writes a clean test `settings.json` into `%APPDATA%\AgentRedactor`.
3. Starts the normal `AgentRedactorUI.exe` window.
4. Uses the FlaUI helper to drive the UI (add keywords/regex/PII toggles/profiles).
5. Sends OpenAI and/or Anthropic requests through the proxy and asserts redaction and statistics.
6. Stops the app and restores the original user data.

### Current status

- Twenty-four GUI end-to-end tests are implemented and pass reliably with the normal Release binary.
- The test-only `--data-dir` option and `AGENTREDACTOR_ENABLE_TEST_COMMANDS` build flag have been removed; the GUI suite uses the real `%APPDATA%` / `%LOCALAPPDATA%` paths and backs them up during each test.
- PII checkboxes are toggled via the UIA `TogglePattern` instead of mouse clicks, avoiding `NoClickablePointException` for virtualized grid items.
- A small app-side fix in `HomePage::ShowHttpWarningAsync()` guards against showing a second HTTP warning `ContentDialog` while one is already open, which previously caused a catastrophic failure when saving an HTTP-backed profile multiple times.
- The FlaUI helper supports keyword-management commands (`get-keywords`, `toggle-keyword`, `delete-keyword`, `set-keyword-text`, `set-keyword-case`). Keyword list rows expose stable `AutomationId` values (`KeywordCheckBox_*`, `KeywordCaseButton_*`, `KeywordTextBox_*`, `KeywordDeleteButton_*`) so the helper can read and manipulate them without relying on the non-exposed `KeywordListPanel` container.

## Notes

- The "Fatal error occurred. See debug.log for details." dialog is
  `MyExceptionFilter` in `App.cpp` (an SEH crash in the GUI). `debug.log`
  (next to the exe) is **rotated, not deleted**, at every startup — the
  previous run's crash frames land in `debug.prev.log`, so a crash report
  must be read from BOTH files before the next launch overwrites the
  rotation. A crash with 0xc0000409 in ucrtbase usually means an unhandled
  C++ exception escaped a `fire_and_forget` coroutine; 0xc000027b is a
  stowed WinUI XAML exception.
- The WinUI 3 project defines `DisableXamlGeneratedMain=true` and uses a custom `wWinMain` in `App.cpp`.
- The old Win32 UI in `src/app.cpp` is **not** compiled by `AgentRedactor.vcxproj`; it is left in the repo for reference.
- When changing XAML, MSBuild will regenerate the `.xaml.g.hpp` files automatically.
- Localization uses `.resw` files under `Strings\`. `EnablePriGenTooling` and `EnableCoreMrtTooling` are disabled in the `.vcxproj` because the BuildTools edition does not include the AppxPackage MSBuild targets; `build.ps1` and `buildquick.ps1` generate the PRI files manually with `makepri.exe`.
- The build scripts configure `makepri.exe` with default qualifiers for all supported
  languages so every translation is included in the generated `resources.pri`.
- At runtime the app applies the saved language by setting the MRT `ResourceContext` `Language` qualifier (`Windows.ApplicationModel.Resources.Core.ResourceManager.Current.DefaultContext().QualifierValues()["Language"]`); this is the supported mechanism for unpackaged desktop apps. `PrimaryLanguageOverride` is also attempted but may be ignored on unpackaged apps.
- Supported languages are declared once in `../core/include/constants.h` (`SUPPORTED_LANGUAGES`).
  See `docs/languages.md` for the full list, RTL/LTR direction, and script families.
  The Settings language `ComboBox` and the tray Language submenu are built dynamically
  from this list, so adding a new language only requires a new `Strings/<tag>/Resources.resw`
  file, a matching `packaging/AgentRedactor.<tag>.wxl`, and an entry in `SUPPORTED_LANGUAGES`.
- The tray Language submenu shows a checkmark next to the active language and automatically restarts the app when a different language is selected.
- Right-to-left (RTL) layout is supported for Arabic (`ar-SA`), Hebrew (`he-IL`), and Urdu (`ur-PK`). The app sets `FlowDirection="RightToLeft"` on the main window frame and on each page when the current language is RTL.
- To regenerate all `.resw` and `.wxl` files (including the existing German file), run:
  ```powershell
  python windows/generate_localizations.py
  ```
- A helper script, `windows/generate_new_languages.py`, can translate the English `.resw` strings into new languages using Google Translate via `deep-translator`. It is intended for bootstrapping translations and should be reviewed by native speakers before release.
