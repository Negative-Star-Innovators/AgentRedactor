#pragma once

#include <functional>
#include <string>
#include <nlohmann/json.hpp>

namespace AgentRedactor {

// Current settings.json schema version. The original, unversioned layout is
// version 1. When the schema changes: bump this constant, add a step in
// src/migrations/settings_migrator.cpp, and add a fixture under
// tests/migration/fixtures/settings/ (see "Changing the settings schema" in
// AGENTS.md).
inline constexpr int SETTINGS_SCHEMA_VERSION = 2;

namespace SettingsMigrator {

// Applies every schema migration to `settings` in place, starting from
// settings["settings_version"] (absent == 1, the pre-versioning schema) up to
// SETTINGS_SCHEMA_VERSION, then stamps settings_version = SETTINGS_SCHEMA_VERSION.
// Returns true when anything changed, so the caller can persist. A file whose
// version is NEWER than this build is left untouched (returns false). `log`
// receives human-readable progress lines and may be empty.
bool MigrateInPlace(nlohmann::json& settings, std::function<void(const std::wstring&)> log);

} // namespace SettingsMigrator
} // namespace AgentRedactor
