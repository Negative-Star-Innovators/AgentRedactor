#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace AgentRedactor {

class SecureStorage {
public:
    // Initialize from the "master_password" config block in settings.json.
    // If master password is enabled but no password is provided, the storage
    // is not initialized (Encrypt/Decrypt will fail until Unlock is called).
    bool Initialize(const json& config, const std::wstring& masterPassword = L"");

    bool IsInitialized() const { return initialized_; }
    bool IsMasterPasswordEnabled() const { return masterPasswordEnabled_; }

    // Encrypt a plaintext string. Returns a JSON object with the encrypted blob.
    // Mode is determined by whether master password is enabled.
    json Encrypt(const std::wstring& plaintext) const;

    // Decrypt an encrypted field JSON object.
    std::optional<std::wstring> Decrypt(const json& fieldJson) const;

    // Get the master_password config block to save to settings.json.
    json GetConfig() const;

    // Enable master password protection. Generates a random AES key and
    // encrypts it with a key derived from the provided password.
    bool EnableMasterPassword(const std::wstring& password);

    // Change the master password. Requires the old password to decrypt the AES key.
    bool ChangeMasterPassword(const std::wstring& oldPassword, const std::wstring& newPassword);

    // Disable master password. Subsequent Encrypt calls will use DPAPI.
    void DisableMasterPassword();

    // Unlock the storage with the master password.
    bool Unlock(const std::wstring& password);

private:
    bool initialized_ = false;
    bool masterPasswordEnabled_ = false;
    std::vector<BYTE> aesKey_; // 32 bytes, only valid when initialized with master password

    // Persistent config for saving
    std::vector<BYTE> salt_;
    int iterations_ = 100000;
    std::vector<BYTE> encryptedAesKey_;
    std::vector<BYTE> keyIv_;
    std::vector<BYTE> keyTag_;

    // DPAPI
    static std::optional<std::vector<BYTE>> DpapiEncrypt(const std::wstring& plaintext);
    static std::optional<std::wstring> DpapiDecrypt(const std::vector<BYTE>& ciphertext);

    // AES-256-GCM via Windows CNG
    static bool AesGcmEncrypt(const std::vector<BYTE>& plaintext, const std::vector<BYTE>& key,
        std::vector<BYTE>& ciphertext, std::vector<BYTE>& iv, std::vector<BYTE>& tag);
    static std::optional<std::vector<BYTE>> AesGcmDecrypt(const std::vector<BYTE>& ciphertext,
        const std::vector<BYTE>& key, const std::vector<BYTE>& iv, const std::vector<BYTE>& tag);

    // PBKDF2-HMAC-SHA256 via Windows CNG
    static std::vector<BYTE> Pbkdf2DeriveKey(const std::wstring& password,
        const std::vector<BYTE>& salt, int iterations, size_t keyLen = 32);

    // Random bytes via BCryptGenRandom
    static std::vector<BYTE> GenerateRandomBytes(size_t count);

    // Base64 via CryptBinaryToString/CryptStringToBinary
    static std::string Base64Encode(const std::vector<BYTE>& data);
    static std::vector<BYTE> Base64Decode(const std::string& str);
};

} // namespace AgentRedactor
