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
    // Protection is Windows-Hello-only (no typed password). A legacy config
    // that enabled protection with a typed password but has no Hello blob
    // degrades to unprotected (the Hello blob cannot be created without the
    // user's Windows Hello verification).
    bool Initialize(const json& config);

    bool IsInitialized() const { return initialized_; }
    bool IsMasterPasswordEnabled() const { return masterPasswordEnabled_; }
    bool IsHelloEnabled() const { return helloEnabled_; }

    // Encrypt a plaintext string. Returns a JSON object with the encrypted blob.
    // Mode is determined by whether protection is enabled.
    json Encrypt(const std::wstring& plaintext) const;

    // Decrypt an encrypted field JSON object.
    std::optional<std::wstring> Decrypt(const json& fieldJson) const;

    // Get the master_password config block to save to settings.json.
    json GetConfig() const;

    // Enable Windows-Hello-only protection: a random AES key is generated and
    // stored only as the DPAPI-wrapped Hello blob. Windows Hello is the only
    // way to unlock.
    bool EnableMasterPassword();

    // Disable master password. Subsequent Encrypt calls will use DPAPI.
    void DisableMasterPassword();

    // Unlock the storage with the Windows Hello blob (the UserConsentVerifier
    // consent prompt is driven by the caller via hello_unlock.cpp).
    bool UnlockWithHello();

    // Lock the session again without discarding the in-memory AES key: the
    // storage is marked uninitialized so reads/decrypts fail until
    // UnlockWithHello succeeds (used after the GUI quits while the engine
    // keeps running, so the next open must re-authenticate).
    void Lock();

private:
    bool initialized_ = false;
    bool masterPasswordEnabled_ = false;
    bool helloEnabled_ = false;
    std::vector<BYTE> aesKey_; // 32 bytes, only valid when initialized
    std::vector<BYTE> helloBlob_; // DPAPI-wrapped copy of aesKey_, used by UnlockWithHello

    // DPAPI
    static std::optional<std::vector<BYTE>> DpapiProtect(const std::vector<BYTE>& data);
    static std::optional<std::vector<BYTE>> DpapiUnprotect(const std::vector<BYTE>& data);
    static std::optional<std::vector<BYTE>> DpapiEncrypt(const std::wstring& plaintext);
    static std::optional<std::wstring> DpapiDecrypt(const std::vector<BYTE>& ciphertext);

    // AES-256-GCM via Windows CNG
    static bool AesGcmEncrypt(const std::vector<BYTE>& plaintext, const std::vector<BYTE>& key,
        std::vector<BYTE>& ciphertext, std::vector<BYTE>& iv, std::vector<BYTE>& tag);
    static std::optional<std::vector<BYTE>> AesGcmDecrypt(const std::vector<BYTE>& ciphertext,
        const std::vector<BYTE>& key, const std::vector<BYTE>& iv, const std::vector<BYTE>& tag);

    // Random bytes via BCryptGenRandom
    static std::vector<BYTE> GenerateRandomBytes(size_t count);

    // Base64 via CryptBinaryToString/CryptStringToBinary
    static std::string Base64Encode(const std::vector<BYTE>& data);
    static std::vector<BYTE> Base64Decode(const std::string& str);
};

} // namespace AgentRedactor