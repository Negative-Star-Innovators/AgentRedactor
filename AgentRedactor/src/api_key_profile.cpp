#include "api_key_profile.h"
#include "utils.h"
#include "constants.h"

namespace AgentRedactor {

namespace {

std::string ProtocolModeToString(ProtocolMode mode) {
    (void)mode;
    return "none";
}

ProtocolMode ProtocolModeFromString(const std::string& s) {
    (void)s;
    return ProtocolMode::None;
}

} // namespace

void RedactionStats::ToJson(json& j) const {
    j = json{
        {"total_requests", totalRequests},
        {"total_pii_detected", totalPIIDetected},
        {"total_regex_matches", totalRegexMatches},
        {"total_keyword_matches", totalKeywordMatches},
        {"pii_type_breakdown", piiTypeBreakdown}
    };
}

void RedactionStats::FromJson(const json& j, RedactionStats& stats) {
    if (j.contains("total_requests")) j.at("total_requests").get_to(stats.totalRequests);
    if (j.contains("total_pii_detected")) j.at("total_pii_detected").get_to(stats.totalPIIDetected);
    if (j.contains("total_regex_matches")) j.at("total_regex_matches").get_to(stats.totalRegexMatches);
    if (j.contains("total_keyword_matches")) j.at("total_keyword_matches").get_to(stats.totalKeywordMatches);
    if (j.contains("pii_type_breakdown")) {
        for (auto& [key, value] : j.at("pii_type_breakdown").items()) {
            stats.piiTypeBreakdown[key] = value.get<uint64_t>();
        }
    }
}

ApiKeyProfile::ApiKeyProfile() {
    id = Utils::GenerateUUID();
    enabledPIITypes = DEFAULT_PII_TYPES;
}

ApiKeyProfile::ApiKeyProfile(const std::wstring& aliasName) : ApiKeyProfile() {
    alias = aliasName;
}

void ApiKeyProfile::ToJson(json& j) const {
    std::vector<std::string> utf8Types;
    for (const auto& t : enabledPIITypes) utf8Types.push_back(Utils::WideToUtf8(t));

    json keywordsJson = json::array();
    for (const auto& k : keywords) {
        keywordsJson.push_back(json{
            {"text", Utils::WideToUtf8(k.text)},
            {"case_sensitive", k.caseSensitive},
            {"enabled", k.enabled}
        });
    }

    json regexJson = json::array();
    for (const auto& r : regexPatterns) {
        regexJson.push_back(json{
            {"pattern", Utils::WideToUtf8(r.pattern)},
            {"enabled", r.enabled}
        });
    }

    json statsJson;
    stats.ToJson(statsJson);

    j = json{
        {"id", Utils::WideToUtf8(id)},
        {"alias", Utils::WideToUtf8(alias)},
        {"upstream_url", Utils::WideToUtf8(upstreamUrl)},
        {"api_key", Utils::WideToUtf8(apiKey)},
        {"port", port},
        {"use_openai_model", useOpenAIModel},
        {"protocol_mode", ProtocolModeToString(protocolMode)},
        {"enabled_pii_types", utf8Types},
        {"pii_confidence_threshold", piiConfidenceThreshold},
        {"regex_patterns", regexJson},
        {"keywords", keywordsJson},
        {"stats", statsJson},
        {"enabled", enabled}
    };
}

ApiKeyProfile ApiKeyProfile::FromJson(const json& j) {
    ApiKeyProfile profile;
    if (j.contains("id")) profile.id = Utils::Utf8ToWide(j.at("id").get<std::string>());
    if (j.contains("alias")) profile.alias = Utils::Utf8ToWide(j.at("alias").get<std::string>());
    if (j.contains("upstream_url")) profile.upstreamUrl = Utils::Utf8ToWide(j.at("upstream_url").get<std::string>());
    if (j.contains("api_key")) {
        const auto& keyField = j.at("api_key");
        if (keyField.is_string()) {
            profile.apiKey = Utils::Utf8ToWide(keyField.get<std::string>());
        } else if (keyField.is_object() && keyField.contains("_enc")) {
            // Encrypted format - SettingsManager will decrypt before passing to us.
            // If we see this, the key hasn't been decrypted yet.
            profile.apiKey = L"";
        }
    }
    if (j.contains("port")) j.at("port").get_to(profile.port);
    if (j.contains("use_openai_model")) j.at("use_openai_model").get_to(profile.useOpenAIModel);
    if (j.contains("protocol_mode")) {
        profile.protocolMode = ProtocolModeFromString(j.at("protocol_mode").get<std::string>());
    }
    if (j.contains("pii_confidence_threshold")) j.at("pii_confidence_threshold").get_to(profile.piiConfidenceThreshold);
    if (j.contains("enabled_pii_types")) {
        profile.enabledPIITypes.clear();
        for (const auto& t : j.at("enabled_pii_types")) {
            profile.enabledPIITypes.push_back(Utils::Utf8ToWide(t.get<std::string>()));
        }
    }
    if (j.contains("regex_patterns")) {
        const auto& rp = j.at("regex_patterns");
        if (rp.is_array()) {
            for (const auto& p : rp) {
                RegexEntry entry;
                if (p.contains("pattern")) entry.pattern = Utils::Utf8ToWide(p.at("pattern").get<std::string>());
                if (p.contains("enabled")) p.at("enabled").get_to(entry.enabled);
                else entry.enabled = true;
                profile.regexPatterns.push_back(entry);
            }
        }
    }
    if (j.contains("keywords")) {
        const auto& kw = j.at("keywords");
        if (kw.is_array()) {
            for (const auto& k : kw) {
                KeywordEntry entry;
                if (k.contains("text")) entry.text = Utils::Utf8ToWide(k.at("text").get<std::string>());
                if (k.contains("case_sensitive")) k.at("case_sensitive").get_to(entry.caseSensitive);
                else entry.caseSensitive = true;
                if (k.contains("enabled")) k.at("enabled").get_to(entry.enabled);
                else entry.enabled = true;
                profile.keywords.push_back(entry);
            }
        }
    }
    if (j.contains("stats")) {
        RedactionStats::FromJson(j.at("stats"), profile.stats);
    }
    if (j.contains("enabled")) j.at("enabled").get_to(profile.enabled);
    return profile;
}

} // namespace AgentRedactor
