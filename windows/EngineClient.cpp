#include "pch.h"
#include "EngineClient.h"
#include "utils.h"
#include "logging.h"
#include "log_manager.h"
#include <winhttp.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

using namespace AgentRedactor;

// ---------------------------------------------------------------------------
// EngineClient — WinHTTP transport
// ---------------------------------------------------------------------------

bool EngineClient::Connect(const std::filesystem::path& configDir) {
    port_ = 0;
    token_.clear();

    auto content = Utils::ReadFileAsString(configDir / L"control.json");
    if (!content) return false;
    try {
        json j = json::parse(Utils::WideToUtf8(*content));
        port_ = j.at("port").get<int>();
        token_ = Utils::Utf8ToWide(j.at("token").get<std::string>());
        enginePid_ = j.value("pid", 0UL);
    } catch (...) {
        port_ = 0;
        token_.clear();
        enginePid_ = 0;
        return false;
    }
    return IsConnected();
}

bool EngineClient::Ping() {
    json j;
    return Get(L"/status", j);
}

bool EngineClient::Get(const std::wstring& path, json& out) const {
    long status = 0;
    std::string body;
    if (!Request(L"GET", path, nullptr, status, body) || status != 200) return false;
    try {
        out = json::parse(body);
    } catch (...) {
        return false;
    }
    return true;
}

