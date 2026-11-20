// agentredactor.exe — engine/CLI process (console subsystem, dual-mode).
//
// Engine mode: invoked with no arguments (or `engine run`, internal/dev-only)
// it hides the console window and runs the engine (settings, PII detection,
// proxy data plane, and the localhost control API).
//
// CLI mode: `agentredactor <subcommand> ...` (status, get/set, profiles,
// regex, keywords, unlock, password) talks to the running engine over the
// control API. The former `engine run` / `engine stop` CLI commands were
// removed: engine lifecycle belongs to the GUI. Command logic lives in
// core/src/cli.cpp;
// this file only supplies the Windows console plumbing and the WinHTTP
// transport (control_api_client, the engine-side mirror of the GUI's
// EngineClient).
#include "engine_app.h"
#include "control_api_client.h"
#include "cli.h"
#include "utils.h"
#include "logging.h"
#include <windows.h>
#include <cstdio>
#include <cwctype>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace AgentRedactor;

namespace {

// ---------------------------------------------------------------------------
// CLI console plumbing
// ---------------------------------------------------------------------------

// Output goes to the inherited console when there is one, to the pipe/file
// when stdout is redirected (AI agents, scripts), or to the parent console
// (AttachConsole) when launched with no console at all.
struct CliChannel {
    HANDLE out = nullptr;
    bool outIsConsole = false;
    HANDLE in = nullptr;
    bool inIsConsole = false;
};

CliChannel SetupCliChannel() {
    CliChannel ch;
    DWORD mode = 0;
    ch.out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (ch.out && GetConsoleMode(ch.out, &mode)) {
        ch.outIsConsole = true;
    } else if (!ch.out || ch.out == INVALID_HANDLE_VALUE) {
        // No stdout handle at all: attach to the parent console (CLI invoked
        // from a shortcut / GUI launcher).
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            ch.out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, 0, nullptr);
            if (ch.out == INVALID_HANDLE_VALUE) ch.out = nullptr;
            ch.outIsConsole = ch.out != nullptr;
        }
    }
    // else: stdout is a pipe or file — WriteFile to it as UTF-8.

    ch.in = GetStdHandle(STD_INPUT_HANDLE);
    if (ch.in && ch.in != INVALID_HANDLE_VALUE && GetConsoleMode(ch.in, &mode)) {
        ch.inIsConsole = true;
    } else if (ch.outIsConsole) {
        HANDLE hIn = CreateFileW(L"CONIN$", GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (hIn != INVALID_HANDLE_VALUE) {
            ch.in = hIn;
            ch.inIsConsole = GetConsoleMode(hIn, &mode) != 0;
        }
    }
    return ch;
}

