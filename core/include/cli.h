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
// command demands a fresh Windows Hello consent on the spot. The consent
// itself runs IN-PROCESS in the client (the transport's `consent` hook) —
// Windows attaches the dialog to the window of the ACTIVE application, so an
// engine-owned prompt would always land in the background. On Verified the
// transport unlocks the engine via POST /unlock (the documented
// "caller has already verified in-process" path, same as the GUI).
// status/help stay open; `password disable` is gated the same way (consent,
// then the engine's disable endpoint, which requires the session unlocked).
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

// Outcome of the transport's Windows Hello consent prompt.
enum class HelloConsentOutcome {
    Granted,        // User verified; the engine session is now unlocked
    Canceled,       // User dismissed the prompt
    RetriesExhausted, // Too many failed verification attempts
    TimedOut,       // No answer within the watchdog
    Unavailable,    // Hello not configured / no hardware / device busy
    Failed,         // Unexpected error
};

struct CliTransport {
    // All return false when the engine is unreachable or the call fails;
    // parsed JSON responses are returned via out (when non-null).
    std::function<bool(const std::wstring& path, json& out)> get;
    std::function<bool(const std::wstring& path, const json& body, json* out)> post;
    std::function<bool(const std::wstring& path, const json& body, json* out)> put;
    std::function<bool(const std::wstring& path)> del;
    // Shows the Windows Hello consent prompt in-process and, on success,
    // unlocks the engine session (POST /unlock). Falls back to
    // HelloConsentOutcome::Unavailable on platforms without Windows Hello.
    std::function<HelloConsentOutcome()> consent;
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
