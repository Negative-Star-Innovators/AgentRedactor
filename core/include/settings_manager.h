#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <nlohmann/json.hpp>
#include "api_key_profile.h"
#include "secure_storage.h"
#include "utils.h"

using json = nlohmann::json;

namespace AgentRedactor {

class SettingsManager {
public:
    explicit SettingsManager(const std::filesystem::path& configDir = L"");
    ~SettingsManager() = default;
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    bool IsStartOnBoot() const;
    void SetStartOnBoot(bool enabled);

    std::wstring GetOnnxProvider() const;
    void SetOnnxProvider(const std::wstring& provider);

    std::vector<ApiKeyProfile> GetProfiles() const;
    void SetProfiles(const std::vector<ApiKeyProfile>& profiles);
    void AddProfile(const ApiKeyProfile& profile);
    void UpdateProfile(const ApiKeyProfile& profile);
    void RemoveProfile(const std::wstring& id);
    std::optional<ApiKeyProfile> GetProfileById(const std::wstring& id) const;
    std::optional<ApiKeyProfile> GetProfileByPort(int port) const;

    void SaveSettings();
    void LoadSettings();

    bool IsMasterPasswordEnabled() const;
    bool IsUnlocked() const;
    bool UnlockWithPassword(const std::wstring& password);
    bool EnableMasterPassword(const std::wstring& password);
    bool ChangeMasterPassword(const std::wstring& oldPassword, const std::wstring& newPassword);
    void DisableMasterPassword();

    bool IsLoggingEnabled() const;
    void SetLoggingEnabled(bool enabled);

    std::wstring GetAppLanguage() const;
    void SetAppLanguage(const std::wstring& language);

    const std::filesystem::path& GetConfigDir() const { return configDir_; }

private:
    std::filesystem::path configDir_;
    std::filesystem::path settingsFile_;
    json settings_;
    mutable std::shared_mutex mutex_;
    SecureStorage secureStorage_;

    void EncryptSensitiveFields();
    void DecryptSensitiveFields();
    void BackupCorruptSettingsFile();
};

} // namespace AgentRedactor
