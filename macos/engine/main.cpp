// macOS entry point for the dual-mode agentredactor binary (mirror of
// linux/engine/main.cpp):
//   - no args, no terminal -> run the engine in the foreground (a launchd
//                             agent or a GUI spawn arrives this way)
//   - no args, in a terminal -> a human typed it: report engine state, start
//                               the engine detached if it is not running, and
//                               return the shell
//   - --console            -> run the engine in the foreground (debugging)
//   - uninstall [--yes]    -> remove user data
//   - any other subcommand -> CLI client over the localhost control API
//   - --selftest-migrate-settings -> headless settings-migration test hook
#include "engine_app.h"
#include "cli.h"
#include "cli_native_auth.h"
#include "control_api_client.h"
#include "utils.h"
#include "logging.h"
#include "uninstall.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <mach-o/dyld.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

using namespace AgentRedactor;

namespace {

// Single-instance guard: the Windows build uses a named mutex; here an flock
// on <configDir>/engine.lock held for the process lifetime. A second engine
// exits quietly with code 0, mirroring the Windows behavior.
int g_lockFd = -1;

bool AcquireSingleInstanceLock(const std::filesystem::path& configDir) {
    g_lockFd = open((configDir / "engine.lock").c_str(), O_RDWR | O_CREAT, 0600);
    if (g_lockFd < 0) return true; // cannot lock -> do not block startup
    if (flock(g_lockFd, LOCK_EX | LOCK_NB) != 0) {
        close(g_lockFd);
        g_lockFd = -1;
        return false;
    }
    return true;
}

int RunEngine() {
    const auto configDir = Utils::GetAppDataPath();
    Utils::CreateDirectoryRecursive(configDir);
    chmod(configDir.c_str(), 0700);

    if (!AcquireSingleInstanceLock(configDir)) {
        return 0; // another engine instance is already running
    }

    Utils::InitializeLogging({});
    LOG_LIFECYCLE(L"[engine] starting (macos)");

    EngineApp engine;
    if (!engine.Initialize({})) {
        LOG_LIFECYCLE(L"[engine] EngineApp::Initialize FAILED");
        std::fprintf(stderr, "[engine] EngineApp::Initialize FAILED\n");
        return 1;
    }
    engine.Run();
    engine.Shutdown();
    Utils::LogShutdown();
    return 0;
}

std::wstring ReadSecret(const std::wstring& prompt) {
    std::fprintf(stderr, "%s", Utils::WideToUtf8(prompt).c_str());
    std::fflush(stderr);

    const bool tty = isatty(STDIN_FILENO);
    struct termios oldt {};
    if (tty) {
        tcgetattr(STDIN_FILENO, &oldt);
        struct termios newt = oldt;
        newt.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &newt);
    }

    char* line = nullptr;
    size_t cap = 0;
    const ssize_t n = getline(&line, &cap, stdin);

    if (tty) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
        std::fprintf(stderr, "\n");
    }
    if (n <= 0 || !line) {
        free(line);
        return L"";
    }
    std::string narrow(line, static_cast<size_t>(n));
    free(line);
    while (!narrow.empty() && (narrow.back() == '\n' || narrow.back() == '\r')) narrow.pop_back();
    return Utils::Utf8ToWide(narrow);
}

int RunCliCommand(const std::vector<std::wstring>& args) {
    ControlApiClient client;
    client.Connect(Utils::GetAppDataPath());

    CliTransport transport;
    transport.get = [&client](const std::wstring& path, json& out) { return client.Get(path, out); };
    transport.post = [&client](const std::wstring& path, const json& body, json* out) { return client.Post(path, body, out); };
    transport.put = [&client](const std::wstring& path, const json& body, json* out) { return client.Put(path, body, out); };
    transport.del = [&client](const std::wstring& path) { return client.Delete(path); };

    // Native protection flow (Keychain secret behind Touch ID / the Mac
    // password, mirroring the GUI) needs a human at an interactive terminal:
    // the OS prompt is graphical and cannot be answered over a pipe. Piped or
    // scripted invocations therefore keep the typed master-password flow
    // (Linux parity) — which is also what lets the engine test suite pipe
    // passwords through stdin on macOS.
    const bool interactive = isatty(STDIN_FILENO) && isatty(STDERR_FILENO);
    if (interactive) {
        transport.nativeConsentName = L"touch id";
        transport.supportsNativeConsent = true;
        transport.consent = [&client]() {
            // LocalAuthentication presents the Touch ID / Mac password prompt
            // before the Keychain read. A missing native secret means protection
            // was enabled before native storage existed — use the typed prompt
            // for that legacy case; Keychain/authentication errors fail.
            std::string secret;
            switch (CliNativeAuth::ReadProtectionSecret(secret)) {
                case CliNativeAuth::SecretReadResult::Ok:
                    return client.UnlockWithPassword(Utils::Utf8ToWide(secret))
                        ? HelloConsentOutcome::Granted : HelloConsentOutcome::Failed;
                case CliNativeAuth::SecretReadResult::Canceled:
                    return HelloConsentOutcome::Canceled;
                case CliNativeAuth::SecretReadResult::NotFound:
                    break;
                case CliNativeAuth::SecretReadResult::Failed:
                    return HelloConsentOutcome::Failed;
            }
            const std::wstring password = ReadSecret(L"master password: ");
            if (password.empty()) return HelloConsentOutcome::Canceled;
            return client.UnlockWithPassword(password)
                ? HelloConsentOutcome::Granted : HelloConsentOutcome::Failed;
        };
        transport.createNativeSecret = [](std::wstring& secretOut, std::wstring& errorOut) {
            std::string secret;
            if (!CliNativeAuth::CreateAndStoreProtectionSecret(secret)) {
                errorOut = L"could not store the protection secret in the Keychain";
                return false;
            }
            secretOut = Utils::Utf8ToWide(secret);
            return true;
        };
        transport.deleteNativeSecret = []() { CliNativeAuth::DeleteProtectionSecret(); };
    } else {
        // No Windows Hello on macOS; piped sessions use the typed flow.
        transport.consent = []() { return HelloConsentOutcome::Unavailable; };
        transport.unlockWithPassword = [&client](const std::wstring& password) {
            return client.UnlockWithPassword(password);
        };
    }

    CliConsole console;
    console.print = [](const std::wstring& line) {
        std::fputs(Utils::WideToUtf8(line).c_str(), stdout);
        std::fputc('\n', stdout);
    };
    console.readSecret = [](const std::wstring& prompt) { return ReadSecret(prompt); };

    return RunCli(args, transport, console);
}

