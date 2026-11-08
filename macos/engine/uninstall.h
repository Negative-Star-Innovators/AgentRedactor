#pragma once

#include <string>
#include <vector>

namespace AgentRedactor {

// Handles `agentredactor uninstall` on macOS.
// Stops running Agent Redactor engine processes and removes the user data
// (settings, logs, machine key, downloaded models).
// Returns the process exit code.
int RunUninstaller(const std::vector<std::wstring>& args);

} // namespace AgentRedactor
