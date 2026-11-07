import Foundation

// Data shapes mirroring core/include/api_key_profile.h (ToJson/FromJson in
// core/src/api_key_profile.cpp) and the control-API payloads in
// core/src/engine_app.cpp. All parsing is defensive: a missing/mistyped field
// yields the engine's default rather than a crash.

struct KeywordEntryModel: Equatable {
    var text: String
    var caseSensitive: Bool
    var enabled: Bool = true

    var json: [String: Any] {
        ["text": text, "case_sensitive": caseSensitive, "enabled": enabled]
    }

    static func fromJSON(_ raw: Any?) -> KeywordEntryModel? {
        guard let dict = raw as? [String: Any], let text = dict["text"] as? String else { return nil }
        return KeywordEntryModel(
            text: text,
            caseSensitive: dict["case_sensitive"] as? Bool ?? true,
            enabled: dict["enabled"] as? Bool ?? true)
    }
}

struct RegexEntryModel: Equatable {
    var pattern: String
    var enabled: Bool = true

    var json: [String: Any] {
        ["pattern": pattern, "enabled": enabled]
    }

    static func fromJSON(_ raw: Any?) -> RegexEntryModel? {
        guard let dict = raw as? [String: Any], let pattern = dict["pattern"] as? String else { return nil }
        return RegexEntryModel(pattern: pattern, enabled: dict["enabled"] as? Bool ?? true)
    }
}

struct RedactionStatsModel: Equatable {
    var totalRequests: Int = 0
    var totalPIIDetected: Int = 0
    var totalRegexMatches: Int = 0
    var totalKeywordMatches: Int = 0
    var piiTypeBreakdown: [String: Int] = [:]

    var json: [String: Any] {
        [
            "total_requests": totalRequests,
            "total_pii_detected": totalPIIDetected,
            "total_regex_matches": totalRegexMatches,
            "total_keyword_matches": totalKeywordMatches,
            "pii_type_breakdown": piiTypeBreakdown,
        ]
    }

    static let empty = RedactionStatsModel()

    static func zeroedJSON() -> [String: Any] {
        RedactionStatsModel.empty.json
    }

    static func fromJSON(_ raw: Any?) -> RedactionStatsModel {
        guard let dict = raw as? [String: Any] else { return .empty }
        var stats = RedactionStatsModel()
        stats.totalRequests = dict["total_requests"] as? Int ?? 0
        stats.totalPIIDetected = dict["total_pii_detected"] as? Int ?? 0
        stats.totalRegexMatches = dict["total_regex_matches"] as? Int ?? 0
        stats.totalKeywordMatches = dict["total_keyword_matches"] as? Int ?? 0
        if let breakdown = dict["pii_type_breakdown"] as? [String: Any] {
            for (key, value) in breakdown {
                stats.piiTypeBreakdown[key] = value as? Int ?? 0
            }
        }
        return stats
    }
}

struct ApiKeyProfileModel: Identifiable, Equatable {
    var id: String
    var alias: String
    var upstreamUrl: String
    var apiKeyMasked: String
    var port: Int
    var useOpenAIModel: Bool
    var enabledPIITypes: [String]
    var piiConfidenceThreshold: Double
    var regexPatterns: [RegexEntryModel]
    var keywords: [KeywordEntryModel]
    var stats: RedactionStatsModel
    var enabled: Bool
    /// proxyRunning comes from /status (per-port), not from /profiles.
    var proxyRunning: Bool = false
    /// True when the API key edit is the masked placeholder ("abc...****"),
    /// meaning "keep the stored key" on PUT.
    var apiKeyIsMaskedPlaceholder: Bool = false

    var json: [String: Any] {
        var body: [String: Any] = [
            "alias": alias,
            "upstream_url": upstreamUrl,
            "api_key": apiKeyMasked,
            "port": port,
            "use_openai_model": useOpenAIModel,
            "protocol_mode": "none",
            "enabled_pii_types": enabledPIITypes,
            "pii_confidence_threshold": piiConfidenceThreshold,
            "regex_patterns": regexPatterns.map { $0.json },
            "keywords": keywords.map { $0.json },
            "stats": stats.json,
            "enabled": enabled,
        ]
        // POST /profiles must omit "id" entirely: ApiKeyProfile::FromJson only
        // generates a UUID when the key is absent, and an explicit empty id
        // would be stored verbatim.
        if !id.isEmpty {
            body["id"] = id
        }
        return body
    }