bool ProbeEngine(const std::filesystem::path& configDir, json& statusOut) {
    ControlApiClient probe;
    return probe.Connect(configDir) && probe.Get(L"/status", statusOut);
}

int ReadEnginePid(const std::filesystem::path& configDir) {
    try {
        std::ifstream in(configDir / "control.json");
        if (!in) return 0;
        return json::parse(in).value("pid", 0);
    } catch (...) {
        return 0;
    }
}

void PrintEngineStateLine(const char* state, int pid) {
    if (pid > 0) std::printf("Agent Redactor engine %s (pid %d).\n", state, pid);
    else std::printf("Agent Redactor engine %s.\n", state);
}

// Absolute path of this executable, symlink-resolved (macOS has no
// /proc/self/exe; _NSGetExecutablePath returns the launch-time path, which
// realpath canonicalizes — the child re-execs below need a stable path).
std::string ResolveExecutablePath() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) return "";
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return "";
    buf.resize(std::strlen(buf.c_str()));
    char resolved[PATH_MAX];
    if (realpath(buf.c_str(), resolved)) return resolved;
    return buf;
}

// Bare `agentredactor` typed in a terminal: a human is at the keys, so never
// be silent and never hold the terminal. Engine running -> say so; not
// running -> start it detached and return the shell.
int RunInteractiveStart() {
    const auto configDir = Utils::GetAppDataPath();

    json status;
    if (ProbeEngine(configDir, status)) {
        PrintEngineStateLine("is already running", ReadEnginePid(configDir));
        RunCliCommand({L"status"});
        std::fputs("Run 'agentredactor help' for available commands.\n", stdout);
        return 0;
    }

    const std::string exePath = ResolveExecutablePath();
    if (exePath.empty()) {
        std::fputs("error: could not determine the executable path\n", stderr);
        return 1;
    }

    // The child re-execs this binary with all streams detached from the
    // terminal, landing it in the non-interactive RunEngine path.
    const pid_t pid = fork();
    if (pid < 0) {
        std::fputs("error: could not start the engine (fork failed)\n", stderr);
        return 1;
    }
    if (pid == 0) {
        setsid();
        const int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        execl(exePath.c_str(), "agentredactor", static_cast<char*>(nullptr));
        _exit(1);
    }

    // First start can include the ~1.6 GB model load; allow 30 s like the GUI.
    bool up = false;
    for (int i = 0; i < 300; ++i) {
        usleep(100000);
        if (ProbeEngine(configDir, status)) { up = true; break; }
    }
    if (!up) {
        std::fprintf(stderr,
            "The engine process was started but is not responding yet.\n"
            "Check the log: %s\n",
            (configDir / "agent_redactor.log").string().c_str());
        return 1;
    }
    PrintEngineStateLine("started", ReadEnginePid(configDir));
    RunCliCommand({L"status"});
    std::fputs("Run 'agentredactor help' for available commands.\n", stdout);
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    // Upstream writes to a closed client socket must not kill the process.
    std::signal(SIGPIPE, SIG_IGN);

    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(Utils::Utf8ToWide(argv[i]));
    }

    // Headless test hook: load + migrate + save the settings for the resolved
    // config dir (AGENTREDACTOR_CONFIG_DIR honored), print a machine-readable
    // result, and exit. Deliberately runs before ANY other init so pytest can
    // drive settings-migration tests without a running engine.
    for (const auto& arg : args) {
        if (arg == L"--selftest-migrate-settings") {
            try {
                SettingsManager settings({});
                std::printf("SETTINGS_MIGRATION_OK\n");
                return 0;
            } catch (const std::exception& e) {
                std::printf("SETTINGS_MIGRATION_FAIL %s\n", e.what());
                return 1;
            } catch (...) {
                std::printf("SETTINGS_MIGRATION_FAIL unknown error\n");
                return 1;
            }
        }
    }

    if (args.empty()) {
        // A terminal means a human typed the bare command. Engine launches by
        // launchd or a GUI arrive without one and keep the quiet blocking
        // behavior; --console stays the explicit foreground mode.
        if (isatty(STDIN_FILENO) || isatty(STDOUT_FILENO)) return RunInteractiveStart();
        return RunEngine();
    }
    if (args[0] == L"--console") {
        return RunEngine();
    }
    if (args[0] == L"uninstall") {
        return RunUninstaller(args);
    }
    return RunCliCommand(args);
}
