#pragma once

// OS-agnostic secure-storage interface for settings secrets (API keys,
// keywords, regex patterns). The class declaration is shared; each platform
// provides its own implementation:
//   Windows: windows/src/secure_storage.cpp — DPAPI + CNG AES-GCM, protection
//            is Windows-Hello-only (no typed password).
//   Linux:   core/src/secure_storage_linux.cpp — OpenSSL AES-GCM; unprotected
//            fields use a machine key from the libsecret keyring (with a
//            machine-id-derived fallback on headless servers); protection is
//            a typed master password whose PBKDF2-HMAC-SHA256 key wraps the
//            AES session key.
// The on-disk envelope (_enc/_mode/_iv/_tag, base64) is identical on both.

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <nlohmann/json.hpp>
#include "platform_compat.h"

using json = nlohmann::json;

namespace AgentRedactor {

class SecureStorage {
public:
    // Initialize from the "master_password" config block in settings.json.
    // A legacy/foreign config whose protection blob this platform cannot
    // unwrap degrades to unprotected so the user can re-enable protection.
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

#ifdef _WIN32
    // Enable Windows-Hello-only protection: a random AES key is generated and
    // stored only as the DPAPI-wrapped Hello blob. Windows Hello is the only
    // way to unlock.
    bool EnableMasterPassword();

    // Disable master password. Subsequent Encrypt calls will use DPAPI.
    void DisableMasterPassword();

    // Unlock the storage with the Windows Hello blob (the UserConsentVerifier
    // consent prompt is driven by the caller via hello_unlock.cpp).
    bool UnlockWithHello();
#else
    // Linux: typed-master-password protection (there is no Windows Hello).
    // A random AES-256-GCM session key is wrapped by a key derived from the
    // password via PBKDF2-HMAC-SHA256; only the wrapped blob is persisted.
    bool EnableMasterPassword(const std::wstring& password);
    void DisableMasterPassword();
    bool UnlockWithPassword(const std::wstring& password);
#endif

    // Lock the session again without discarding the in-memory AES key: the
    // storage is marked uninitialized so reads/decrypts fail until unlock
    // succeeds (used after the GUI quits while the engine keeps running, so
    // the next open must re-authenticate).
    void Lock();

private:
    bool initialized_ = false;
    bool masterPasswordEnabled_ = false;
    bool helloEnabled_ = false; // Windows Hello; always false on Linux
    std::vector<BYTE> aesKey_; // 32 bytes, only valid when initialized

#ifdef _WIN32
    std::vector<BYTE> helloBlob_; // DPAPI-wrapped copy of aesKey_, used by UnlockWithHello

    // DPAPI
    static std::optional<std::vector<BYTE>> DpapiProtect(const std::vector<BYTE>& data);
    static std::optional<std::vector<BYTE>> DpapiUnprotect(const std::vector<BYTE>& data);
    static std::optional<std::vector<BYTE>> DpapiEncrypt(const std::wstring& plaintext);
    static std::optional<std::wstring> DpapiDecrypt(const std::vector<BYTE>& ciphertext);
#else
    // Persisted password verification block (under master_password.password).
    std::vector<BYTE> passwordSalt_;
    uint32_t passwordIterations_ = 0;
    std::vector<BYTE> wrappedKey_;    // AES-GCM(aesKey_) under the PBKDF2 KEK
    std::vector<BYTE> wrappedKeyIv_;
    std::vector<BYTE> wrappedKeyTag_;

    // The unprotected at-rest key (DPAPI counterpart): a random key from the
    // libsecret keyring, wrapped by the machine-id-derived key in machine.key,
    // or PBKDF2-derived from /etc/machine-id when no keyring/wrap file is
    // available (headless servers).
    static std::optional<std::vector<BYTE>> MachineKey();
    static std::optional<std::vector<BYTE>> MachineIdDerivedKey();
    static std::optional<std::vector<BYTE>> ReadWrappedMachineKey(
        const std::vector<BYTE>& machineIdKey);
    static bool WriteWrappedMachineKey(const std::vector<BYTE>& machineKey,
        const std::vector<BYTE>& machineIdKey);

    static std::vector<BYTE> Pbkdf2(const std::string& password, const std::vector<BYTE>& salt, uint32_t iterations);
#endif

    // AES-256-GCM (CNG on Windows, OpenSSL EVP on Linux)
    static bool AesGcmEncrypt(const std::vector<BYTE>& plaintext, const std::vector<BYTE>& key,
        std::vector<BYTE>& ciphertext, std::vector<BYTE>& iv, std::vector<BYTE>& tag);
    static std::optional<std::vector<BYTE>> AesGcmDecrypt(const std::vector<BYTE>& ciphertext,
        const std::vector<BYTE>& key, const std::vector<BYTE>& iv, const std::vector<BYTE>& tag);

    // Random bytes (BCryptGenRandom on Windows, RAND_bytes on Linux)
    static std::vector<BYTE> GenerateRandomBytes(size_t count);

    // Base64 (CryptBinaryToString on Windows, EVP on Linux)
    static std::string Base64Encode(const std::vector<BYTE>& data);
    static std::vector<BYTE> Base64Decode(const std::string& str);
};

} // namespace AgentRedactor
