#include "settings_manager.h"
#include "constants.h"
#include "logging.h"
#include "migrations/settings_migrator.h"
#include <chrono>
#include <ctime>
#include <fstream>

namespace AgentRedactor {

SettingsManager::SettingsManager(const std::filesystem::path& configDir) {
    if (configDir.empty()) {
        configDir_ = Utils::GetAppDataPath();
    } else {
        configDir_ = configDir;
    }
    Utils::CreateDirectoryRecursive(configDir_);
    settingsFile_ = configDir_ / SETTINGS_FILE;
    LoadSettings();
    // Run schema migrations. A file reset after corruption is schema-current
    // (empty), so the migrator only stamps settings_version there.
    if (SettingsMigrator::MigrateInPlace(settings_, [](const std::wstring& msg) {
            LOG_LIFECYCLE(msg);
        })) {
        SaveSettings();
    }
    if (!settings_.contains("start_on_boot")) {
        settings_["start_on_boot"] = true;
        SaveSettings();
    }
    // NOTE: the legacy verbose_logging -> logging_enabled rename lives in
    // migration 1 -> 2 (src/migrations/settings_migrator.cpp), not here.
    if (!settings_.contains("logging_enabled")) {
        settings_["logging_enabled"] = false;
        SaveSettings();
    }
    if (!settings_.contains("app_language")) {
        settings_["app_language"] = "";
        SaveSettings();
    }
}

bool SettingsManager::IsStartOnBoot() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (settings_.contains("start_on_boot")) return settings_["start_on_boot"].get<bool>();
    return true;
}

void SettingsManager::SetStartOnBoot(bool enabled) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    settings_["start_on_boot"] = enabled;
    SaveSettings();
}

std::wstring SettingsManager::GetOnnxProvider() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (settings_.contains("onnx_provider")) return Utils::Utf8ToWide(settings_["onnx_provider"].get<std::string>());
    return L"auto";
}

void SettingsManager::SetOnnxProvider(const std::wstring& provider) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    settings_["onnx_provider"] = Utils::WideToUtf8(provider);
    SaveSettings();
}

bool SettingsManager::IsLoggingEnabled() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (settings_.contains("logging_enabled")) return settings_["logging_enabled"].get<bool>();
    return false;
}

void SettingsManager::SetLoggingEnabled(bool enabled) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    settings_["logging_enabled"] = enabled;
    SaveSettings();
}

std::wstring SettingsManager::GetAppLanguage() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (settings_.contains("app_language")) return Utils::Utf8ToWide(settings_["app_language"].get<std::string>());
    return L"";
}

void SettingsManager::SetAppLanguage(const std::wstring& language) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    settings_["app_language"] = Utils::WideToUtf8(language);
    SaveSettings();
}

std::vector<ApiKeyProfile> SettingsManager::GetProfiles() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<ApiKeyProfile> profiles;
    if (settings_.contains("profiles")) {
        for (const auto& p : settings_["profiles"]) {
            profiles.push_back(ApiKeyProfile::FromJson(p));
        }
    }
    return profiles;
}

void SettingsManager::SetProfiles(const std::vector<ApiKeyProfile>& profiles) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    json oldProfiles = settings_.value("profiles", json::array());
    LOGF_LIFECYCLE(L"[SettingsManager] SetProfiles input count=%zu old profiles size=%zu",
        profiles.size(), oldProfiles.size());
    json arr = json::array();
    for (const auto& p : profiles) {
        LOGF_LIFECYCLE(L"[SettingsManager] SetProfiles profile id=%s keywords=%zu unavailable=%d",
            p.id.c_str(), p.keywords.size(), p.keywordsUnavailable);
        json pj;
        p.ToJson(pj);
        // If decryption failed for a sensitive field, keep the existing encrypted
        // blob instead of replacing it with an encrypted empty string.
        if (p.apiKeyUnavailable || p.keywordsUnavailable || p.regexPatternsUnavailable) {
            const json* old = nullptr;
            for (const auto& o : oldProfiles) {
                if (o.value("id", std::string()) == Utils::WideToUtf8(p.id)) {
                    old = &o;
                    break;
                }
            }
            if (old) {
                if (p.apiKeyUnavailable && old->contains("api_key")) {
                    pj["api_key"] = (*old)["api_key"];
                }
                if (p.keywordsUnavailable && old->contains("keywords")) {
                    pj["keywords"] = (*old)["keywords"];
                }
                if (p.regexPatternsUnavailable && old->contains("regex_patterns")) {
                    pj["regex_patterns"] = (*old)["regex_patterns"];
                }
            }
        }
        arr.push_back(std::move(pj));
    }
    settings_["profiles"] = arr;
    SaveSettings();
    LOGF_LIFECYCLE(L"[SettingsManager] SetProfiles after save keywords in settings=%zu",
        settings_.value("profiles", json::array()).at(0).value("keywords", json::array()).size());
}

