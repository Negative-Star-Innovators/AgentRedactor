# Agent Redactor — Agent Notes

This file is a quick reference for working on the Agent Redactor WinUI 3 C++ project.

## Project Layout

- `AgentRedactor.vcxproj` — Main MSBuild project (WinUI 3 / C++/WinRT).
- `src/` / `include/` — Core C++ sources (proxy, PII detector, settings, tray icon, etc.).
- `HomePage.xaml` / `MainWindow.xaml` — WinUI 3 UI.
- `buildquick.ps1` — Fast local build (EXE + copy models/resources).
- `build.ps1` — Full release build (MSI + MSIX packaging; much slower).

## Quick Build (for testing)

Run from the `AgentRedactor` folder:

```powershell
.\buildquick.ps1
```

This will:
1. Stop any running `AgentRedactor.exe`.
2. Find MSBuild (BuildTools / Enterprise / Professional).
3. Build `Release|x64` (change with `-Configuration Debug` if needed).
4. Copy the `models\` folder and icon resources to `build\x64\Release\`.
5. Produce `build\x64\Release\AgentRedactor.exe`.

The EXE can be run directly from that folder for rapid iteration.

## Full Release Build (MSI + MSIX)

Run from the `AgentRedactor` folder when you need installer packages:

```powershell
.\build.ps1
```

This performs a release build and then packages the output into:
- `build\AgentRedactor.msix`
- `build\msi\AgentRedactor.msi`

The script also:
- Generates a multilingual `resources.pri` from all `Strings\<lang>\Resources.resw`
  files using `makepri.exe`.
- Builds a multilingual MSI with embedded language transforms for every supported
  language.

This script downloads NuGet and WiX v3.14 if they are not present, so the first run may take a while.

## Common Files to Know

| Area | Key files |
|------|-----------|
| Startup / tray-only launch | `App.cpp`, `include/constants.h` (`RegisterStartupTask`) |
| Main window sizing | `MainWindow.cpp` |
| Home page UI layout | `HomePage.xaml`, `HomePage.cpp` |
| Settings / start-on-boot | `SettingsPage.xaml`, `SettingsPage.cpp`, `AppState.cpp` |
| Proxy engine | `src/proxy_engine.cpp`, `src/http_server.cpp` |
| Settings persistence | `src/settings_manager.cpp` |

## Build Requirements

- Windows 10/11
- Visual Studio 2022 Build Tools (or higher) with the **Desktop development with C++** workload
- Windows SDK 10.0.17763.0 or later (the SDK's `makepri.exe`, `MakeAppx.exe`, `wilangid.vbs`, and `MsiTran.exe` are used for packaging)
- NuGet packages are already checked in under `packages\`; MSBuild will use them directly

## End-to-End Tests

Testing is done through the GUI E2E suite in `tests/gui/`. It drives the **real** `AgentRedactor.exe` Windows UI with a FlaUI/C# helper and verifies proxy behaviour against an `aiohttp` mock LLM.

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
- `tests/gui/gui_process.py` — starts/stops `AgentRedactor.exe` using the real `%APPDATA%` / `%LOCALAPPDATA%` directories.
- `tests/gui/conftest.py` — backs up the user's real `AgentRedactor` data, creates a clean test profile, and restores the original data after the test.
- `tests/gui/test_gui_keyword_redaction.py` — focused keyword scenarios: case-insensitive/case-sensitive matching, deletion, toggling case sensitivity, modifying keyword text, statistics/session redactions, and log output.
- `tests/gui/test_gui_regex_redaction.py` — focused regex scenarios: adding, deleting, modifying, enabling/disabling, statistics/session redactions, and log output.
- `tests/gui/test_gui_pii_controls.py` — toggles PII types through the Home page grid and asserts person/email/disabled/toggle behaviour.
- `tests/gui/test_gui_statistics.py` — verifies request counts and per-session redaction labels are surfaced in the UI.
- `tests/gui/test_gui_protocol_modes.py` — **removed/deprecated**: protocol-mode translation is no longer available.
- `tests/gui/test_gui_profiles.py` — adds a second profile through the UI.
- `tests/gui/test_gui_verbose_logging.py` — toggles verbose logging through the UI.

### Build the helper

```powershell
cd tests\gui\windows
.\build.ps1
```

This uses the existing `AgentRedactor\build\tools\nuget.exe` to restore `FlaUI.Core`, `FlaUI.UIA3`, and `Microsoft.NETFramework.ReferenceAssemblies.net48`, then compiles with MSBuild.

### Run the GUI tests

```powershell
cd tests
pytest -v gui/
```

Each test:
1. Backs up `%APPDATA%\AgentRedactor` and `%LOCALAPPDATA%\AgentRedactor`.
2. Writes a clean test `settings.json` into `%APPDATA%\AgentRedactor`.
3. Starts the normal `AgentRedactor.exe` window.
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

- The WinUI 3 project defines `DisableXamlGeneratedMain=true` and uses a custom `wWinMain` in `App.cpp`.
- The old Win32 UI in `src/app.cpp` is **not** compiled by `AgentRedactor.vcxproj`; it is left in the repo for reference.
- When changing XAML, MSBuild will regenerate the `.xaml.g.hpp` files automatically.
- Localization uses `.resw` files under `Strings\`. `EnablePriGenTooling` and `EnableCoreMrtTooling` are disabled in the `.vcxproj` because the BuildTools edition does not include the AppxPackage MSBuild targets; `build.ps1` and `buildquick.ps1` generate the PRI files manually with `makepri.exe`.
- The build scripts configure `makepri.exe` with default qualifiers for all supported
  languages so every translation is included in the generated `resources.pri`.
- At runtime the app applies the saved language by setting the MRT `ResourceContext` `Language` qualifier (`Windows.ApplicationModel.Resources.Core.ResourceManager.Current.DefaultContext().QualifierValues()["Language"]`); this is the supported mechanism for unpackaged desktop apps. `PrimaryLanguageOverride` is also attempted but may be ignored on unpackaged apps.
- Supported languages are declared once in `include/constants.h` (`SUPPORTED_LANGUAGES`).
  See `docs/languages.md` for the full list, RTL/LTR direction, and script families.
  The Settings language `ComboBox` and the tray Language submenu are built dynamically
  from this list, so adding a new language only requires a new `Strings/<tag>/Resources.resw`
  file, a matching `packaging/AgentRedactor.<tag>.wxl`, and an entry in `SUPPORTED_LANGUAGES`.
- The tray Language submenu shows a checkmark next to the active language and automatically restarts the app when a different language is selected.
- Right-to-left (RTL) layout is supported for Arabic (`ar-SA`), Hebrew (`he-IL`), and Urdu (`ur-PK`). The app sets `FlowDirection="RightToLeft"` on the main window frame and on each page when the current language is RTL.
- To regenerate all `.resw` and `.wxl` files (including the existing German file), run:
  ```powershell
  python AgentRedactor/generate_localizations.py
  ```
- A helper script, `AgentRedactor/generate_new_languages.py`, can translate the English `.resw` strings into new languages using Google Translate via `deep-translator`. It is intended for bootstrapping translations and should be reviewed by native speakers before release.
