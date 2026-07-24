# Supported Languages

Agent Redactor ships with UI translations for **53 languages**. The source of truth for the language list is `include/constants.h` (`SUPPORTED_LANGUAGES`).

The Settings page language `ComboBox` and the system-tray **Language** submenu are built dynamically from this list.

## Language list

| Tag | Native name | Direction | Script family |
|-----|-------------|-----------|---------------|
| `af` | Afrikaans | LTR | Latin |
| `ar` | العربية | **RTL** | Arabic |
| `az-Latn` | Azərbaycan dili | LTR | Latin |
| `bg` | Български | LTR | Cyrillic |
| `cs` | Čeština | LTR | Latin |
| `da` | Dansk | LTR | Latin |
| `de` | Deutsch | LTR | Latin |
| `el` | Ελληνικά | LTR | Greek |
| `en` | English | LTR | Latin |
| `es` | Español | LTR | Latin |
| `et` | Eesti | LTR | Latin |
| `fil` | Filipino | LTR | Latin |
| `fi` | Suomi | LTR | Latin |
| `fr` | Français | LTR | Latin |
| `ha-Latn` | Hausa | LTR | Latin |
| `he` | עברית | **RTL** | Hebrew |
| `hi` | हिन्दी | LTR | Devanagari |
| `hr` | Hrvatski | LTR | Latin |
| `hu` | Magyar | LTR | Latin |
| `hy` | Հայերեն | LTR | Armenian |
| `id` | Bahasa Indonesia | LTR | Latin |
| `ig` | Igbo | LTR | Latin |
| `is` | Íslenska | LTR | Latin |
| `it` | Italiano | LTR | Latin |
| `ja` | 日本語 | LTR | Japanese (CJK) |
| `ka` | ქართული | LTR | Georgian |
| `kk` | Қазақ тілі | LTR | Cyrillic |
| `ko` | 한국어 | LTR | Korean (Hangul) |
| `lb` | Lëtzebuergesch | LTR | Latin |
| `lt` | Lietuvių | LTR | Latin |
| `lv` | Latviešu | LTR | Latin |
| `ms` | Bahasa Melayu | LTR | Latin |
| `mt` | Malti | LTR | Latin |
| `nb` | Norsk bokmål | LTR | Latin |
| `nl` | Nederlands | LTR | Latin |
| `pl` | Polski | LTR | Latin |
| `pt` | Português | LTR | Latin |
| `ro` | Română | LTR | Latin |
| `ru` | Русский | LTR | Cyrillic |
| `sk` | Slovenčina | LTR | Latin |
| `sl` | Slovenščina | LTR | Latin |
| `sr-Latn` | Srpski | LTR | Latin |
| `sv` | Svenska | LTR | Latin |
| `sw` | Kiswahili | LTR | Latin |
| `ta` | தமிழ் | LTR | Tamil |
| `th` | ไทย | LTR | Thai |
| `tr` | Türkçe | LTR | Latin |
| `uk` | Українська | LTR | Cyrillic |
| `ur` | اردو | **RTL** | Arabic |
| `vi` | Tiếng Việt | LTR | Latin |
| `zh-CN` | 简体中文 | LTR | Chinese (Simplified) |
| `zh-TW` | 繁體中文 | LTR | Chinese (Traditional) |

## Right-to-left (RTL) languages

Only three supported languages are laid out right-to-left:

| Tag | Native name |
|-----|-------------|
| `ar` | العربية |
| `he` | עברית |
| `ur` | اردو |

When one of these is selected, the app sets `FlowDirection="RightToLeft"` on the main window frame and on the current page.

The RTL check is performed by `IsLanguageRtl()` in `include/constants.h`. It matches the following primary subtags: `ar`, `he`, `ur`, `fa`, `ps`, `sd`, `ug`, `dv`, `yi`. Among the currently supported languages, only `ar`, `he`, and `ur` are present.

## Left-to-right (LTR) languages

All other supported languages (50 of 53) are left-to-right.

## System default

The Settings language `ComboBox` also offers **System default** (empty tag). When selected, the app does not apply a language override and lets Windows decide the UI language from the user's regional preferences.

## How language is applied at runtime

1. On startup, `AppState::Initialize()` calls `InitializeLocalization()`, which reads the saved `app_language` from `settings.json`.
2. If a non-empty language is saved, the app sets the MRT `ResourceContext` global `Language` qualifier and attempts `ApplicationLanguages::PrimaryLanguageOverride`.
3. The active language is persisted in `settings.json` under the key `app_language`.
4. When the user changes language from the Settings page or the tray menu, the new language is applied in-session; the visible UI is re-localized without restarting the app.

## Adding a new language

To add a new supported language:

1. Add the `{tag, nativeName}` entry to `SUPPORTED_LANGUAGES` in `include/constants.h`.
2. Create `Strings/<tag>/Resources.resw` with all required string resources.
3. Create `packaging/AgentRedactor.<tag>.wxl` for the installer.
4. Add the tag to the `$langs` array in both `build.ps1` and `buildquick.ps1` so `makepri.exe` includes it in `resources.pri`.
5. Update this document.

> `generate_localizations.py` regenerates all `.resw` and `.wxl` files from curated dictionaries. `generate_new_languages.py` can bootstrap a new translation via Google Translate for review.