    /// PUT body used when saving an edited profile: a masked placeholder key
    /// is sent verbatim (the engine treats "...****" as "unchanged").
    static func fromJSON(_ raw: Any?) -> ApiKeyProfileModel? {
        guard let dict = raw as? [String: Any] else { return nil }
        guard let id = dict["id"] as? String else { return nil }
        let apiKey = dict["api_key"] as? String ?? ""
        return ApiKeyProfileModel(
            id: id,
            alias: dict["alias"] as? String ?? "",
            upstreamUrl: (dict["upstream_url"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines),
            apiKeyMasked: apiKey,
            port: dict["port"] as? Int ?? 8080,
            useOpenAIModel: dict["use_openai_model"] as? Bool ?? true,
            enabledPIITypes: dict["enabled_pii_types"] as? [String] ?? [],
            piiConfidenceThreshold: dict["pii_confidence_threshold"] as? Double ?? 0.9,
            regexPatterns: (dict["regex_patterns"] as? [Any])?.compactMap(RegexEntryModel.fromJSON) ?? [],
            keywords: (dict["keywords"] as? [Any])?.compactMap(KeywordEntryModel.fromJSON) ?? [],
            stats: RedactionStatsModel.fromJSON(dict["stats"]),
            enabled: dict["enabled"] as? Bool ?? true,
            proxyRunning: dict["proxyRunning"] as? Bool ?? false,
            apiKeyIsMaskedPlaceholder: apiKey.contains("...****"))
    }
}

struct SessionMatchModel: Identifiable, Equatable {
    var id: String { timestamp + type + matchedText }
    var type: String
    var matchedText: String
    var detail: String
    var timestamp: String

    static func fromJSON(_ raw: Any?) -> SessionMatchModel? {
        guard let dict = raw as? [String: Any] else { return nil }
        return SessionMatchModel(
            type: dict["type"] as? String ?? "",
            matchedText: dict["matchedText"] as? String ?? "",
            detail: dict["detail"] as? String ?? "",
            timestamp: dict["timestamp"] as? String ?? "")
    }
}

/// GET /status payload.
struct EngineStatus: Equatable {
    var engineVersion: String = "dev"
    var unlocked: Bool = true
    var masterPasswordEnabled: Bool = false
    var helloEnabled: Bool = false
    var modelDownloadRequired: Bool = false
    var modelDownloadInProgress: Bool = false
    var modelDownloadFailed: Bool = false
    var modelDownloadWaitingToRetry: Bool = false
    /// -1 = unknown/indeterminate.
    var modelDownloadPercent: Int = -1
    var modelDownloadStatus: String = ""
    var profiles: [StatusProfile] = []

    struct StatusProfile: Equatable {
        var id: String
        var port: Int
        var enabled: Bool
        var proxyRunning: Bool
    }

    static func fromJSON(_ raw: Any?) -> EngineStatus {
        guard let dict = raw as? [String: Any] else { return EngineStatus() }
        var status = EngineStatus()
        status.engineVersion = dict["engineVersion"] as? String ?? "dev"
        status.unlocked = dict["unlocked"] as? Bool ?? true
        status.masterPasswordEnabled = dict["masterPasswordEnabled"] as? Bool ?? false
        status.helloEnabled = dict["helloEnabled"] as? Bool ?? false
        status.modelDownloadRequired = dict["modelDownloadRequired"] as? Bool ?? false
        status.modelDownloadInProgress = dict["modelDownloadInProgress"] as? Bool ?? false
        status.modelDownloadFailed = dict["modelDownloadFailed"] as? Bool ?? false
        status.modelDownloadWaitingToRetry = dict["modelDownloadWaitingToRetry"] as? Bool ?? false
        status.modelDownloadPercent = dict["modelDownloadPercent"] as? Int ?? -1
        status.modelDownloadStatus = dict["modelDownloadStatus"] as? String ?? ""
        status.profiles = (dict["profiles"] as? [Any])?.compactMap { raw -> StatusProfile? in
            guard let p = raw as? [String: Any], let id = p["id"] as? String else { return nil }
            return StatusProfile(
                id: id,
                port: p["port"] as? Int ?? 0,
                enabled: p["enabled"] as? Bool ?? true,
                proxyRunning: p["proxyRunning"] as? Bool ?? false)
        } ?? []
        return status
    }
}

/// GET /settings payload.
struct EngineSettings: Equatable {
    var startOnBoot: Bool = false
    var onnxProvider: String = "cpu"
    var loggingEnabled: Bool = false
    var showSensitive: Bool = false
    /// Effective BCP-47-ish tag (engine resolves "system default" to the OS
    /// locale before reporting it here).
    var appLanguage: String = "en"
    var masterPasswordEnabled: Bool = false
    var helloEnabled: Bool = false
    var unlocked: Bool = true
    var profilesRevision: Int = 0

    static func fromJSON(_ raw: Any?) -> EngineSettings {
        guard let dict = raw as? [String: Any] else { return EngineSettings() }
        var settings = EngineSettings()
        settings.startOnBoot = dict["startOnBoot"] as? Bool ?? false
        settings.onnxProvider = dict["onnxProvider"] as? String ?? "cpu"
        settings.loggingEnabled = dict["loggingEnabled"] as? Bool ?? false
        settings.showSensitive = dict["showSensitive"] as? Bool ?? false
        settings.appLanguage = dict["appLanguage"] as? String ?? "en"
        settings.masterPasswordEnabled = dict["masterPasswordEnabled"] as? Bool ?? false
        settings.helloEnabled = dict["helloEnabled"] as? Bool ?? false
        settings.unlocked = dict["unlocked"] as? Bool ?? true
        settings.profilesRevision = dict["profilesRevision"] as? Int ?? 0
        return settings
    }
}
