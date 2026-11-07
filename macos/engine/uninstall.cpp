#include "uninstall.h"

#include "cli_native_auth.h"
#include "model_downloader.h"
#include "utils.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace AgentRedactor {

namespace {

bool AskConfirm() {
    std::fputs("This will permanently delete Agent Redactor, its settings, logs, and downloaded models.\n", stderr);
    std::fputs("Type 'yes' to continue: ", stderr);
    std::fflush(stderr);
    char line[16] = {};
    if (!std::fgets(line, sizeof(line), stdin)) return false;
    std::string answer(line);
    while (!answer.empty() && (answer.back() == '\n' || answer.back() == '\r')) answer.pop_back();
    return answer == "yes";
}

// PIDs of running processes named exactly "agentredactor" (pgrep -x is the
// macOS counterpart of Linux pidof -x).
std::vector<pid_t> PidsOfEngine() {
    std::vector<pid_t> result;
    FILE* f = popen("pgrep -x agentredactor", "r");
    if (!f) return result;
    char buf[1024];
    while (std::fgets(buf, sizeof(buf), f)) {
        char* p = buf;
        while (*p) {
            char* end = nullptr;
            long v = std::strtol(p, &end, 10);
            if (end == p) break;
            result.push_back(static_cast<pid_t>(v));
            p = end;
        }
    }
    pclose(f);
    return result;
}

void StopOtherInstances() {
    const pid_t self = getpid();
    std::vector<pid_t> targets;
    for (pid_t pid : PidsOfEngine()) {
        if (pid != self && pid > 0) targets.push_back(pid);
    }
    if (targets.empty()) return;

    std::fprintf(stderr, "Stopping running Agent Redactor processes...\n");
    for (pid_t pid : targets) ::kill(pid, SIGTERM);

    // Give them a moment to shut down gracefully.
    for (int i = 0; i < 20; ++i) {
        bool anyAlive = false;
        for (pid_t pid : targets) {
            if (::kill(pid, 0) == 0) anyAlive = true;
        }
        if (!anyAlive) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (pid_t pid : targets) {
        if (::kill(pid, 0) == 0) ::kill(pid, SIGKILL);
    }
}

void RemoveAll(const fs::path& p) {
    std::error_code ec;
    if (fs::remove_all(p, ec) > 0 && !ec) std::fprintf(stderr, "Removed: %s\n", p.string().c_str());
}

} // namespace

int RunUninstaller(const std::vector<std::wstring>& args) {
    bool force = false;
    for (const auto& a : args) {
        if (a == L"--yes" || a == L"-y") force = true;
    }

    if (!force && !AskConfirm()) {
        std::fputs("Uninstall cancelled.\n", stderr);
        return 1;
    }

    StopOtherInstances();

    // User data (settings, logs, sessions, machine.key). GetAppDataPath covers
    // ~/Library/Application Support/AgentRedactor and the
    // AGENTREDACTOR_CONFIG_DIR override; downloaded model weights live in
    // GetFallbackModelDir() (nested inside the same folder on macOS, removed
    // twice harmlessly).
    RemoveAll(Utils::GetAppDataPath());
    RemoveAll(ModelDownloader::GetFallbackModelDir());
    CliNativeAuth::DeleteProtectionSecret();

    std::fputs("Agent Redactor has been uninstalled.\n", stderr);
    return 0;
}

} // namespace AgentRedactor
