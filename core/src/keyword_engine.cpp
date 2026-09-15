#include "keyword_engine.h"
#include "utils.h"
#include <algorithm>
#include <cwctype>

namespace AgentRedactor {

namespace {

// Same case-folding comparison as Utils::ReplaceAllCaseInsensitive, so the
// presence check agrees exactly with the replacement that follows it.
bool ContainsKeyword(const std::wstring& text, const std::wstring& needle, bool caseSensitive) {
    if (caseSensitive) return text.find(needle) != std::wstring::npos;
    return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
        [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); }) != text.end();
}

} // namespace

KeywordEngine::KeywordEngine() {
}

void KeywordEngine::SetKeywords(const std::vector<KeywordEntry>& keywords) {
    keywords_.clear();
    for (const auto& k : keywords) {
        if (!k.enabled) continue;
        std::wstring trimmed = Utils::Trim(k.text);
        if (!trimmed.empty()) {
            KeywordEntry entry = k;
            entry.text = trimmed;
            keywords_.push_back(entry);
        }
    }
    std::sort(keywords_.begin(), keywords_.end(), [](const KeywordEntry& a, const KeywordEntry& b) {
        return a.text.length() > b.text.length();
    });
}

std::pair<std::wstring, std::map<std::wstring, std::wstring>> KeywordEngine::Redact(const std::wstring& text, int startCounter) {
    std::map<std::wstring, std::wstring> labelMap;
    if (keywords_.empty()) return {text, labelMap};

    std::wstring result = text;
    int counter = startCounter;
    for (const auto& entry : keywords_) {
        // Only assign a label when the keyword actually occurs — otherwise
        // every configured keyword would be counted as a "match" per request.
        if (!ContainsKeyword(result, entry.text, entry.caseSensitive)) continue;
        std::wstring label = L"<<REDACTED_KEYWORD_" + std::to_wstring(counter++) + L">>";
        labelMap[label] = entry.text;
        if (entry.caseSensitive) {
            result = Utils::ReplaceAll(result, entry.text, label);
        } else {
            result = Utils::ReplaceAllCaseInsensitive(result, entry.text, label);
        }
    }
    return {result, labelMap};
}

std::pair<std::wstring, std::map<std::wstring, std::wstring>> KeywordEngine::Redact(const std::wstring& text,
    std::map<std::wstring, std::wstring>& sessionKeywordToLabel, int& sessionCounter,
    std::map<std::wstring, std::wstring>& requestLabelMap) {
    std::map<std::wstring, std::wstring> newLabels;
    if (keywords_.empty()) return {text, newLabels};

    std::wstring result = text;
    for (const auto& entry : keywords_) {
        // Skip keywords absent from the text: pre-assigning labels for them
        // inflates the keyword-match stats with matches that never happened.
        if (!ContainsKeyword(result, entry.text, entry.caseSensitive)) continue;
        std::wstring label;
        auto it = sessionKeywordToLabel.find(entry.text);
        if (it != sessionKeywordToLabel.end()) {
            label = it->second;
        } else {
            label = L"<<REDACTED_KEYWORD_" + std::to_wstring(sessionCounter++) + L">>";
            sessionKeywordToLabel[entry.text] = label;
            newLabels[label] = entry.text;
        }
        requestLabelMap[label] = entry.text;
        if (entry.caseSensitive) {
            result = Utils::ReplaceAll(result, entry.text, label);
        } else {
            result = Utils::ReplaceAllCaseInsensitive(result, entry.text, label);
        }
    }
    return {result, newLabels};
}

std::wstring KeywordEngine::Unredact(const std::wstring& text, const std::map<std::wstring, std::wstring>& labelMap) {
    std::wstring result = text;
    for (const auto& [label, original] : labelMap) {
        result = Utils::ReplaceAll(result, label, original);
    }
    return result;
}

} // namespace AgentRedactor
