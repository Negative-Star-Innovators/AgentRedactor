#pragma once

#include <string>
#include <vector>

namespace AgentRedactor {

// Handles `agentredactor uninstall` on Linux.
// Stops running Agent Redactor processes, removes desktop integration files,
// user data, and (when running from an AppImage) deletes the AppImage itself.
// Returns the process exit code.
int RunUninstaller(const std::vector<std::wstring>& args);

} // namespace AgentRedactor
