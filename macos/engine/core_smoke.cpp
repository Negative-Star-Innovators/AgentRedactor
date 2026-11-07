// Phase 1 smoke test: links against the core static library and constructs
// SettingsManager + RegexEngine against a temp config dir. Not shipped —
// built only via `cmake --build build --target core-smoke`.
#include "settings_manager.h"
#include "regex_engine.h"
#include "utils.h"
#include <cstdio>

int main() {
    using namespace AgentRedactor;
    SettingsManager settings; // honors AGENTREDACTOR_CONFIG_DIR
    RegexEngine regex;
    RegexEntry entry;
    entry.pattern = L"smoke-[0-9]+";
    regex.SetPatterns({entry});
    auto [redacted, matches] = regex.Redact(std::wstring(L"token smoke-42 here"));
    if (matches.empty()) {
        std::fprintf(stderr, "smoke: regex redaction matched nothing\n");
        return 1;
    }
    if (!Utils::FileExists(Utils::GetAppDataPath() / L"settings.json")) {
        std::fprintf(stderr, "smoke: settings.json not created\n");
        return 1;
    }
    std::fprintf(stderr, "smoke: OK (config dir: %s)\n",
        Utils::WideToUtf8(Utils::GetAppDataPath().wstring()).c_str());
    return 0;
}
