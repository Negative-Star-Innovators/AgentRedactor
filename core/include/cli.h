#pragma once

// OS-agnostic CLI for `agentredactor <subcommand>`: argument parsing, command
// logic, and output formatting for the flat command surface (status, get/set,
// profiles, regex, keywords, unlock, password, engine stop).
//
// The OS layer (windows/engine today, linux/ later) supplies two things:
//   - CliTransport: authenticated calls to the engine's localhost control API
//     (WinHTTP on Windows; a socket/curl implementation on Linux).
//   - CliConsole: line output and a no-echo password prompt.
// This file therefore stays free of OS dependencies so the same command
// behavior ships on every platform.
//
// Password model: when the master password is enabled and the engine is
// locked, every command except status / engine stop / help asks for the
// password (--password <pw>, or an interactive prompt when attached to a
// console) and unlocks the session before proceeding. Without a master
// password everything is open. Enforcement is UX-level, mirroring the GUI:
// the real access control is the bearer token in control.json, ACL'd to the
// current user.

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace AgentRedactor {

struct CliTransport {
    // All return false when the engine is unreachable or the call fails;
    // parsed JSON responses are returned via out (when non-null).
    std::function<bool(const std::wstring& path, json& out)> get;
    std::function<bool(const std::wstring& path, const json& body, json* out)> post;
    std::function<bool(const std::wstring& path, const json& body, json* out)> put;
    std::function<bool(const std::wstring& path)> del;
};

struct CliConsole {
    std::function<void(const std::wstring& line)> print;
    // No-echo password prompt. May be null / return nullopt when there is no
    // interactive console (piped output) — callers then require --password.
    std::function<std::optional<std::wstring>()> promptPassword;
};

// Executes one CLI invocation (args excluding argv[0], e.g. {"get","api-key",
// "--profile","2"}) and returns the process exit code:
//   0 success, 1 runtime error (engine down / locked / not found), 2 usage.
// `engine run` never reaches here — the OS entry point starts the engine
// process itself; only `engine stop` is handled as a CLI command.
int RunCli(const std::vector<std::wstring>& args,
           const CliTransport& transport,
           const CliConsole& console);

} // namespace AgentRedactor