bool EngineClient::Post(const std::wstring& path, const json& body, json* out) const {
    std::string payload = body.dump();
    long status = 0;
    std::string respBody;
    if (!Request(L"POST", path, &payload, status, respBody) || status != 200) return false;
    if (out) {
        try {
            *out = json::parse(respBody);
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool EngineClient::Put(const std::wstring& path, const json& body, json* out) const {
    std::string payload = body.dump();
    long status = 0;
    std::string respBody;
    if (!Request(L"PUT", path, &payload, status, respBody) || status != 200) return false;
    if (out) {
        try {
            *out = json::parse(respBody);
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool EngineClient::Delete(const std::wstring& path) const {
    long status = 0;
    std::string body;
    return Request(L"DELETE", path, nullptr, status, body) && status == 200;
}

bool EngineClient::Request(const std::wstring& method, const std::wstring& path,
    const std::string* body, long& statusCode, std::string& responseBody) const {
    statusCode = 0;
    responseBody.clear();
    if (!IsConnected()) return false;

    bool ok = false;
    HINTERNET hSession = WinHttpOpen(L"AgentRedactor-GUI/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    // Short timeouts: the engine is localhost; a hang means it is not running.
    // /unlock/hello and /settings/disableMasterPassword hold the request while
    // the user answers the Windows Hello consent prompt (the engine cancels it
    // after 60s), so they get a long receive timeout.
    if (path == L"/unlock/hello" || path == L"/settings/disableMasterPassword") {
        WinHttpSetTimeouts(hSession, 1500, 1500, 3000, 90000);
        // Lift the foreground lock for the engine process: its consent dialog
        // is created from a background process and would otherwise pop up
        // behind this window.
        if (enginePid_ != 0) ::AllowSetForegroundWindow(enginePid_);
    } else {
        WinHttpSetTimeouts(hSession, 1500, 1500, 3000, 3000);
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)port_, 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, method.c_str(), path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hRequest) {
            std::wstring headers = L"Authorization: Bearer " + token_ + L"\r\nContent-Type: application/json";
            LPVOID bodyPtr = body ? (LPVOID)body->data() : WINHTTP_NO_REQUEST_DATA;
            DWORD bodyLen = body ? (DWORD)body->size() : 0;
            if (WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.size(),
                    bodyPtr, bodyLen, bodyLen, 0) &&
                WinHttpReceiveResponse(hRequest, nullptr)) {
                DWORD code = 0;
                DWORD codeSize = sizeof(code);
                WinHttpQueryHeaders(hRequest,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX);
                statusCode = (long)code;
                for (;;) {
                    DWORD available = 0;
                    if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
                    if (available == 0) { ok = true; break; }
                    std::string chunk(available, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(hRequest, chunk.data(), available, &read)) break;
                    chunk.resize(read);
                    responseBody += chunk;
                }
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return ok;
}

// ---------------------------------------------------------------------------
// SettingsFacade
// ---------------------------------------------------------------------------

bool SettingsFacade::GetSettingsJson(json& out) const {
    return client_ && client_->Get(L"/settings", out);
}

bool SettingsFacade::PutValue(const std::wstring& key, const json& value) {
    if (!client_) return false;
    json body;
    body["value"] = value;
    return client_->Put(L"/settings/" + key, body);
}

std::vector<ApiKeyProfile> SettingsFacade::GetProfiles() const {
    std::vector<ApiKeyProfile> profiles;
    json arr;
    if (!client_ || !client_->Get(L"/profiles", arr)) return profiles;
    try {
        for (const auto& pj : arr) {
            profiles.push_back(ApiKeyProfile::FromJson(pj));
        }
    } catch (...) {}
    return profiles;
}

void SettingsFacade::AddProfile(const ApiKeyProfile& profile) {
    if (!client_) return;
    json j;
    profile.ToJson(j);
    client_->Post(L"/profiles", j);
}

void SettingsFacade::UpdateProfile(const ApiKeyProfile& profile) {
    if (!client_ || profile.id.empty()) return;
    json j;
    profile.ToJson(j);
    client_->Put(L"/profiles/" + profile.id, j);
}

void SettingsFacade::RemoveProfile(const std::wstring& id) {
    if (!client_) return;
    client_->Delete(L"/profiles/" + id);
}

std::optional<ApiKeyProfile> SettingsFacade::GetProfileById(const std::wstring& id) const {
    for (const auto& p : GetProfiles()) {
        if (p.id == id) return p;
    }
    return std::nullopt;
}

bool SettingsFacade::IsStartOnBoot() const {
    json j;
    return GetSettingsJson(j) && j.value("startOnBoot", false);
}

void SettingsFacade::SetStartOnBoot(bool enabled) {
    PutValue(L"startOnBoot", enabled);
}

std::wstring SettingsFacade::GetOnnxProvider() const {
    json j;
    if (!GetSettingsJson(j)) return L"";
    return Utils::Utf8ToWide(j.value("onnxProvider", ""));
}

void SettingsFacade::SetOnnxProvider(const std::wstring& provider) {
    PutValue(L"onnxProvider", Utils::WideToUtf8(provider));
}

bool SettingsFacade::IsMasterPasswordEnabled() const {
    json j;
    return GetSettingsJson(j) && j.value("masterPasswordEnabled", false);
}

HelloUnlockOutcome SettingsFacade::UnlockWithHello() const {
    HelloUnlockOutcome outcome;
    if (!client_) return outcome;
    json out;
    if (!client_->Post(L"/unlock/hello", json::object(), &out)) return outcome;
    outcome.ok = out.value("ok", false);
    outcome.canceled = out.value("canceled", false);
    outcome.retriesExhausted = out.value("retriesExhausted", false);
    outcome.timedOut = out.value("timedOut", false);
    outcome.unavailable = out.value("unavailable", false);
    outcome.helloNotEnabled = out.value("helloNotEnabled", false);
    return outcome;
}

bool SettingsFacade::UnlockEngine() const {
    if (!client_) return false;
    json out;
    return client_->Post(L"/unlock", json::object(), &out) && out.value("ok", false);
}

// Locks the session (PUT /settings/lock): proxies keep running, only
// sensitive reads/settings are gated until the next Hello unlock.
void SettingsFacade::LockSession() const {
    if (!client_) return;
    client_->Put(L"/settings/lock", json::object());
}

std::wstring SettingsFacade::GetProfileApiKey(const std::wstring& id) const {
    if (!client_) return L"";
    json out;
    if (!client_->Get(L"/profiles/" + id + L"/apikey", out)) return L"";
    return Utils::Utf8ToWide(out.value("apiKey", std::string("")));
}

bool SettingsFacade::HelloVerify() const {
    if (!client_) return false;
    json out;
    return client_->Post(L"/hello/verify", json::object(), &out) && out.value("ok", false);
}

// Enables Windows-Hello-only protection: no typed password exists.
bool SettingsFacade::EnableMasterPassword() {
    json body;
    body["value"] = "";
    body["hello"] = true;
    return client_ && client_->Put(L"/settings/enableMasterPassword", body);
}

HelloUnlockOutcome SettingsFacade::DisableMasterPassword() {
    // The engine runs the Windows Hello consent inside this call and only
    // disables on a Verified result; every failure keeps protection on.
    HelloUnlockOutcome outcome;
    if (!client_) return outcome;
    json out;
    if (!client_->Put(L"/settings/disableMasterPassword", json{{"value", false}}, &out)) return outcome;
    outcome.ok = out.value("ok", false);
    outcome.canceled = out.value("canceled", false);
    outcome.retriesExhausted = out.value("retriesExhausted", false);
    outcome.timedOut = out.value("timedOut", false);
    outcome.unavailable = out.value("unavailable", false);
    outcome.helloNotEnabled = out.value("helloNotEnabled", false);
    return outcome;
}

bool SettingsFacade::IsHelloEnabled() const {
    json j;
    return GetSettingsJson(j) && j.value("helloEnabled", false);
}

bool SettingsFacade::IsLoggingEnabled() const {
    json j;
    return GetSettingsJson(j) && j.value("loggingEnabled", false);
}

void SettingsFacade::SetLoggingEnabled(bool enabled) {
    PutValue(L"loggingEnabled", enabled);
}

std::wstring SettingsFacade::GetAppLanguage() const {
    json j;
    if (!GetSettingsJson(j)) return L"";
    return Utils::Utf8ToWide(j.value("appLanguage", ""));
}

void SettingsFacade::SetAppLanguage(const std::wstring& language) {
    PutValue(L"appLanguage", Utils::WideToUtf8(language));
}

// ---------------------------------------------------------------------------
// LogsFacade
// ---------------------------------------------------------------------------

void LogsFacade::SetLoggingEnabled(bool enabled) {
    if (client_) {
        json body;
        body["value"] = enabled;
        client_->Put(L"/settings/loggingEnabled", body);
    }
    if (localMirror_) localMirror_->SetLoggingEnabled(enabled);
}

bool LogsFacade::IsLoggingEnabled() const {
    json j;
    return client_ && client_->Get(L"/settings", j) && j.value("loggingEnabled", false);
}

void LogsFacade::SetShowSensitive(bool show) {
    if (client_) {
        json body;
        body["value"] = show;
        client_->Put(L"/settings/showSensitive", body);
    }
}

bool LogsFacade::IsShowSensitive() const {
    json j;
    return client_ && client_->Get(L"/settings", j) && j.value("showSensitive", false);
}

// ---------------------------------------------------------------------------
// ProxyFacade
// ---------------------------------------------------------------------------

std::vector<SessionMatch> ProxyFacade::GetSessionMatches(const std::wstring& profileId) const {
    std::vector<SessionMatch> matches;
    json arr;
    if (!client_ || !client_->Get(L"/profiles/" + profileId + L"/matches", arr)) return matches;
    try {
        for (const auto& mj : arr) {
            SessionMatch m;
            m.type = Utils::Utf8ToWide(mj.value("type", ""));
            m.matchedText = Utils::Utf8ToWide(mj.value("matchedText", ""));
            m.detail = Utils::Utf8ToWide(mj.value("detail", ""));
            m.timestamp = Utils::Utf8ToWide(mj.value("timestamp", ""));
            matches.push_back(std::move(m));
        }
    } catch (...) {}
    return matches;
}

void ProxyFacade::ClearSessionMatches(const std::wstring& profileId) {
    if (!client_) return;
    client_->Delete(L"/profiles/" + profileId + L"/matches");
}
