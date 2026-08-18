#pragma once

// GUI-side client for the engine's localhost control API (agentredactor.exe).
//
// EngineClient is the WinHTTP transport: it reads <configDir>/control.json
// ({"port": N, "token": "..."}) written by the engine and issues authenticated
// requests to 127.0.0.1. On top of it sit three facades — SettingsFacade,
// LogsFacade, ProxyFacade — that deliberately mirror the method signatures the
// GUI pages used to call on SettingsManager/LogManager/ProxyEngine, so the
// pages keep working unchanged while the real services live in the engine
// process. All calls are synchronous; localhost round-trips are fast and every
// failure is surfaced as "engine not running" (false/empty results).

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "api_key_profile.h"

using json = nlohmann::json;

namespace AgentRedactor {

class LogManager;

// Mirrors proxy_engine.h's SessionMatch (the GUI no longer links ProxyEngine).
struct SessionMatch {
    std::wstring type;
    std::wstring matchedText;
    std::wstring detail;
    std::wstring timestamp;
};

// Outcome of a POST /unlock/hello (Windows Hello consent) call.
struct HelloUnlockOutcome {
    bool ok = false;
    bool canceled = false;
    bool retriesExhausted = false;
    bool timedOut = false;
    bool unavailable = false;
    bool helloNotEnabled = false;
};

class EngineClient {
public:
    EngineClient() = default;

    // Loads connection info from <configDir>/control.json. Returns false when
    // the file is missing or malformed (engine not started yet).
    bool Connect(const std::filesystem::path& configDir);
    bool IsConnected() const { return port_ != 0 && !token_.empty(); }

    // GET /status reachability check.
    bool Ping();

    bool Get(const std::wstring& path, json& out) const;
    bool Post(const std::wstring& path, const json& body, json* out = nullptr) const;
    bool Put(const std::wstring& path, const json& body, json* out = nullptr) const;
    bool Delete(const std::wstring& path) const;

    int Port() const { return port_; }

private:
    bool Request(const std::wstring& method, const std::wstring& path,
        const std::string* body, long& statusCode, std::string& responseBody) const;

    int port_ = 0;
    std::wstring token_;
    // Engine process id (from control.json): the GUI grants it the foreground
    // (AllowSetForegroundWindow) before engine-owned consent calls (e.g. the
    // gated disable) so the Windows Hello dialog can take the foreground.
    unsigned long enginePid_ = 0;
};

class SettingsFacade {
public:
    explicit SettingsFacade(EngineClient* client) : client_(client) {}

    std::vector<ApiKeyProfile> GetProfiles() const;
    void AddProfile(const ApiKeyProfile& profile);
    void UpdateProfile(const ApiKeyProfile& profile);
    void RemoveProfile(const std::wstring& id);
    std::optional<ApiKeyProfile> GetProfileById(const std::wstring& id) const;

    bool IsStartOnBoot() const;
    void SetStartOnBoot(bool enabled);
    std::wstring GetOnnxProvider() const;
    void SetOnnxProvider(const std::wstring& provider);
    bool IsMasterPasswordEnabled() const;
    HelloUnlockOutcome UnlockWithHello() const;
    // Unlocks a Windows-Hello session WITHOUT a consent prompt: the caller
    // (the GUI) has already verified the user in-process (POST /unlock).
    bool UnlockEngine() const;
    // Locks the session without stopping the proxies (PUT /settings/lock).
    void LockSession() const;
    // Fetches the real (unmasked) API key for a profile; empty on failure.
    std::wstring GetProfileApiKey(const std::wstring& id) const;
    bool HelloVerify() const;
    bool EnableMasterPassword();
    // Disables Windows-Hello protection. The ENGINE runs the consent prompt
    // inside this call (PUT /settings/disableMasterPassword): only a Verified
    // result disables, so the outcome mirrors HelloUnlockOutcome.
    HelloUnlockOutcome DisableMasterPassword();
    bool IsHelloEnabled() const;
    bool IsLoggingEnabled() const;
    void SetLoggingEnabled(bool enabled);
    std::wstring GetAppLanguage() const;
    void SetAppLanguage(const std::wstring& language);

    // The engine persists on every mutation; kept for call-site compatibility.
    void SaveSettings() {}

private:
    bool GetSettingsJson(json& out) const;
    bool PutValue(const std::wstring& key, const json& value);

    EngineClient* client_;
};

class LogsFacade {
public:
    // localMirror receives SetLoggingEnabled so the GUI's own file logging
    // follows the same toggle (the engine applies it process-side too).
    LogsFacade(EngineClient* client, LogManager* localMirror)
        : client_(client), localMirror_(localMirror) {}

    void SetLoggingEnabled(bool enabled);
    bool IsLoggingEnabled() const;
    void SetShowSensitive(bool show);
    bool IsShowSensitive() const;

private:
    EngineClient* client_;
    LogManager* localMirror_;
};

class ProxyFacade {
public:
    explicit ProxyFacade(EngineClient* client) : client_(client) {}

    std::vector<SessionMatch> GetSessionMatches(const std::wstring& profileId) const;
    void ClearSessionMatches(const std::wstring& profileId);

private:
    EngineClient* client_;
};

} // namespace AgentRedactor
