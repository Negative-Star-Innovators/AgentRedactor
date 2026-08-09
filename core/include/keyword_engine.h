#pragma once

#include <string>
#include <vector>
#include <map>
#include "api_key_profile.h"

namespace AgentRedactor {

class KeywordEngine {
public:
    KeywordEngine();
    void SetKeywords(const std::vector<KeywordEntry>& keywords);
    std::pair<std::wstring, std::map<std::wstring, std::wstring>> Redact(const std::wstring& text, int startCounter = 0);
    std::pair<std::wstring, std::map<std::wstring, std::wstring>> Redact(const std::wstring& text,
        std::map<std::wstring, std::wstring>& sessionKeywordToLabel, int& sessionCounter,
        std::map<std::wstring, std::wstring>& requestLabelMap);
    std::wstring Unredact(const std::wstring& text, const std::map<std::wstring, std::wstring>& labelMap);

private:
    std::vector<KeywordEntry> keywords_;
};

} // namespace AgentRedactor
