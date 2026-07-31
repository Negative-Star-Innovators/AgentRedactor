# Settings migration & self-release upgrade tests

Headless tests for the settings schema and the Velopack self-release update
channel. Unlike `tests/gui/`, these do **not** need an interactive desktop —
they drive the exe's `--selftest-migrate-settings` hook (and, for the upgrade
E2E, the update-manager test hooks), which runs before any WinUI startup.

## What each test covers

### `test_settings_migration.py`

Runs `<exe> --selftest-migrate-settings` with `AGENTREDACTOR_CONFIG_DIR`
pointing at a temp dir seeded with a fixture `settings.json`, then asserts on
exit code, the `SETTINGS_MIGRATION_OK` stdout marker, and the resulting file:

- **v1 fixtures** (`fixtures/settings/v1_*.json` + matching `.expected.json`):
  an unversioned (schema v1) file is migrated to schema v2 —
  `settings_version == 2`, `verbose_logging` renamed to `logging_enabled`,
  every other key preserved. Compared against the fixture's `.expected.json`.
- **v2 fixture** (`fixtures/settings/v2_defaults.json`): a current-schema file
  (a snapshot of the defaults exactly as the app writes them) must be left
  **byte-identical** — the app must not rewrite it.
- **Corrupt JSON**: a truncated file is backed up to
  `settings.json.corrupt-<timestamp>.bak` (content verified) and replaced with
  fresh, valid, schema-current default settings; exit code stays 0.
- **Wrong types**: valid JSON with wrong-typed values
  (`"start_on_boot": "yes"`) must not crash the process. Note the app only
  fills in *missing* keys — it does not re-type existing ones — so the bogus
  value is preserved; the test asserts exit 0, valid JSON, stamped
  `settings_version`, and defaults applied for missing keys.

The exe is resolved from `AGENTREDACTOR_EXE`, falling back to
`AgentRedactor/build/x64/Release/AgentRedactor.exe`. The module skips with a
clear message when no exe is found. Run it with:

```powershell
cd tests
pytest -v migration/test_settings_migration.py
# or against a specific build:
$env:AGENTREDACTOR_EXE = "C:\path\to\AgentRedactor.exe"
pytest -v migration/test_settings_migration.py
```

### `test_selfrelease_upgrade.py` (marker: `upgrade`)

The full self-release upgrade E2E:

1. Silent-installs the **previous** release (`*-Setup.exe --silent`) and
   verifies the Velopack layout under `%LOCALAPPDATA%\AgentRedactor\`
   (`Update.exe` at the root, app under `current\`).
2. Seeds a settings.json in a temp config dir.
3. Serves `AGENTREDACTOR_FEED_DIR` (the vNext `vpk pack` output:
   `releases.win.json` + `*-full.nupkg`) over `python -m http.server` on an
   ephemeral localhost port — the feed override requires HTTP/HTTPS.
4. Launches the installed app with `AGENTREDACTOR_UPDATE_FEED=http://127.0.0.1:<port>`
   and `AGENTREDACTOR_UPDATE_AUTOAPPLY=1` (apply + restart without prompting).
5. Polls (up to 5 min) until `current\AgentRedactor.exe`'s file version
   equals the expected vNext version.
6. Runs `--selftest-migrate-settings` on the upgraded install and asserts the
   seeded settings survived (`settings_version == 2`, values preserved, no
   `verbose_logging`).

It is **opt-in** and skipped unless `AGENTREDACTOR_UPGRADE_TEST=1`:

```powershell
$env:AGENTREDACTOR_UPGRADE_TEST = "1"
$env:AGENTREDACTOR_PREV_SETUP = "C:\dl\AgentRedactor-1.0.0-Setup.exe"   # previous release
$env:AGENTREDACTOR_FEED_DIR = "...\AgentRedactor\build\velopack"        # vNext vpk output
$env:AGENTREDACTOR_EXPECT_VERSION = "1.1.0"   # optional; default = newest in feed
cd tests
pytest -v migration/test_selfrelease_upgrade.py
```

Caveats for local runs:

- The test installs into `%LOCALAPPDATA%\AgentRedactor` and kills app
  processes running from there — don't run it on a machine where you use the
  self-release install for real. It does not uninstall afterwards.
- The env-var contract (`PREV_SETUP` path + `FEED_DIR` folder) is deliberately
  source-agnostic, so CI can point it at artifacts downloaded from GitHub
  releases just as well as at local build output.
- The previous version's app may start its first-run model download in the
  background (~1.6 GB); it does not block the update path, but on a metered
  connection cancel the test after the upgrade completes.

## Adding a settings fixture when the schema changes

Follow `AgentRedactor/AGENTS.md`, section **"Changing the settings schema"**:

1. Bump `SETTINGS_SCHEMA_VERSION` and add the `MigrateNToN+1` step.
2. Add a pre-migration `settings.json` fixture here as
   `fixtures/settings/v<N>_<short_description>.json`, plus its expected
   post-migration form as `v<N>_<short_description>.expected.json`. The
   parametrized test in `test_settings_migration.py` picks up every
   `v1_*`/`v<N>_*` pair automatically.
3. Refresh the current-defaults snapshot (`v2_defaults.json`, renamed to the
   new schema version) — `scripts/new-release.ps1` regenerates a per-version
   snapshot (`v<Version>.json`) automatically when a Release exe is present.

Fixtures are plain JSON; comparisons are semantic (parsed), except the
current-schema fixture which is also compared byte-for-byte.

## CI wiring

- `release-selfrelease.yml` runs `test_settings_migration.py` against the
  just-built self-release exe, then — when a previous published release with a
  `*-Setup.exe` asset exists — the upgrade E2E, both **before** `vpk upload`
  publishes. A failed E2E blocks the publish; the first-ever self-release
  skips the E2E with a warning.
- `tests.yml` runs `test_settings_migration.py` against the
  `AgentRedactor-release-bin` (Store-channel) artifact, since the migration
  hook exists in both channels.
