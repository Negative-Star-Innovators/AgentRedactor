#pragma once

// Linux mirror of windows/engine/control_api_client.h: the CLI-side client
// for the engine's localhost control API, implemented over libcurl.

#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace AgentRedactor {

class ControlApiClient {
public:
    // Reads <configDir>/control.json (port + bearer token). False when the
    // engine is not running.
    bool Connect(const std::filesystem::path& configDir);
    bool IsConnected() const { return port_ != 0 && !token_.empty(); }

    bool Get(const std::wstring& path, json& out) const;
    bool Post(const std::wstring& path, const json& body, json* out) const;
    bool Put(const std::wstring& path, const json& body, json* out) const;
    bool Delete(const std::wstring& path) const;

    // Typed master password unlock (POST /unlock {"password": ...}). There is
    // no Windows Hello on Linux, so this is the only unlock path.
    bool UnlockWithPassword(const std::wstring& password) const;

private:
    bool Request(const std::wstring& method, const std::wstring& path,
        const std::string* body, long& statusCode, std::string& responseBody) const;

    int port_ = 0;
    std::wstring token_;
};

} // namespace AgentRedactor
