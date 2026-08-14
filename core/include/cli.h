#pragma once

// OS-agnostic CLI for `agentredactor <subcommand>`: argument parsing, command
// logic, and output formatting for the flat command surface (status, get/set,
// profiles list|add|delete, regex, keywords, password, pii-types).
//
// The OS layer (windows/engine today, linux/ later) supplies two things:
//   - CliTransport: authenticated calls to the engine's localhost control API
//     (WinHTTP on Windows; a socket/curl implementation on Linux).
//   - CliConsole: line output (no password prompt — there is no typed
//     password; the only prompt is the Windows Hello consent shown by the
//     engine, so this file stays fully console-prompt-free).
// This file therefore stays free of OS dependencies so the same command
// behavior ships on every platform.
//
// Password model: protection is Windows-Hello-only and lives entirely in the
// engine. There is NO `unlock` command: with protection enabled, every gated
// command demands a fresh Windows Hello consent on the spot (via the
// transport's post to /hello/verify when the session is already unlocked, or
// /unlock/hello when locked — the consent then also unlocks the engine).
// status/help stay open; `password disable` is the ungated recovery path.
// Enforcement is UX-level, mirroring the GUI: the real access control is the
// bearer token in control.json, ACL'd to the current user. `engine run` /
// `engine stop` are NOT CLI commands: engine lifecycle belongs to the GUI
// (spawn on startup, stop/lock on quit).

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
};

// Executes one CLI invocation (args excluding argv[0], e.g. {"get","api-key",
// "--profile","2"}) and returns the process exit code:
//   0 success, 1 runtime error (engine down / locked / not found), 2 usage.
int RunCli(const std::vector<std::wstring>& args,
           const CliTransport& transport,
           const CliConsole& console);

} // namespace AgentRedactor
