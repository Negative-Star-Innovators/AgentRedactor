#include "secure_storage.h"
#include "utils.h"
#include "logging.h"
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <vector>
#include <string>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace AgentRedactor {

// ============================================================================
// Public API
// ============================================================================

bool SecureStorage::Initialize(const json& config, const std::wstring& masterPassword) {
    initialized_ = false;
    masterPasswordEnabled_ = false;
    aesKey_.clear();
    salt_.clear();
    encryptedAesKey_.clear();
    keyIv_.clear();
    keyTag_.clear();

    if (config.contains("enabled") && config["enabled"].get<bool>()) {
        masterPasswordEnabled_ = true;
        salt_ = Base64Decode(config.value("salt", ""));
        iterations_ = config.value("iterations", 100000);
        encryptedAesKey_ = Base64Decode(config.value("encrypted_key", ""));
        keyIv_ = Base64Decode(config.value("key_iv", ""));
        keyTag_ = Base64Decode(config.value("key_tag", ""));

        if (!masterPassword.empty()) {
            return Unlock(masterPassword);
        }
        // Config loaded, waiting for password
        return true;
    }

    // DPAPI mode - always ready
    masterPasswordEnabled_ = false;
    initialized_ = true;
    return true;
}

json SecureStorage::Encrypt(const std::wstring& plaintext) const {
    if (!initialized_) {
        return json{{"_enc", ""}, {"_mode", "none"}};
    }

    if (plaintext.empty()) {
        return json{{"_enc", ""}, {"_mode", masterPasswordEnabled_ ? "aes" : "dpapi"}};
    }

    std::string utf8Plain = Utils::WideToUtf8(plaintext);
    std::vector<BYTE> plainBytes(utf8Plain.begin(), utf8Plain.end());

    if (masterPasswordEnabled_) {
        std::vector<BYTE> ciphertext, iv, tag;
        if (!AesGcmEncrypt(plainBytes, aesKey_, ciphertext, iv, tag)) {
            return json{{"_enc", ""}, {"_mode", "aes"}};
        }
        return json{
            {"_enc", Base64Encode(ciphertext)},
            {"_mode", "aes"},
            {"_iv", Base64Encode(iv)},
            {"_tag", Base64Encode(tag)}
        };
    } else {
        auto encrypted = DpapiEncrypt(plaintext);
        if (!encrypted) {
            return json{{"_enc", ""}, {"_mode", "dpapi"}};
        }
        return json{
            {"_enc", Base64Encode(*encrypted)},
            {"_mode", "dpapi"}
        };
    }
}

std::optional<std::wstring> SecureStorage::Decrypt(const json& fieldJson) const {
    if (!initialized_) {
        return std::nullopt;
    }

    if (!fieldJson.is_object() || !fieldJson.contains("_enc")) {
        return std::nullopt;
    }

    std::string mode = fieldJson.value("_mode", "dpapi");
    std::vector<BYTE> ciphertext = Base64Decode(fieldJson.value("_enc", ""));
    if (ciphertext.empty()) {
        return L"";
    }

    if (mode == "aes") {
        std::vector<BYTE> iv = Base64Decode(fieldJson.value("_iv", ""));
        std::vector<BYTE> tag = Base64Decode(fieldJson.value("_tag", ""));
        auto decrypted = AesGcmDecrypt(ciphertext, aesKey_, iv, tag);
        if (!decrypted) return std::nullopt;
        std::string utf8Result(decrypted->begin(), decrypted->end());
        return Utils::Utf8ToWide(utf8Result);
    } else {
        auto decrypted = DpapiDecrypt(ciphertext);
        if (!decrypted) return std::nullopt;
        return *decrypted;
    }
}

json SecureStorage::GetConfig() const {
    json config;
    config["enabled"] = masterPasswordEnabled_;
    if (masterPasswordEnabled_) {
        config["salt"] = Base64Encode(salt_);
        config["iterations"] = iterations_;
        config["encrypted_key"] = Base64Encode(encryptedAesKey_);
        config["key_iv"] = Base64Encode(keyIv_);
        config["key_tag"] = Base64Encode(keyTag_);
    }
    return config;
}

