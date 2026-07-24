#include "regex_engine.h"
#include "utils.h"
#include <algorithm>
#include <set>

namespace AgentRedactor {

RegexEngine::RegexEngine() {
}

void RegexEngine::SetPatterns(const std::vector<RegexEntry>& patterns) {
    rawPatterns_.clear();
    compiledPatterns_.clear();
    for (const auto& p : patterns) {
        if (!p.enabled) continue;
        std::wstring trimmed = Utils::Trim(p.pattern);
        if (trimmed.empty()) continue;
        try {
            compiledPatterns_.emplace_back(trimmed, std::regex_constants::ECMAScript | std::regex_constants::icase);
            rawPatterns_.push_back(trimmed);
        } catch (const std::regex_error&) {
            // Invalid regex, skip
        }
    }
}

std::pair<std::wstring, std::map<std::wstring, std::wstring>> RegexEngine::Redact(const std::wstring& text, int startCounter) {
    std::map<std::wstring, std::wstring> labelMap;
    if (compiledPatterns_.empty()) return {text, labelMap};

    std::set<std::wstring> seen;
    for (const auto& re : compiledPatterns_) {
        std::wsregex_iterator it(text.begin(), text.end(), re);
        std::wsregex_iterator end;
        for (; it != end; ++it) {
            seen.insert(it->str());
        }
    }

    std::vector<std::wstring> sorted(seen.begin(), seen.end());
    std::sort(sorted.begin(), sorted.end(), [](const std::wstring& a, const std::wstring& b) {
        return a.length() > b.length();
    });

    std::wstring result = text;
    int counter = startCounter;
    for (const auto& original : sorted) {
        std::wstring label = L"<<REDACTED_REGEX_" + std::to_wstring(counter++) + L">>";
        labelMap[label] = original;
        result = Utils::ReplaceAll(result, original, label);
    }
    return {result, labelMap};
}

std::pair<std::wstring, std::map<std::wstring, std::wstring>> RegexEngine::Redact(const std::wstring& text,
    std::map<std::wstring, std::wstring>& sessionMatchToLabel, int& sessionCounter,
    std::map<std::wstring, std::wstring>& requestLabelMap) {
    std::map<std::wstring, std::wstring> newLabels;
    if (compiledPatterns_.empty()) return {text, newLabels};

    std::set<std::wstring> seen;
    for (const auto& re : compiledPatterns_) {
        std::wsregex_iterator it(text.begin(), text.end(), re);
        std::wsregex_iterator end;
        for (; it != end; ++it) {
            seen.insert(it->str());
        }
    }

    std::vector<std::wstring> sorted(seen.begin(), seen.end());
    std::sort(sorted.begin(), sorted.end(), [](const std::wstring& a, const std::wstring& b) {
        return a.length() > b.length();
    });

    std::wstring result = text;
    for (const auto& original : sorted) {
        std::wstring label;
        auto it = sessionMatchToLabel.find(original);
        if (it != sessionMatchToLabel.end()) {
            label = it->second;
        } else {
            label = L"<<REDACTED_REGEX_" + std::to_wstring(sessionCounter++) + L">>";
            sessionMatchToLabel[original] = label;
            newLabels[label] = original;
        }
        requestLabelMap[label] = original;
        result = Utils::ReplaceAll(result, original, label);
    }
    return {result, newLabels};
}

std::wstring RegexEngine::Unredact(const std::wstring& text, const std::map<std::wstring, std::wstring>& labelMap) {
    std::wstring result = text;
    for (const auto& [label, original] : labelMap) {
        result = Utils::ReplaceAll(result, label, original);
    }
    return result;
}

} // namespace AgentRedactor
