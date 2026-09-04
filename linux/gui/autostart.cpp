#include "autostart.h"

#include <QCoreApplication>
#include <QtDebug>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

std::filesystem::path AutostartDir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "autostart";
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".config" / "autostart";
}

// Quote one argument for a desktop-entry Exec line: wrap in double quotes and
// backslash-escape the four chars that are special inside them.
std::string QuoteExecArg(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') out += '\\';
        out += c;
    }
    return out + '"';
}

} // namespace

namespace Autostart {

std::filesystem::path DesktopFilePath() {
    return AutostartDir() / "agentredactor.desktop";
}

// The path a launcher entry must point at. QCoreApplication::applicationFilePath
// resolves /proc/self/exe, which inside an AppImage is the ephemeral
// /tmp/.mount_* FUSE path — dead the moment the app exits, so an autostart
// entry written with it never survives a reboot. $APPIMAGE (set by the
// AppImage runtime) is the stable path to the AppImage file itself, the same
// value the ~/.local/bin/agentredactor CLI wrapper uses.
std::filesystem::path ResolveExecPath() {
    if (const char* appImage = std::getenv("APPIMAGE"); appImage && *appImage) {
        return std::filesystem::path(appImage);
    }
    // If $APPIMAGE is missing but we are running from an AppImage mount, there
    // is no stable path to persist. Return empty so callers can skip writing
    // entries that would break on the next launch.
    const std::string exe = QCoreApplication::applicationFilePath().toStdString();
    if (exe.rfind("/tmp/.mount_", 0) == 0) {
        return {};
    }
    return std::filesystem::path(exe);
}

bool HasStableExecPath() {
    return !ResolveExecPath().empty();
}

std::string DesktopFileContents() {
    std::ostringstream ss;
    ss << "[Desktop Entry]\n"
       << "Type=Application\n"
       << "Name=Agent Redactor\n"
       << "Exec=" << QuoteExecArg(ResolveExecPath().string()) << " --tray-only\n"
       // Resolved from the hicolor set installed by DesktopIntegration.
       << "Icon=agentredactor\n"
       << "X-GNOME-Autostart-enabled=true\n";
    // Desktop entries are user config, not secrets; 644 is conventional.
    return ss.str();
}

bool IsEnabled() {
    return std::filesystem::exists(DesktopFilePath());
}

bool IsUpToDate() {
    std::ifstream in(DesktopFilePath(), std::ios::binary);
    if (!in) return false;
    return std::string(std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()) == DesktopFileContents();
}

// Parse the Exec= line from an existing .desktop file and return the first
// quoted/unquoted argument (the executable path). Returns empty on parse failure.
std::filesystem::path ReadExistingExecPath() {
    std::ifstream in(DesktopFilePath(), std::ios::binary);
    if (!in) return {};
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("Exec=", 0) != 0) continue;
        std::string rest = line.substr(5);
        // Strip trailing \r if the file uses CRLF.
        if (!rest.empty() && rest.back() == '\r') rest.pop_back();
        std::string path;
        bool inQuotes = false;
        for (size_t i = 0; i < rest.size(); ++i) {
            char c = rest[i];
            if (c == '\\' && i + 1 < rest.size()) {
                path += rest[++i];
                continue;
            }
            if (c == '"') {
                inQuotes = !inQuotes;
                continue;
            }
            if (!inQuotes && (c == ' ' || c == '\t')) break;
            path += c;
        }
        return std::filesystem::path(path);
    }
    return {};
}

bool ExistingEntryIsValid() {
    const auto exec = ReadExistingExecPath();
    if (exec.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(exec, ec) &&
           std::filesystem::is_regular_file(exec, ec);
}

void SetEnabled(bool enabled) {
    const auto path = DesktopFilePath();
    if (!enabled) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return;
    }
    if (!HasStableExecPath()) {
        // Running inside an AppImage without $APPIMAGE (e.g. extract-and-run).
        // Do not overwrite an existing stable entry with a transient path, and
        // do not create a broken new one. If the existing entry already points
        // at a missing file, remove it so the user is not left with a dead
        // launcher.
        if (std::filesystem::exists(path)) {
            if (ExistingEntryIsValid()) {
                qInfo("[Autostart] Keeping existing autostart entry (no $APPIMAGE this launch)");
            } else {
                qWarning("[Autostart] Removing stale autostart entry: executable no longer exists");
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } else {
            qWarning("[Autostart] Skipping autostart entry: no stable AppImage path ($APPIMAGE missing)");
        }
        return;
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::trunc);
    f << DesktopFileContents();
}

} // namespace Autostart