bool SecureStorage::EnableMasterPassword(const std::wstring& password) {
    if (password.empty()) return false;

    // Generate random AES key
    aesKey_ = GenerateRandomBytes(32);
    salt_ = GenerateRandomBytes(16);
    iterations_ = 100000;

    // Derive KEK from password
    auto kek = Pbkdf2DeriveKey(password, salt_, iterations_, 32);

    // Encrypt AES key with KEK
    if (!AesGcmEncrypt(aesKey_, kek, encryptedAesKey_, keyIv_, keyTag_)) {
        aesKey_.clear();
        salt_.clear();
        encryptedAesKey_.clear();
        keyIv_.clear();
        keyTag_.clear();
        return false;
    }

    masterPasswordEnabled_ = true;
    initialized_ = true;
    return true;
}

bool SecureStorage::ChangeMasterPassword(const std::wstring& oldPassword, const std::wstring& newPassword) {
    if (!masterPasswordEnabled_ || newPassword.empty()) return false;

    // Verify old password by unlocking
    if (!Unlock(oldPassword)) return false;

    // Now aesKey_ is decrypted. Re-encrypt with new password.
    salt_ = GenerateRandomBytes(16);
    auto kek = Pbkdf2DeriveKey(newPassword, salt_, iterations_, 32);

    if (!AesGcmEncrypt(aesKey_, kek, encryptedAesKey_, keyIv_, keyTag_)) {
        return false;
    }

    // aesKey_ stays in memory, initialized_ stays true
    return true;
}

void SecureStorage::DisableMasterPassword() {
    masterPasswordEnabled_ = false;
    initialized_ = true;
    aesKey_.clear();
    salt_.clear();
    encryptedAesKey_.clear();
    keyIv_.clear();
    keyTag_.clear();
}

bool SecureStorage::Unlock(const std::wstring& password) {
    if (!masterPasswordEnabled_) {
        initialized_ = true;
        return true;
    }

    if (salt_.empty() || encryptedAesKey_.empty() || keyIv_.empty() || keyTag_.empty()) {
        return false;
    }

    auto kek = Pbkdf2DeriveKey(password, salt_, iterations_, 32);
    auto decrypted = AesGcmDecrypt(encryptedAesKey_, kek, keyIv_, keyTag_);
    if (!decrypted) {
        initialized_ = false;
        return false;
    }

    aesKey_ = *decrypted;
    initialized_ = true;
    return true;
}

// ============================================================================
// DPAPI
// ============================================================================

std::optional<std::vector<BYTE>> SecureStorage::DpapiEncrypt(const std::wstring& plaintext) {
    std::string utf8 = Utils::WideToUtf8(plaintext);
    DATA_BLOB inBlob = { static_cast<DWORD>(utf8.size()), reinterpret_cast<BYTE*>(const_cast<char*>(utf8.data())) };
    DATA_BLOB outBlob = {};

    if (CryptProtectData(&inBlob, L"AgentRedactorApiKey", nullptr, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        std::vector<BYTE> result(outBlob.pbData, outBlob.pbData + outBlob.cbData);
        LocalFree(outBlob.pbData);
        return result;
    }
    return std::nullopt;
}

std::optional<std::wstring> SecureStorage::DpapiDecrypt(const std::vector<BYTE>& ciphertext) {
    DATA_BLOB inBlob = { static_cast<DWORD>(ciphertext.size()), const_cast<BYTE*>(ciphertext.data()) };
    DATA_BLOB outBlob = {};

    if (CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        std::string utf8(reinterpret_cast<char*>(outBlob.pbData), outBlob.cbData);
        LocalFree(outBlob.pbData);
        return Utils::Utf8ToWide(utf8);
    }
    return std::nullopt;
}

// ============================================================================
// AES-256-GCM via Windows CNG
// ============================================================================

bool SecureStorage::AesGcmEncrypt(const std::vector<BYTE>& plaintext, const std::vector<BYTE>& key,
    std::vector<BYTE>& ciphertext, std::vector<BYTE>& iv, std::vector<BYTE>& tag) {

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) return false;

    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
        sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    DWORD objLen = 0, resultLen = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objLen), sizeof(DWORD), &resultLen, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    BCRYPT_KEY_HANDLE hKey = nullptr;
    std::vector<BYTE> keyObj(objLen);
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, keyObj.data(), objLen,
        const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    // Get block length for IV
    DWORD blockLen = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_BLOCK_LENGTH,
        reinterpret_cast<PUCHAR>(&blockLen), sizeof(DWORD), &resultLen, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    // Generate random IV
    iv = GenerateRandomBytes(blockLen);
    tag.resize(16);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = iv.data();
    authInfo.cbNonce = static_cast<ULONG>(iv.size());
    authInfo.pbTag = tag.data();
    authInfo.cbTag = static_cast<ULONG>(tag.size());

    DWORD cipherLen = 0;
    status = BCryptEncrypt(hKey, const_cast<PUCHAR>(plaintext.data()), static_cast<ULONG>(plaintext.size()),
        &authInfo, nullptr, 0, nullptr, 0, &cipherLen, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    ciphertext.resize(cipherLen);
    status = BCryptEncrypt(hKey, const_cast<PUCHAR>(plaintext.data()), static_cast<ULONG>(plaintext.size()),
        &authInfo, nullptr, 0, ciphertext.data(), cipherLen, &cipherLen, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return true;
}

std::optional<std::vector<BYTE>> SecureStorage::AesGcmDecrypt(const std::vector<BYTE>& ciphertext,
    const std::vector<BYTE>& key, const std::vector<BYTE>& iv, const std::vector<BYTE>& tag) {

    if (key.size() != 32 || iv.empty() || tag.size() != 16) return std::nullopt;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) return std::nullopt;

    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
        sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return std::nullopt;
    }

    DWORD objLen = 0, resultLen = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objLen), sizeof(DWORD), &resultLen, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return std::nullopt;
    }

    BCRYPT_KEY_HANDLE hKey = nullptr;
    std::vector<BYTE> keyObj(objLen);
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, keyObj.data(), objLen,
        const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return std::nullopt;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(iv.data());
    authInfo.cbNonce = static_cast<ULONG>(iv.size());
    authInfo.pbTag = const_cast<PUCHAR>(tag.data());
    authInfo.cbTag = static_cast<ULONG>(tag.size());

    DWORD plainLen = 0;
    status = BCryptDecrypt(hKey, const_cast<PUCHAR>(ciphertext.data()), static_cast<ULONG>(ciphertext.size()),
        &authInfo, nullptr, 0, nullptr, 0, &plainLen, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return std::nullopt;
    }

    std::vector<BYTE> plaintext(plainLen);
    status = BCryptDecrypt(hKey, const_cast<PUCHAR>(ciphertext.data()), static_cast<ULONG>(ciphertext.size()),
        &authInfo, nullptr, 0, plaintext.data(), plainLen, &plainLen, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return std::nullopt;
    }

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return plaintext;
}

