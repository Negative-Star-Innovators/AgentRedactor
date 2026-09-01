#pragma once

// XDG desktop integration for the Linux GUI: installs the launcher entry
// ($XDG_DATA_HOME/applications/agentredactor.desktop) and the hicolor icon
// set ($XDG_DATA_HOME/icons/hicolor/<N>x<N>/apps/agentredactor.png).
//
// This is what makes the fox icon show up in the GNOME dock / app switcher:
// on Wayland QApplication::setWindowIcon is ignored and the compositor
// resolves the window icon from the desktop entry matching the app-id
// (QGuiApplication::desktopFileName), so without an installed
// agentredactor.desktop + hicolor icons GNOME shows its generic gear.
//
// Files are (re)written only when missing or stale, so startup cost is a
// couple of stat/read calls once installed.

namespace DesktopIntegration {

// Installs/updates the launcher .desktop entry and hicolor icons.
// Exec points at Autostart::ResolveExecPath() ($APPIMAGE when applicable).
void EnsureInstalled();

} // namespace DesktopIntegration
