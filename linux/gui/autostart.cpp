#include "autostart.h"

#include <QCoreApplication>

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
    return QCoreApplication::applicationFilePath().toStdString();
}

std::string DesktopFileContents() {
    std::ostringstream ss;
    ss << "[Desktop Entry]\n"
       << "Type=Application\n"
       << "Name=Agent Redactor\n"
       << "Exec=" << QuoteExecArg(ResolveExecPath().string()) << " --tray-only\n"
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

void SetEnabled(bool enabled) {
    const auto path = DesktopFilePath();
    if (!enabled) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return;
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::trunc);
    f << DesktopFileContents();
}

} // namespace Autostart