// ============================================================================
// PBKDF2-HMAC-SHA256
// ============================================================================

std::vector<BYTE> SecureStorage::Pbkdf2DeriveKey(const std::wstring& password,
    const std::vector<BYTE>& salt, int iterations, size_t keyLen) {

    std::vector<BYTE> result;
    if (password.empty() || salt.empty() || iterations <= 0 || keyLen == 0) return result;

    std::string utf8Password = Utils::WideToUtf8(password);

    BCRYPT_ALG_HANDLE hPrf = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hPrf, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!BCRYPT_SUCCESS(status)) return result;

    result.resize(keyLen);
    status = BCryptDeriveKeyPBKDF2(hPrf,
        reinterpret_cast<PUCHAR>(const_cast<char*>(utf8Password.data())), static_cast<ULONG>(utf8Password.size()),
        const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()),
        iterations, result.data(), static_cast<ULONG>(keyLen), 0);

    BCryptCloseAlgorithmProvider(hPrf, 0);

    if (!BCRYPT_SUCCESS(status)) {
        result.clear();
    }
    return result;
}

// ============================================================================
// Random bytes
// ============================================================================

std::vector<BYTE> SecureStorage::GenerateRandomBytes(size_t count) {
    std::vector<BYTE> result(count);
    if (count == 0) return result;
    NTSTATUS status = BCryptGenRandom(nullptr, result.data(), static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        result.clear();
    }
    return result;
}

// ============================================================================
// Base64
// ============================================================================

std::string SecureStorage::Base64Encode(const std::vector<BYTE>& data) {
    if (data.empty()) return "";
    DWORD strLen = 0;
    CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &strLen);
    if (strLen == 0) return "";
    std::string result(strLen - 1, '\0');
    CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &strLen);
    return result;
}

std::vector<BYTE> SecureStorage::Base64Decode(const std::string& str) {
    if (str.empty()) return {};
    DWORD binLen = 0;
    CryptStringToBinaryA(str.c_str(), static_cast<DWORD>(str.size()),
        CRYPT_STRING_BASE64, nullptr, &binLen, nullptr, nullptr);
    if (binLen == 0) return {};
    std::vector<BYTE> result(binLen);
    CryptStringToBinaryA(str.c_str(), static_cast<DWORD>(str.size()),
        CRYPT_STRING_BASE64, result.data(), &binLen, nullptr, nullptr);
    return result;
}

} // namespace AgentRedactor
