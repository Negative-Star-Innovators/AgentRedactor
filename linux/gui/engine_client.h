#pragma once

// GUI-side client for the engine's loopback control API — the Linux mirror
// of windows/EngineClient. Thin typed wrapper over the shared curl
// ControlApiClient (linux/engine/control_api_client.*); plain blocking C++,
// called from AppState's worker thread and (for user-initiated mutations,
// which are sub-millisecond on loopback) the UI thread.

#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>

#include "control_api_client.h"
#include "utils.h"

using json = nlohmann::json;

namespace AgentRedactor {

class EngineClient {
public:
    bool Connect(const std::filesystem::path& configDir) { return client_.Connect(configDir); }
    bool IsConnected() const { return client_.IsConnected(); }

    bool Ping() { json s; return GetStatus(s); }

    bool GetStatus(json& out) { return client_.Get(L"/status", out); }
    bool GetSettings(json& out) { return client_.Get(L"/settings", out); }
    bool GetProfiles(json& out) { return client_.Get(L"/profiles", out); }

    // False + WasLocked() means the session is locked (403).
    bool GetApiKey(const std::wstring& profileId, std::wstring& keyOut) {
        json out;
        if (!client_.Get(L"/profiles/" + profileId + L"/apikey", out)) return false;
        keyOut = Utils::Utf8ToWide(out.value("apiKey", std::string("")));
        return true;
    }

    bool PostProfile(const json& profile, std::wstring& idOut) {
        json out;
        if (!client_.Post(L"/profiles", profile, &out)) return false;
        idOut = Utils::Utf8ToWide(out.value("id", std::string("")));
        return true;
    }

    bool PutProfile(const std::wstring& profileId, const json& profile) {
        return client_.Put(L"/profiles/" + profileId, profile, nullptr);
    }

    bool DeleteProfile(const std::wstring& profileId) {
        return client_.Delete(L"/profiles/" + profileId);
    }

    bool PutSetting(const std::wstring& key, const json& value) {
        return client_.Put(L"/settings/" + key, json{{"value", value}}, nullptr);
    }

    bool Unlock(const std::wstring& password) { return client_.UnlockWithPassword(password); }
    bool Lock() { return PutSetting(L"lock", json::object()); }

    bool GetMatches(const std::wstring& profileId, json& out) {
        return client_.Get(L"/profiles/" + profileId + L"/matches", out);
    }
    bool DeleteMatches(const std::wstring& profileId) {
        return client_.Delete(L"/profiles/" + profileId + L"/matches");
    }

    bool RestartListeners() { return client_.Post(L"/engine/restart-listeners", json::object(), nullptr); }
    bool DownloadModel() { return client_.Post(L"/engine/download-model", json::object(), nullptr); }
    bool StopEngine() { return client_.Post(L"/engine/stop", json::object(), nullptr); }

    bool EnableMasterPassword(const std::wstring& password) {
        return client_.Put(L"/settings/enableMasterPassword",
            json{{"password", Utils::WideToUtf8(password)}}, nullptr);
    }

    bool DisableMasterPassword() {
        return client_.Put(L"/settings/disableMasterPassword", json{{"value", false}}, nullptr);
    }

    long LastStatus() const { return client_.LastStatus(); }
    bool WasLocked() const { return client_.LastStatus() == 403; }

private:
    ControlApiClient client_;
};

} // namespace AgentRedactor
