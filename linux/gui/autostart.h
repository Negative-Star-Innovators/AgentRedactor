#pragma once

// XDG autostart for the Linux GUI: writes/removes
// $XDG_CONFIG_HOME/autostart/agentredactor.desktop (default
// ~/.config/autostart), mirroring the Windows registry Run-key toggle
// (core/include/constants.h RegisterStartupTask, which stays Windows-only).
// The engine only persists the startOnBoot setting; applying it to the
// autostart dir is GUI-side, exactly as on Windows.
//
// Under an AppImage the entry must point at $APPIMAGE (the AppImage file),
// never at the binary's own path: inside an AppImage that is the transient
// /tmp/.mount_* FUSE mount, which is gone after exit and breaks autostart
// on the next boot.

#include <filesystem>
#include <string>

namespace Autostart {

// Path of the .desktop file this install manages.
std::filesystem::path DesktopFilePath();

// Stable path launchers should exec: $APPIMAGE when running from an
// AppImage, otherwise the application file path. Returns an empty path when
// running inside an AppImage without $APPIMAGE (extract-and-run), in which
// case persistent entries must not be written.
std::filesystem::path ResolveExecPath();

// True when ResolveExecPath() yields a stable, persistent executable path.
bool HasStableExecPath();

// Exact .desktop contents SetEnabled(true) writes (also used to detect
// stale entries, e.g. ones written with an ephemeral AppImage mount path).
std::string DesktopFileContents();

bool IsEnabled();

// True only when the entry exists and matches DesktopFileContents().
bool IsUpToDate();

// True when the existing entry's Exec= path points at a real file.
bool ExistingEntryIsValid();

// Writes (ResolveExecPath() --tray-only) or removes the autostart entry.
// If no stable path is available (AppImage without $APPIMAGE), existing valid
// entries are preserved and broken ones are removed.
void SetEnabled(bool enabled);

} // namespace Autostart
