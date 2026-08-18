#pragma once

// XDG autostart for the Linux GUI: writes/removes
// $XDG_CONFIG_HOME/autostart/agentredactor.desktop (default
// ~/.config/autostart), mirroring the Windows registry Run-key toggle
// (core/include/constants.h RegisterStartupTask, which stays Windows-only).
// The engine only persists the startOnBoot setting; applying it to the
// autostart dir is GUI-side, exactly as on Windows.

#include <filesystem>

namespace Autostart {

// Path of the .desktop file this install manages.
std::filesystem::path DesktopFilePath();

bool IsEnabled();

// Writes (execPath --tray-only) or removes the autostart entry.
void SetEnabled(bool enabled, const std::filesystem::path& execPath);

} // namespace Autostart
