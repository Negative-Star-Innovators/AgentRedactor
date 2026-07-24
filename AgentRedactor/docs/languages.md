# Supported Languages

Agent Redactor ships with UI translations for **53 languages**. The source of truth for the language list is `include/constants.h` (`SUPPORTED_LANGUAGES`).

The Settings page language `ComboBox` and the system-tray **Language** submenu are built dynamically from this list.

## Language list

| Tag | Native name | Direction | Script family |
|-----|-------------|-----------|---------------|
| `af-ZA` | Afrikaans | LTR | Latin |
| `ar-SA` | العربية | **RTL** | Arabic |
| `az-Latn-AZ` | Azərbaycan dili | LTR | Latin |
| `bg-BG` | Български | LTR | Cyrillic |
| `cs-CZ` | Čeština | LTR | Latin |
| `da-DK` | Dansk | LTR | Latin |
| `de-DE` | Deutsch | LTR | Latin |
| `el-GR` | Ελληνικά | LTR | Greek |
| `en-US` | English | LTR | Latin |
| `es-ES` | Español | LTR | Latin |
| `et-EE` | Eesti | LTR | Latin |
| `fil-PH` | Filipino | LTR | Latin |
| `fi-FI` | Suomi | LTR | Latin |
| `fr-FR` | Français | LTR | Latin |
| `ha-Latn-NG` | Hausa | LTR | Latin |
| `he-IL` | עברית | **RTL** | Hebrew |
| `hi-IN` | हिन्दी | LTR | Devanagari |
| `hr-HR` | Hrvatski | LTR | Latin |
| `hu-HU` | Magyar | LTR | Latin |
| `hy-AM` | Հայերեն | LTR | Armenian |
| `id-ID` | Bahasa Indonesia | LTR | Latin |
| `ig-NG` | Igbo | LTR | Latin |
| `is-IS` | Íslenska | LTR | Latin |
| `it-IT` | Italiano | LTR | Latin |
| `ja-JP` | 日本語 | LTR | Japanese (CJK) |
| `ka-GE` | ქართული | LTR | Georgian |
| `kk-KZ` | Қазақ тілі | LTR | Cyrillic |
| `ko-KR` | 한국어 | LTR | Korean (Hangul) |
| `lb-LU` | Lëtzebuergesch | LTR | Latin |
| `lt-LT` | Lietuvių | LTR | Latin |
| `lv-LV` | Latviešu | LTR | Latin |
| `ms-MY` | Bahasa Melayu | LTR | Latin |
| `mt-MT` | Malti | LTR | Latin |
| `nb-NO` | Norsk bokmål | LTR | Latin |
| `nl-NL` | Nederlands | LTR | Latin |
| `pl-PL` | Polski | LTR | Latin |
| `pt-PT` | Português | LTR | Latin |
| `ro-RO` | Română | LTR | Latin |
| `ru-RU` | Русский | LTR | Cyrillic |
| `sk-SK` | Slovenčina | LTR | Latin |
| `sl-SI` | Slovenščina | LTR | Latin |
| `sr-Latn-RS` | Srpski | LTR | Latin |
| `sv-SE` | Svenska | LTR | Latin |
| `sw-KE` | Kiswahili | LTR | Latin |
| `ta-IN` | தமிழ் | LTR | Tamil |
| `th-TH` | ไทย | LTR | Thai |
| `tr-TR` | Türkçe | LTR | Latin |
| `uk-UA` | Українська | LTR | Cyrillic |
| `ur-PK` | اردو | **RTL** | Arabic |
| `vi-VN` | Tiếng Việt | LTR | Latin |
| `zh-CN` | 简体中文 | LTR | Chinese (Simplified) |
| `zh-TW` | 繁體中文 | LTR | Chinese (Traditional) |

## Right-to-left (RTL) languages

Only three supported languages are laid out right-to-left:

| Tag | Native name |
|-----|-------------|
| `ar-SA` | العربية |
| `he-IL` | עברית |
| `ur-PK` | اردو |

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
