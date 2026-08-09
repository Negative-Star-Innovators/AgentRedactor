#include "migrations/settings_migrator.h"

namespace AgentRedactor {
namespace SettingsMigrator {
namespace {

using json = nlohmann::json;
using MigrationStep = void (*)(json&);

void Log(const std::function<void(const std::wstring&)>& log, const std::wstring& message) {
    if (log) {
        try { log(message); } catch (...) {}
    }
}

// ---------------------------------------------------------------------------
// Migration steps. Each step upgrades the settings JSON from version N to
// N + 1. Steps run in table order; never edit or reorder steps that have
// shipped — older installs depend on replaying them verbatim.
// ---------------------------------------------------------------------------

// 1 -> 2: rename the legacy "verbose_logging" flag to "logging_enabled"
// (an already-present logging_enabled value wins).
void Migrate1To2(json& settings) {
    if (settings.contains("verbose_logging")) {
        if (!settings.contains("logging_enabled")) {
            settings["logging_enabled"] = settings["verbose_logging"];
        }
        settings.erase("verbose_logging");
    }
}

struct Migration {
    int fromVersion;
    MigrationStep apply;
};

// Ordered migration table. ADD FUTURE MIGRATIONS HERE: write a MigrateNToN+1
// function above, append `{ N, &MigrateNToNPlus1 },` to this table, and bump
// SETTINGS_SCHEMA_VERSION in include/migrations/settings_migrator.h.
const Migration kMigrations[] = {
    { 1, &Migrate1To2 },
};

} // anonymous namespace

bool MigrateInPlace(json& settings, std::function<void(const std::wstring&)> log) {
    if (!settings.is_object()) {
        // Defensive: treat a non-object document as empty, schema-current
        // settings; the caller's default-insertion fills in the gaps.
        settings = json::object();
    }

    int version = 1; // schema before versioning existed
    if (settings.contains("settings_version") && settings["settings_version"].is_number_integer()) {
        version = settings["settings_version"].get<int>();
    }

    if (version > SETTINGS_SCHEMA_VERSION) {
        Log(log, L"[SettingsMigrator] settings_version " + std::to_wstring(version) +
            L" is newer than this build (schema " + std::to_wstring(SETTINGS_SCHEMA_VERSION) +
            L"); leaving the file untouched");
        return false;
    }

    bool changed = false;
    for (const auto& step : kMigrations) {
        if (step.fromVersion >= version) {
            Log(log, L"[SettingsMigrator] Migrating settings schema " +
                std::to_wstring(step.fromVersion) + L" -> " + std::to_wstring(step.fromVersion + 1));
            step.apply(settings);
            changed = true;
        }
    }

    if (version != SETTINGS_SCHEMA_VERSION) {
        settings["settings_version"] = SETTINGS_SCHEMA_VERSION;
        changed = true;
    }
    return changed;
}

} // namespace SettingsMigrator
} // namespace AgentRedactor