void CliPrintRaw(const CliChannel& ch, const std::wstring& s, bool newline) {
    if (!ch.out) return;
    if (ch.outIsConsole) {
        DWORD written = 0;
        WriteConsoleW(ch.out, s.c_str(), static_cast<DWORD>(s.size()), &written, nullptr);
        if (newline) WriteConsoleW(ch.out, L"\n", 1, &written, nullptr);
    } else {
        std::string utf8 = Utils::WideToUtf8(s);
        if (newline) utf8 += "\n";
        DWORD written = 0;
        WriteFile(ch.out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
}

int RunCliCommand(const std::vector<std::wstring>& args) {
    CliChannel ch = SetupCliChannel();

    auto client = std::make_unique<ControlApiClient>();
    client->Connect(Utils::GetAppDataPath());

    CliTransport transport{
        [&client](const std::wstring& path, json& out) { return client->Get(path, out); },
        [&client](const std::wstring& path, const json& body, json* out) { return client->Post(path, body, out); },
        [&client](const std::wstring& path, const json& body, json* out) { return client->Put(path, body, out); },
        [&client](const std::wstring& path) { return client->Delete(path); },
        [&client]() { return client->ConsentWithHello(); },
    };
    CliConsole console{
        [&ch](const std::wstring& line) { CliPrintRaw(ch, line, true); },
    };

    return RunCli(args, transport, console);
}

// ---------------------------------------------------------------------------
// Engine mode
// ---------------------------------------------------------------------------

#ifdef AGENTREDACTOR_SELFRELEASE
// Velopack channel only: make `agentredactor` work in any terminal by putting
// the install dir (…\AgentRedactor\current) on the USER Path (HKCU\Environment
// — no admin). Idempotent; new terminals pick it up via WM_SETTINGCHANGE.
// The MSIX/Store channel does not need this — it declares an
// AppExecutionAlias in Package.appxmanifest instead.
void EnsureUserPathContainsExeDir() {
    std::wstring exeDir = Utils::GetExecutablePath().wstring();
    while (!exeDir.empty() && (exeDir.back() == L'\\' || exeDir.back() == L'/')) exeDir.pop_back();
    if (exeDir.empty()) return;

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ | KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return;

    wchar_t buffer[32767];
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    std::wstring pathValue;
    const LONG rc = RegGetValueW(hKey, nullptr, L"Path", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
        &type, buffer, &size);
    if (rc == ERROR_SUCCESS) pathValue.assign(buffer);

    // Case-insensitive entry match (Path entries are ';'-separated).
    std::wstring haystack = L";" + pathValue + L";";
    for (auto& c : haystack) c = static_cast<wchar_t>(std::towlower(c));
    std::wstring needle = L";" + exeDir + L";";
    for (auto& c : needle) c = static_cast<wchar_t>(std::towlower(c));
    if (haystack.find(needle) == std::wstring::npos) {
        std::wstring newPath = pathValue.empty() ? exeDir : pathValue + L";" + exeDir;
        RegSetValueExW(hKey, L"Path", 0, REG_EXPAND_SZ,
            reinterpret_cast<const BYTE*>(newPath.c_str()),
            static_cast<DWORD>((newPath.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
        DWORD_PTR result = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
            reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 2000, &result);
        return;
    }
    RegCloseKey(hKey);
}
#endif

int RunEngine(bool keepConsole) {
    // Hide the console immediately unless --console was given: the engine is
    // normally a background process; --console keeps it attached for debugging
    // and headless servers.
    if (!keepConsole) {
        HWND consoleWindow = GetConsoleWindow();
        if (consoleWindow) ShowWindow(consoleWindow, SW_HIDE);
        FreeConsole();
    }

#ifdef AGENTREDACTOR_SELFRELEASE
    EnsureUserPathContainsExeDir();
#endif

    // Single-instance guard, PER USER (Linux/macOS parity: those lock
    // <configDir>/engine.lock, and configDir is per-user).
    //
    // Scope matters here. The engine is a per-USER singleton, because all of its
    // state (settings.json, the control token, the API keys) lives in the user's
    // own %APPDATA%\AgentRedactor. Two DIFFERENT users on one machine are
    // supposed to run two independent engines; only a second engine for the
    // SAME user is an error.
    //
    // The old name was unqualified, so it lived in the caller's session
    // namespace: an engine in another logon session (fast user switching, an
    // RDP/console session, a scheduled task) was invisible to this check, and a
    // second engine started for the same user. On Windows SO_REUSEADDR then let
    // it bind the SAME 127.0.0.1:8737 the first was already serving; the
    // newcomer overwrote control.json with its own token while the original
    // process kept receiving every connection, so the GUI got 401 for every call
    // and showed an empty profile list with saves that silently did nothing.
    //
    // Keying the name on the user's own profile directory makes it per-user
    // across every session of that user, which is exactly the Linux/macOS
    // behaviour. A machine-wide (Global\) name would be WRONG: it would stop a
    // second user's engine from starting at all.
    //
    // The key is the user's %APPDATA% path (the same value the engine uses to
    // find its config dir), so the guard scopes precisely to the state it is
    // protecting. A deterministic hash keeps the mutex name short and free of
    // the backslashes/colons a raw path would contain (those are illegal in
    // object names). If the path cannot be resolved we fall back to the bare
    // name, which is never machine-wide, so a second user is never blocked. The
    // control-port exclusivity check in HttpServer is the independent backstop
    // if a guard is ever bypassed.
    std::wstring mutexName = L"AgentRedactor_Engine_Mutex";
    {
        const std::filesystem::path configDir = Utils::GetAppDataPath();
        if (!configDir.empty()) {
            std::wstring userKey = configDir.wstring();
            for (auto& c : userKey) c = static_cast<wchar_t>(std::towlower(c));
            // FNV-1a over the lowercased path: same user+dir always maps to the
            // same name; different users map to different names.
            uint64_t hash = 1469598103934665603ULL;
            for (wchar_t c : userKey) {
                hash ^= static_cast<uint64_t>(c);
                hash *= 1099511628211ULL;
            }
            wchar_t suffix[32] = {};
            _snwprintf_s(suffix, std::size(suffix), _TRUNCATE, L"_%016llx",
                static_cast<unsigned long long>(hash));
            mutexName += suffix;
        }
    }

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    // ERROR_ALREADY_EXISTS: this user already has an engine. ERROR_ACCESS_DENIED:
    // the existing mutex belongs to another user (its DACL does not grant us
    // access), which means this name is not ours to share — treating that as
    // "start anyway" is what the per-user naming is designed to avoid, so exit
    // quietly rather than risk a second engine on the same control port.
    if (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_ACCESS_DENIED) {
        // Another engine instance for this user is already running; exit quietly.
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
        return 0;
    }

    std::filesystem::path exePath = Utils::GetExecutablePath();
    SetCurrentDirectoryW(exePath.c_str());

    Utils::InitializeLogging({});
    LOG_LIFECYCLE(L"[engine] agentredactor.exe starting");
    if (keepConsole) printf("[engine] logging initialized\n");

    EngineApp engine;
    if (!engine.Initialize({})) {
        LOG_LIFECYCLE(L"[engine] EngineApp::Initialize FAILED");
        if (keepConsole) printf("[engine] EngineApp::Initialize FAILED\n");
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
        return 1;
    }
    if (keepConsole) printf("[engine] initialized; control API up, engine running\n");

    engine.Run();
    engine.Shutdown();

    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    LOG_LIFECYCLE(L"[engine] exiting cleanly");
    return 0;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    std::vector<std::wstring> args(argv + 1, argv + argc);

    if (args.empty()) {
        // Bare `agentredactor` with a console (terminal, execution alias,
        // double-click) means the user wants the CLI, not a hidden engine —
        // show help. Engine startup always arrives WITHOUT a console (the
        // GUI's hidden CreateProcess, the packaged startup task), so this
        // does not change autostart behavior.
        if (GetConsoleWindow() != nullptr) {
            return RunCliCommand({L"help"});
        }
        return RunEngine(false);
    }

    const std::wstring& command = args[0];
    // `engine run` / `engine stop` were removed from the CLI surface; engine
    // lifecycle belongs to the GUI (spawn on startup, stop/lock on quit).
    // Only the bare --console launch flag survives as a dev convenience.
    if (command == L"--console") {
        return RunEngine(true);
    }

    // CLI mode: status / get / set / profiles / regex / keywords / unlock /
    // password / help.
    return RunCliCommand(args);
}

