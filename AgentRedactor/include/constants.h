#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <windows.h>

namespace AgentRedactor {

constexpr const wchar_t* APP_NAME = L"Agent Redactor";
constexpr const wchar_t* APP_VERSION = L"1.0.0";
constexpr UINT WM_TRAYICON = WM_USER + 1;

constexpr size_t MAX_TOKENS_PER_CHUNK = 128000;
constexpr size_t TOKEN_OVERLAP = 128;

constexpr const wchar_t* MODEL_DIR = L"models";
constexpr const wchar_t* MODEL_FILENAME = L"model_quantized.onnx";
constexpr const wchar_t* SETTINGS_FILE = L"settings.json";

inline const std::vector<std::wstring> DEFAULT_PII_TYPES = {
    L"account_number",
    L"private_address",
    L"private_date",
    L"private_email",
    L"private_person",
    L"private_phone",
    L"private_url",
    L"secret",
};

struct PIICategory {
    std::wstring id;
    std::wstring label;
    std::vector<std::wstring> types;
};

inline const std::vector<PIICategory> PII_CATEGORIES = {
    {L"FINANCIAL", L"Financial", {L"account_number"}},
    {L"CONTACT", L"Contact & Location", {L"private_address", L"private_email", L"private_phone"}},
    {L"PERSONAL", L"Personal", {L"private_person", L"private_date"}},
    {L"DIGITAL", L"Digital & Secrets", {L"private_url", L"secret"}},
};

enum MenuIDs : UINT {
    ID_TRAY_OPEN = 1001,
    ID_TRAY_LANGUAGE_FIRST = 2000,
    ID_TRAY_START_ON_BOOT = 3001,
    ID_TRAY_QUIT,
};

struct SupportedLanguage {
    std::wstring tag;
    std::wstring nativeName; // Display name in the language itself (e.g. L"Español").
};

// Supported UI languages (displayed in Settings and the tray menu).
// Keep this list in sync with the Strings/<tag>/Resources.resw files.
inline const std::vector<SupportedLanguage> SUPPORTED_LANGUAGES = {
    {L"en", L"English"},
    {L"de", L"Deutsch"},
    {L"es", L"Espa\u00F1ol"},
    {L"fr", L"Fran\u00E7ais"},
    {L"pt", L"Portugu\u00EAs"},
    {L"it", L"Italiano"},
    {L"da", L"Dansk"},
    {L"nl", L"Nederlands"},
    {L"sv", L"Svenska"},
    {L"lb", L"L\u00EBtzebuergesch"},
    {L"nb", L"Norsk bokm\u00E5l"},
    {L"fi", L"Suomi"},
    {L"ru", L"\u0420\u0443\u0441\u0441\u043A\u0438\u0439"},
    {L"hr", L"Hrvatski"},
    {L"el", L"\u0395\u03BB\u03BB\u03B7\u03BD\u03B9\u03BA\u03AC"},
    {L"sl", L"Sloven\u0161\u010Dina"},
    {L"sr-Latn", L"Srpski"},
    {L"uk", L"\u0423\u043A\u0440\u0430\u0457\u043D\u0441\u044C\u043A\u0430"},
    {L"sq", L"Shqip"},
    {L"lv", L"Latvie\u0161u"},
    {L"hy", L"\u0540\u0561\u0575\u0565\u0580\u0565\u0576"},
    {L"cs", L"\u010Ce\u0161tina"},
    {L"et", L"Eesti"},
    {L"sk", L"Sloven\u010Dina"},
    {L"bg", L"\u0411\u044A\u043B\u0433\u0430\u0440\u0441\u043A\u0438"},
    {L"ka", L"\u10E5\u10D0\u10E0\u10D7\u10E3\u10DA\u10D8"},
    {L"hu", L"Magyar"},
    {L"pl", L"Polski"},
    {L"ro", L"Rom\u00E2n\u0103"},
    {L"lt", L"Lietuvi\u0173"},
    {L"is", L"\u00CDslenska"},
    {L"zh-CN", L"\u7B80\u4F53\u4E2D\u6587"},
    {L"zh-TW", L"\u7E41\u9AD4\u4E2D\u6587"},
    {L"ja", L"\u65E5\u672C\u8A9E"},
    {L"ko", L"\uD55C\uAD6D\uC5B4"},
    {L"mt", L"Malti"},
    {L"hi", L"\u0939\u093F\u0928\u094D\u0926\u0940"},
    {L"ta", L"\u0BA4\u0BAE\u0BBF\u0BB4\u0BCD"},
    {L"vi", L"Ti\u1EBFng Vi\u1EC7t"},
    {L"sw", L"Kiswahili"},
    {L"af", L"Afrikaans"},
    {L"he", L"\u05E2\u05D1\u05E8\u05D9\u05EA"},
    {L"id", L"Bahasa Indonesia"},
    {L"fil", L"Filipino"},
    {L"ig-NG", L"Igbo"},
    {L"th", L"\u0E44\u0E17\u0E22"},
    {L"tr", L"T\u00FCrk\u00E7e"},
    {L"ur", L"\u0627\u0631\u062F\u0648"},
    {L"ar", L"\u0627\u0644\u0639\u0631\u0628\u064A\u0629"},
    {L"ms", L"Bahasa Melayu"},
    {L"az-Latn", L"Az\u0259rbaycan dili"},
    {L"kk", L"\u049A\u0430\u0437\u0430\u049B \u0442\u0456\u043B\u0456"},
    {L"ha-Latn", L"Hausa"},
};

// Right-to-left language tags (primary subtag matching).
inline bool IsLanguageRtl(const std::wstring& languageTag) {
    static const std::vector<std::wstring> rtlPrefixes = {
        L"ar", L"he", L"ur", L"fa", L"ps", L"sd", L"ug", L"dv", L"yi"
    };
    for (const auto& prefix : rtlPrefixes) {
        if (languageTag == prefix) return true;
        if (languageTag.size() > prefix.size() &&
            languageTag.substr(0, prefix.size()) == prefix &&
            languageTag[prefix.size()] == L'-') {
            return true;
        }
    }
    return false;
}

// Returns true if `current` matches `supportedTag` exactly or when one tag is
// a more specific variant of the other (e.g. "de-DE" matches "de", and
// "de" matches "de-DE").
inline bool LanguageMatches(const std::wstring& current, const std::wstring& supportedTag) {
    if (current == supportedTag) return true;
    if (current.size() > supportedTag.size() &&
        current.compare(0, supportedTag.size(), supportedTag) == 0 &&
        current[supportedTag.size()] == L'-') return true;
    if (supportedTag.size() > current.size() &&
        supportedTag.compare(0, current.size(), current) == 0 &&
        supportedTag[current.size()] == L'-') return true;
    return false;
}

} // namespace AgentRedactor

inline void RegisterStartupTask() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring command = std::wstring(L"\"") + path + L"\" --tray-only";
        RegSetValueExW(hKey, AgentRedactor::APP_NAME, 0, REG_SZ, (BYTE*)command.c_str(), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

inline void UnregisterStartupTask() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, AgentRedactor::APP_NAME);
        RegCloseKey(hKey);
    }
}
