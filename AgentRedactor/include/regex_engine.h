#pragma once

#include <string>
#include <vector>
#include <map>
#include <regex>
#include "api_key_profile.h"

namespace AgentRedactor {

class RegexEngine {
public:
    RegexEngine();
    void SetPatterns(const std::vector<RegexEntry>& patterns);
    std::pair<std::wstring, std::map<std::wstring, std::wstring>> Redact(const std::wstring& text, int startCounter = 0);
    std::pair<std::wstring, std::map<std::wstring, std::wstring>> Redact(const std::wstring& text,
        std::map<std::wstring, std::wstring>& sessionMatchToLabel, int& sessionCounter,
        std::map<std::wstring, std::wstring>& requestLabelMap);
    std::wstring Unredact(const std::wstring& text, const std::map<std::wstring, std::wstring>& labelMap);

private:
    std::vector<std::wstring> rawPatterns_;
    std::vector<std::wregex> compiledPatterns_;
};

} // namespace AgentRedactor
