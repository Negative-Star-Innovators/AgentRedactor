#include "desktop_integration.h"

#include "autostart.h"

#include <QFile>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

// Keep in sync with resources.qrc (:/icons/agentredactor-<N>.png).
constexpr int kIconSizes[] = {16, 32, 48, 64, 128, 256, 512};

std::filesystem::path DataHome() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg);
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".local" / "share";
}

// Same desktop-entry quoting as autostart.cpp: wrap in double quotes and
// backslash-escape the four chars that are special inside them.
std::string QuoteExecArg(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') out += '\\';
        out += c;
    }
    return out + '"';
}

// Writes content to path only when missing or different; creates parents.
void WriteIfStale(const std::filesystem::path& path, const std::string& content) {
    if (std::ifstream in(path, std::ios::binary); in) {
        const std::string existing((std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        if (existing == content) return;
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

void InstallIcons() {
    const auto hicolor = DataHome() / "icons" / "hicolor";
    for (const int size : kIconSizes) {
        QFile res(QStringLiteral(":/icons/agentredactor-%1.png").arg(size));
        if (!res.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = res.readAll();
        WriteIfStale(
            hicolor / QStringLiteral("%1x%1").arg(size).toStdString() / "apps" / "agentredactor.png",
            std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
    }
}

void InstallDesktopEntry() {
    const auto execPath = Autostart::ResolveExecPath();
    if (execPath.empty()) {
        qWarning("[DesktopIntegration] Skipping applications-menu entry: no stable AppImage path ($APPIMAGE missing)");
        return;
    }
    std::ostringstream ss;
    ss << "[Desktop Entry]\n"
       << "Type=Application\n"
       << "Name=Agent Redactor\n"
       << "Comment=Local PII redaction proxy for AI agents\n"
       << "Exec=" << QuoteExecArg(execPath.string()) << "\n"
       << "Icon=agentredactor\n"
       << "Terminal=false\n"
       << "Categories=Utility;Security;\n"
       // Matches QGuiApplication::desktopFileName / the Wayland app-id so the
       // compositor associates windows with this entry (dock icon, grouping).
       << "StartupWMClass=agentredactor\n";
    WriteIfStale(DataHome() / "applications" / "agentredactor.desktop", ss.str());
}

} // namespace

namespace DesktopIntegration {

void EnsureInstalled() {
    // Best-effort: a read-only home or missing resource must never stop the
    // app from starting.
    try {
        InstallIcons();
        InstallDesktopEntry();
    } catch (...) {
    }
}

} // namespace DesktopIntegration
