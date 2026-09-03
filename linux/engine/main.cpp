// Linux entry point for the dual-mode agentredactor binary (mirror of
// windows/engine/main.cpp):
//   - no args / --console  -> run the engine in the foreground (the GUI and
//                             the systemd --user unit launch it detached)
//   - any other subcommand -> CLI client over the localhost control API
//   - --selftest-migrate-settings -> headless settings-migration test hook
#include "engine_app.h"
#include "cli.h"
#include "control_api_client.h"
#include "utils.h"
#include "logging.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
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
    LOG_LIFECYCLE(L"[engine] starting (linux)");

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
    // No Windows Hello on Linux; the typed master password flow is used instead.
    transport.consent = []() { return HelloConsentOutcome::Unavailable; };
    transport.unlockWithPassword = [&client](const std::wstring& password) {
        return client.UnlockWithPassword(password);
    };

    CliConsole console;
    console.print = [](const std::wstring& line) {
        std::fputs(Utils::WideToUtf8(line).c_str(), stdout);
        std::fputc('\n', stdout);
    };
    console.readSecret = [](const std::wstring& prompt) { return ReadSecret(prompt); };

    return RunCli(args, transport, console);
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

    if (args.empty() || args[0] == L"--console") {
        return RunEngine();
    }
    return RunCliCommand(args);
}