void SettingsManager::AddProfile(const ApiKeyProfile& profile) {
    auto profiles = GetProfiles();
    profiles.push_back(profile);
    SetProfiles(profiles);
}

void SettingsManager::UpdateProfile(const ApiKeyProfile& profile) {
    auto profiles = GetProfiles();
    for (auto& p : profiles) {
        if (p.id == profile.id) {
            p = profile;
            break;
        }
    }
    SetProfiles(profiles);
}

void SettingsManager::RemoveProfile(const std::wstring& id) {
    auto profiles = GetProfiles();
    profiles.erase(std::remove_if(profiles.begin(), profiles.end(),
        [&id](const ApiKeyProfile& p) { return p.id == id; }), profiles.end());
    SetProfiles(profiles);
}

std::optional<ApiKeyProfile> SettingsManager::GetProfileById(const std::wstring& id) const {
    auto profiles = GetProfiles();
    for (const auto& p : profiles) {
        if (p.id == id) return p;
    }
    return std::nullopt;
}

std::optional<ApiKeyProfile> SettingsManager::GetProfileByPort(int port) const {
    auto profiles = GetProfiles();
    for (const auto& p : profiles) {
        if (p.port == port) return p;
    }
    return std::nullopt;
}

// ============================================================================
// SecureStorage integration
// ============================================================================

bool SettingsManager::IsMasterPasswordEnabled() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return secureStorage_.IsMasterPasswordEnabled();
}

bool SettingsManager::IsUnlocked() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return secureStorage_.IsInitialized();
}

bool SettingsManager::IsHelloEnabled() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return secureStorage_.IsHelloEnabled();
}

#ifdef _WIN32
bool SettingsManager::EnableMasterPassword() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!secureStorage_.EnableMasterPassword()) return false;
    settings_["master_password"] = secureStorage_.GetConfig();
    SaveSettings();
    return true;
}
#else
bool SettingsManager::EnableMasterPassword(const std::wstring& password) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!secureStorage_.EnableMasterPassword(password)) return false;
    settings_["master_password"] = secureStorage_.GetConfig();
    SaveSettings();
    return true;
}
#endif

void SettingsManager::DisableMasterPassword() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    secureStorage_.DisableMasterPassword();
    settings_["master_password"] = secureStorage_.GetConfig();
    SaveSettings();
}

#ifdef _WIN32
bool SettingsManager::UnlockWithHello() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!secureStorage_.IsMasterPasswordEnabled()) return true;
    if (!secureStorage_.UnlockWithHello()) return false;
    DecryptSensitiveFields();
    return true;
}
#else
bool SettingsManager::UnlockWithPassword(const std::wstring& password) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!secureStorage_.IsMasterPasswordEnabled()) return true;
    if (!secureStorage_.UnlockWithPassword(password)) return false;
    DecryptSensitiveFields();
    return true;
}
#endif

void SettingsManager::Lock() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!secureStorage_.IsMasterPasswordEnabled()) return;
    secureStorage_.Lock();
}

// ============================================================================
// Save / Load with encryption
// ============================================================================

void SettingsManager::EncryptSensitiveFields() {
    if (!settings_.contains("profiles")) return;
    for (auto& profile : settings_["profiles"]) {
        if (profile.contains("api_key") && profile["api_key"].is_string()) {
            auto& field = profile["api_key"];
            std::wstring plaintext = Utils::Utf8ToWide(field.get<std::string>());
            field = secureStorage_.Encrypt(plaintext);
        }
        if (profile.contains("keywords") && profile["keywords"].is_array()) {
            auto& field = profile["keywords"];
            std::wstring plaintext = Utils::Utf8ToWide(field.dump());
            LOGF_LIFECYCLE(L"[SettingsManager] EncryptSensitiveFields keywords plaintext size=%zu", field.size());
            field = secureStorage_.Encrypt(plaintext);
            LOGF_LIFECYCLE(L"[SettingsManager] EncryptSensitiveFields keywords encrypted is_object=%d", field.is_object());
        }
        if (profile.contains("regex_patterns") && profile["regex_patterns"].is_array()) {
            auto& field = profile["regex_patterns"];
            std::wstring plaintext = Utils::Utf8ToWide(field.dump());
            field = secureStorage_.Encrypt(plaintext);
        }
    }
}

