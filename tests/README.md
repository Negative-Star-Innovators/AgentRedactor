# AgentRedactor GUI End-to-End Tests

These tests drive the **real** `AgentRedactorUI.exe` Windows UI with a FlaUI/C# helper and verify proxy behaviour through an `aiohttp` mock LLM.

## Prerequisites

1. Build a Release binary:
   ```powershell
   cd ..\AgentRedactor
   .\buildquick.ps1 -Configuration Release
   ```
   On ARM64 Windows, add `-Platform ARM64` (the tests pick the exe from
   `build\x64\Release` or `build\ARM64\Release` automatically based on the
   host architecture).

2. Build the FlaUI helper:
   ```powershell
   cd tests\gui\windows
   .\build.ps1
   ```

3. Install Python dependencies:
   ```powershell
   cd tests
   pip install -r requirements.txt
   ```

## Running tests

```powershell
cd tests
pytest -v gui/
```

## What is tested

- **Keyword redaction**: add/delete/modify/toggle case sensitivity, case-insensitive and case-sensitive matching for `Project Chimera`
- **Regex redaction**: add/delete/modify/toggle enabled for patterns such as `\bPN-\d{5}\b`
- **PII redaction**: person/email/all-categories redaction, master-switch disable, all-categories unselected, enable/disable/toggle via the UI, and statistics/logs verification
- **Statistics and session redactions**: request counts and per-session redaction labels
- **Profiles**: adding a second profile
- **Logging**: Enable logging toggle (persists across restarts and disables the Show sensitive checkbox when off), API key redaction in both log files, Show sensitive information (confirmation dialog, raw values logged only while on, resets on restart), no traffic written when logging is disabled, metadata-only client boundaries in the debug log, and migration of the legacy `verbose_logging` setting

## Architecture

- `gui/windows/FlaUIHelper/` — C# console app that automates the AgentRedactor window.
- `gui/windows/gui_driver.py` — Python wrapper around `FlaUIHelper.exe`.
- `gui/gui_process.py` — starts/stops `AgentRedactorUI.exe` (plus the `agentredactor.exe` engine) using real `%APPDATA%` / `%LOCALAPPDATA%`.
- `gui/conftest.py` — backs up the user's real data, creates a clean test profile, and restores it after the test.
- `config_factory.py` generates a plaintext `settings.json`.
- `mock_llm.py` is an `aiohttp` mock server that echoes redacted text back in OpenAI or Anthropic format.

## Headless CLI and Linux engine tests

`cli/`, `migration/`, and `linux/` drive the headless `agentredactor`
binary (no GUI) and also run on Linux, where the binary comes from
`linux/build/engine/agentredactor` (override with `AGENTREDACTOR_ENGINE_BIN`
for `cli/`, `AGENTREDACTOR_EXE` for `migration/`). The `gui/` directory is
ignored on non-Windows.

```bash
cd tests
pytest -v cli/
pytest -v migration/test_settings_migration.py
pytest -v linux/
```

On Linux the protection model is a typed master password instead of Windows
Hello: the Hello-consent tests in `cli/test_cli.py` skip, and
`cli/test_cli_linux_password.py` covers the equivalent password flow by
piping the password to stdin.

Run each suite in its own pytest process (as above): the suites share the
`conftest` module name and cannot be collected in a single invocation.
