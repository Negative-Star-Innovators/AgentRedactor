#include "autostart.h"

#include <cstdlib>
#include <fstream>

namespace {

std::filesystem::path AutostartDir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "autostart";
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".config" / "autostart";
}

} // namespace

namespace Autostart {

std::filesystem::path DesktopFilePath() {
    return AutostartDir() / "agentredactor.desktop";
}

bool IsEnabled() {
    return std::filesystem::exists(DesktopFilePath());
}

void SetEnabled(bool enabled, const std::filesystem::path& execPath) {
    const auto path = DesktopFilePath();
    if (!enabled) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return;
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::trunc);
    f << "[Desktop Entry]\n"
      << "Type=Application\n"
      << "Name=Agent Redactor\n"
      << "Exec=" << execPath.string() << " --tray-only\n"
      << "X-GNOME-Autostart-enabled=true\n";
    // Desktop entries are user config, not secrets; 644 is conventional.
}

} // namespace Autostart