void SettingsManager::DecryptSensitiveFields() {
    if (!settings_.contains("profiles")) return;
    for (auto& profile : settings_["profiles"]) {
        if (profile.contains("api_key") && profile["api_key"].is_object() && profile["api_key"].contains("_enc")) {
            auto& field = profile["api_key"];
            auto decrypted = secureStorage_.Decrypt(field);
            if (decrypted) {
                field = Utils::WideToUtf8(*decrypted);
            }
        }
        if (profile.contains("keywords") && profile["keywords"].is_object() && profile["keywords"].contains("_enc")) {
            auto& field = profile["keywords"];
            auto decrypted = secureStorage_.Decrypt(field);
            LOGF_LIFECYCLE(L"[SettingsManager] DecryptSensitiveFields keywords decrypted=%d", decrypted.has_value());
            if (decrypted) {
                try {
                    field = json::parse(Utils::WideToUtf8(*decrypted));
                    LOGF_LIFECYCLE(L"[SettingsManager] DecryptSensitiveFields keywords parsed size=%zu", field.size());
                } catch (...) {
                    field = json::array();
                    LOGF_LIFECYCLE(L"[SettingsManager] DecryptSensitiveFields keywords parse failed");
                }
            }
        }
        if (profile.contains("regex_patterns") && profile["regex_patterns"].is_object() && profile["regex_patterns"].contains("_enc")) {
            auto& field = profile["regex_patterns"];
            auto decrypted = secureStorage_.Decrypt(field);
            if (decrypted) {
                try {
                    field = json::parse(Utils::WideToUtf8(*decrypted));
                } catch (...) {
                    field = json::array();
                }
            }
        }
    }
}

void SettingsManager::SaveSettings() {
    struct EncryptionGuard {
        SettingsManager* sm;
        EncryptionGuard(SettingsManager* s) : sm(s) { sm->EncryptSensitiveFields(); }
        ~EncryptionGuard() { sm->DecryptSensitiveFields(); }
    };

    try {
        const auto& kwBefore = settings_.value("profiles", json::array()).at(0).value("keywords", json::array());
        LOGF_LIFECYCLE(L"[SettingsManager] SaveSettings before keywords is_array=%d size=%zu",
            kwBefore.is_array(), kwBefore.size());
        EncryptionGuard guard(this);
        const auto& kwAfterEnc = settings_.value("profiles", json::array()).at(0).find("keywords");
        const bool kwIsArrayAfterEnc = kwAfterEnc != settings_.value("profiles", json::array()).at(0).end() && kwAfterEnc->is_array();
        LOGF_LIFECYCLE(L"[SettingsManager] SaveSettings after encrypt keywords is_array=%d", kwIsArrayAfterEnc);
        std::ofstream file(settingsFile_);
        if (file) {
            file << settings_.dump(2);
        }
    } catch (const std::exception& e) {
        LOGF_LIFECYCLE(L"[SettingsManager] Save error: %s", Utils::Utf8ToWide(e.what()).c_str());
    }
}

void SettingsManager::LoadSettings() {
    try {
        if (!Utils::FileExists(settingsFile_)) {
            settings_ = json::object();
            secureStorage_.Initialize(json::object());
            return;
        }
        std::ifstream file(settingsFile_);
        if (file) {
            file >> settings_;
        }

        // Initialize SecureStorage from master_password config
        json mpConfig;
        if (settings_.contains("master_password")) {
            mpConfig = settings_["master_password"];
        }
        secureStorage_.Initialize(mpConfig);

        // If protection is not enabled, decrypt immediately with DPAPI
        if (!secureStorage_.IsMasterPasswordEnabled()) {
            DecryptSensitiveFields();
        }
        // If protection is enabled, decryption is deferred until
        // UnlockWithHello is called (Windows Hello only).
    } catch (const std::exception& e) {
        LOGF_LIFECYCLE(L"[SettingsManager] Load error: %s", Utils::Utf8ToWide(e.what()).c_str());
        BackupCorruptSettingsFile();
        settings_ = json::object();
        secureStorage_.Initialize(json::object());
    }
}

// Called before a corrupt settings file is reset to {}: copies the unreadable
// file to <name>.corrupt-<timestamp>.bak so user data is never silently
// destroyed. Never throws.
void SettingsManager::BackupCorruptSettingsFile() {
    try {
        if (settingsFile_.empty() || !Utils::FileExists(settingsFile_)) return;
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &t);
        wchar_t stamp[32] = {};
        std::wcsftime(stamp, sizeof(stamp) / sizeof(stamp[0]), L"%Y%m%d-%H%M%S", &tm);
        auto backup = settingsFile_;
        backup += L".corrupt-";
        backup += stamp;
        backup += L".bak";
        std::error_code ec;
        std::filesystem::copy_file(settingsFile_, backup, ec);
        if (ec) {
            LOGF_LIFECYCLE(L"[SettingsManager] FAILED to back up corrupt settings file to %s: %s",
                backup.c_str(), Utils::Utf8ToWide(ec.message()).c_str());
        } else {
            LOGF_LIFECYCLE(L"[SettingsManager] Corrupt settings file backed up to %s before reset",
                backup.c_str());
        }
    } catch (...) {
        LOG_LIFECYCLE(L"[SettingsManager] FAILED to back up corrupt settings file (unknown error)");
    }
}

} // namespace AgentRedactor
