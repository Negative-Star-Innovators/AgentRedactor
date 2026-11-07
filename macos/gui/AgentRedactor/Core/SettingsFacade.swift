import Foundation

// Port of windows/EngineClient.h's SettingsFacade: typed profile/settings
// operations over the control API. Throws EngineClientError; call sites
// surface messages in alerts. Runs on the caller's queue — AppState marshals
// these onto its engine queue.

struct UnlockResult: Equatable {
    var ok: Bool
    var error: String?
    var helloNotEnabled: Bool
}

final class SettingsFacade {
    private let client: EngineClient

    init(client: EngineClient) {
        self.client = client
    }

    // MARK: Profiles

    func getProfiles() throws -> [ApiKeyProfileModel] {
        let response = try client.get("/profiles")
        return (response.body as? [Any])?.compactMap(ApiKeyProfileModel.fromJSON) ?? []
    }

    @discardableResult
    func addProfile(_ profile: ApiKeyProfileModel) throws -> String {
        let response = try client.post("/profiles", body: profile.json)
        return (response.body as? [String: Any])?["id"] as? String ?? ""
    }

    func updateProfile(_ profile: ApiKeyProfileModel) throws {
        try client.put("/profiles/\(profile.id)", body: profile.json)
    }

    func removeProfile(id: String) throws {
        try client.delete("/profiles/\(id)")
    }

    /// The one endpoint that serves a secret; 403 while the session is locked.
    func getProfileApiKey(id: String) throws -> String {
        let response = try client.get("/profiles/\(id)/apikey")
        return (response.body as? [String: Any])?["apiKey"] as? String ?? ""
    }

    // MARK: Settings

    func getSettings() throws -> EngineSettings {
        try EngineSettings.fromJSON(client.get("/settings").body)
    }

    /// PUT /settings/<key> with {"value": value}; the engine rejects some
    /// combinations (e.g. showSensitive while logging is off) with 400 + error.
    func putSetting(key: String, value: Any) throws {
        try client.put("/settings/\(key)", body: ["value": value])
    }

    // MARK: Master password (typed-password flow, the non-Windows engine path)

    func enableMasterPassword(password: String) throws {
        try client.put("/settings/enableMasterPassword", body: ["password": password])
    }

    /// Engine accepts this only on an unlocked session (403 "locked" otherwise).
    func disableMasterPassword() throws {
        try client.putWithConsentTimeout("/settings/disableMasterPassword", body: ["value": false])
    }

    /// Unlock after the caller verified the user in-process (LocalAuthentication
    /// on macOS). Body carries the typed master password; engine stays unlocked
    /// for proxying until PUT /settings/lock.
    func unlock(password: String) throws -> UnlockResult {
        let response = try client.postWithConsentTimeout("/unlock", body: ["password": password])
        guard let dict = response.body as? [String: Any] else {
            return UnlockResult(ok: false, error: "bad response", helloNotEnabled: false)
        }
        return UnlockResult(
            ok: dict["ok"] as? Bool ?? false,
            error: dict["error"] as? String,
            helloNotEnabled: dict["helloNotEnabled"] as? Bool ?? false)
    }

    /// Lock the session without stopping the proxies (sent on hide/quit when
    /// the engine survives, and by the inactivity timer).
    func lockSession() throws {
        try client.put("/settings/lock", body: ["value": false])
    }
}
