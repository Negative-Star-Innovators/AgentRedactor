#pragma once

// Engine-process-side client for the engine's own localhost control API, used
// by the CLI subcommands of agentredactor.exe (`agentredactor status` etc.).
//
// This mirrors windows/EngineClient (the GUI-side client) but is standalone:
// the engine project has no pch.h, so EngineClient.cpp cannot be shared
// (precompiled-header builds require pch.h as the unconditional first
// include). Command logic lives in core/src/cli.cpp; this is only the WinHTTP
// transport behind CliTransport.

#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>

#include "cli.h"

using json = nlohmann::json;

namespace AgentRedactor {

class ControlApiClient {
public:
    ControlApiClient() = default;

    // Loads connection info from <configDir>/control.json. Returns false when
    // the file is missing or malformed (engine not started yet).
    bool Connect(const std::filesystem::path& configDir);
    bool IsConnected() const { return port_ != 0 && !token_.empty(); }

    bool Get(const std::wstring& path, json& out) const;
    bool Post(const std::wstring& path, const json& body, json* out = nullptr) const;
    bool Put(const std::wstring& path, const json& body, json* out = nullptr) const;
    bool Delete(const std::wstring& path) const;

    // In-process Windows Hello consent (the CLI process is the ACTIVE
    // application when run from a terminal, so the dialog comes to the
    // foreground — an engine-owned prompt belongs to a background process and
    // always lands in the background). On Verified the engine session is
    // unlocked via POST /unlock.
    HelloConsentOutcome ConsentWithHello() const;

private:
    bool Request(const std::wstring& method, const std::wstring& path,
        const std::string* body, long& statusCode, std::string& responseBody) const;

    int port_ = 0;
    std::wstring token_;
    // Engine process id (from control.json): the CLI grants it the foreground
    // (AllowSetForegroundWindow) before consent prompts so the engine-owned
    // Windows Hello dialog can take the foreground.
    unsigned long enginePid_ = 0;
};

} // namespace AgentRedactor
