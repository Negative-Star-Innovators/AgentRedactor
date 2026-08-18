// Engine-side shim for LocString/LocFormat. core/localization.cpp is bound
// to the GUI process (WinUI pch, AppState, MRT resource contexts), so the
// engine cannot link it. proxy_engine.cpp only needs a handful of keys for
// log summaries and session-match types; this shim serves the English values
// and substitutes {0}/{1}/... placeholders exactly like LocFormat.
//
// Known Stage A limitation: engine-generated log/match-type strings are
// English-only. Full MRT localization inside the engine process is follow-up
// work (the GUI itself is fully localized as before).
#include "localization.h"
#include <unordered_map>

namespace AgentRedactor {

void InitializeLocalization() {}

namespace {
const std::unordered_map<std::wstring_view, std::wstring_view>& EngineStrings() {
    static const std::unordered_map<std::wstring_view, std::wstring_view> strings = {
        { L"MatchType_PII", L"PII" },
        { L"MatchType_Regex", L"Regex" },
        { L"MatchType_Keyword", L"Keyword" },
        { L"ProxyLog_Request", L"Request: {0} {1}" },
        { L"ProxyLog_PII", L"PII: {0}" },
        { L"ProxyLog_Regex", L"Regex: {0}" },
        { L"ProxyLog_Keywords", L"Keywords: {0}" },
    };
    return strings;
}
} // namespace

std::wstring LocString(std::wstring_view key) {
    const auto& strings = EngineStrings();
    auto it = strings.find(key);
    if (it != strings.end()) return std::wstring(it->second);
    return std::wstring(key);
}

std::wstring LocFormat(std::wstring_view key, std::initializer_list<std::wstring_view> args) {
    std::wstring value = LocString(key);
    size_t index = 0;
    for (const auto& arg : args) {
        std::wstring placeholder = L"{" + std::to_wstring(index) + L"}";
        size_t pos = 0;
        while ((pos = value.find(placeholder, pos)) != std::wstring::npos) {
            value.replace(pos, placeholder.length(), arg.data(), arg.length());
            pos += arg.length();
        }
        ++index;
    }
    return value;
}

bool SetLanguageOverride(const std::wstring&) { return true; }
std::wstring GetCurrentLanguage() { return L"en"; }
std::wstring GetLanguageOverride() { return L""; }
bool IsCurrentLanguageRtl() { return false; }

} // namespace AgentRedactor
