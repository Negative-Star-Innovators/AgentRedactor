#include "uninstall.h"

#include "utils.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <signal.h>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace AgentRedactor {

namespace {

// Keep in sync with linux/gui/desktop_integration.cpp.
constexpr int kIconSizes[] = {16, 32, 48, 64, 128, 256, 512};

fs::path HomeDir() {
    if (const char* home = std::getenv("HOME"); home && *home) {
        return fs::path(home);
    }
    return fs::path(".");
}

std::string GetEnvUtf8(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

std::optional<fs::path> AppImagePath() {
    std::string p = GetEnvUtf8("APPIMAGE");
    if (!p.empty()) return fs::path(p);
    return std::nullopt;
}

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

std::vector<pid_t> PidsOf(const char* name) {
    std::vector<pid_t> result;
    std::string cmd = "pidof -x ";
    cmd += name;
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return result;
    char buf[1024];
    if (std::fgets(buf, sizeof(buf), f)) {
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
    // "agentredactor-gui" is the bash wrapper in the AppImage/appdir layout;
    // the process it exec's is "agentredactor-gui.real", so match both.
    for (const char* name : {"agentredactor-gui", "agentredactor-gui.real", "agentredactor"}) {
        for (pid_t pid : PidsOf(name)) {
            if (pid != self && pid > 0) targets.push_back(pid);
        }
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

void RemoveFile(const fs::path& p) {
    std::error_code ec;
    // The error_code overload treats a missing file as success (returns false,
    // ec clear), so check the return value to avoid printing bogus removals.
    if (fs::remove(p, ec) && !ec) std::fprintf(stderr, "Removed: %s\n", p.string().c_str());
}

void RemoveAll(const fs::path& p) {
    std::error_code ec;
    if (fs::remove_all(p, ec) > 0 && !ec) std::fprintf(stderr, "Removed: %s\n", p.string().c_str());
}

std::string Md5OfUri(const std::string& uri) {
    std::string cmd = "printf '%s' \"";
    // Simple escaping for shell double quotes: only backslash and double quote are special.
    for (char c : uri) {
        if (c == '\\' || c == '"' || c == '$' || c == '`') cmd += '\\';
        cmd += c;
    }
    cmd += "\" | md5sum | cut -d' ' -f1";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return {};
    char buf[64] = {};
    if (std::fgets(buf, sizeof(buf), f)) {
        std::string s(buf);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        pclose(f);
        return s;
    }
    pclose(f);
    return {};
}

void RemoveSystemdUnit() {
    // install.sh (headless) registers the engine as a systemd user service;
    // tear it down too - otherwise a unit pointing at the deleted AppImage
    // would restart a missing binary on every boot. Best-effort throughout:
    // systems without systemd simply no-op here.
    std::error_code ec;
    const fs::path unit = HomeDir() / ".config" / "systemd" / "user" / "agentredactor.service";
    if (!fs::exists(unit, ec)) return;
    std::fprintf(stderr, "Removing systemd user service...\n");
    std::system("systemctl --user disable --now agentredactor.service >/dev/null 2>&1");
    RemoveFile(unit);
    std::system("systemctl --user daemon-reload >/dev/null 2>&1");
}

void RemoveThumbnails(const fs::path& appImage) {    std::string uri = "file://" + appImage.string();
    std::string md5 = Md5OfUri(uri);
    if (md5.empty()) return;
    const fs::path cache = HomeDir() / ".cache" / "thumbnails";
    RemoveFile(cache / "normal" / (md5 + ".png"));
    RemoveFile(cache / "large" / (md5 + ".png"));
}

void ScheduleSelfDelete(const fs::path& appImage) {
    // The AppImage is the running executable. Linux allows unlinking it now;
    // the inode is kept alive until the process exits, then the space is freed.
    std::error_code ec;
    fs::remove(appImage, ec);
    if (!ec) {
        std::fprintf(stderr, "Removed AppImage: %s\n", appImage.string().c_str());
        // If the containing directory is now empty (e.g. ~/Applications was created just for us),
        // remove it too.
        fs::path dir = appImage.parent_path();
        if (fs::is_directory(dir, ec)) {
            fs::directory_iterator end;
            fs::directory_iterator it(dir, ec);
            if (!ec && it == end) {
                fs::remove(dir, ec);
            }
        }
        return;
    }

    // Fallback: spawn a tiny detached shell that waits for this process to exit
    // and then deletes the AppImage.
    std::fprintf(stderr, "Could not remove AppImage immediately (%s); scheduling deletion on exit...\n",
        ec.message().c_str());
    std::string quoted;
    for (char c : appImage.string()) {
        if (c == '\\' || c == '"' || c == '$' || c == '`') quoted += '\\';
        quoted += c;
    }
    std::string script = "(sleep 2; rm -f \"" + quoted + "\") >/dev/null 2>&1 &";
    if (std::system(script.c_str()) < 0) {
        std::fprintf(stderr, "Warning: failed to schedule AppImage deletion\n");
    }
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
    RemoveSystemdUnit();

    const fs::path home = HomeDir();
    const auto appImage = AppImagePath();

    // Desktop integration.
    RemoveFile(home / ".local" / "share" / "applications" / "agentredactor.desktop");
    RemoveFile(home / ".config" / "autostart" / "agentredactor.desktop");
    RemoveFile(home / ".local" / "bin" / "agentredactor");

    // Engine stdout log from installs that started the engine directly
    // (no systemd user session).
    RemoveAll(home / ".local" / "state" / "agentredactor");

    // Icons.
    for (int size : kIconSizes) {
        fs::path iconDir = home / ".local" / "share" / "icons" / "hicolor" /
                           (std::to_string(size) + "x" + std::to_string(size)) / "apps";
        RemoveFile(iconDir / "agentredactor.png");
    }

    // User data (settings, logs, sessions). GetAppDataPath covers the XDG and
    // AGENTREDACTOR_CONFIG_DIR locations; the app never writes to
    // ~/.config/AgentRedactor.
    RemoveAll(Utils::GetAppDataPath());
    RemoveAll(home / ".local" / "share" / "agentredactor");

    // Thumbnail cache (only meaningful when running from an AppImage).
    if (appImage) RemoveThumbnails(*appImage);

    // Finally, the AppImage itself.
    if (appImage) {
        ScheduleSelfDelete(*appImage);
    } else {
        std::fputs("Not running from an AppImage; skipping AppImage file deletion.\n", stderr);
    }

    std::fputs("Agent Redactor has been uninstalled.\n", stderr);
    return 0;
}

} // namespace AgentRedactor
