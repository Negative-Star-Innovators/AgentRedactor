#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace AgentRedactor {

struct RedactionStats {
    uint64_t totalRequests = 0;
    uint64_t totalPIIDetected = 0;
    uint64_t totalRegexMatches = 0;
    uint64_t totalKeywordMatches = 0;
    std::unordered_map<std::string, uint64_t> piiTypeBreakdown;

    void ToJson(json& j) const;
    static void FromJson(const json& j, RedactionStats& stats);
};

enum class ProtocolMode {
    None
};

struct KeywordEntry {
    std::wstring text;
    bool caseSensitive = true;
    bool enabled = true;
};

struct RegexEntry {
    std::wstring pattern;
    bool enabled = true;
};

struct ApiKeyProfile {
    std::wstring id;
    std::wstring alias;
    std::wstring upstreamUrl;
    std::wstring apiKey;
    int port = 8080;

    bool useOpenAIModel = true;
    ProtocolMode protocolMode = ProtocolMode::None;
    std::vector<std::wstring> enabledPIITypes;
    float piiConfidenceThreshold = 0.9f;

    std::vector<RegexEntry> regexPatterns;
    std::vector<KeywordEntry> keywords;

    RedactionStats stats;
    bool enabled = true;

    ApiKeyProfile();
    explicit ApiKeyProfile(const std::wstring& aliasName);

    void ToJson(json& j) const;
    static ApiKeyProfile FromJson(const json& j);
};

} // namespace AgentRedactor
