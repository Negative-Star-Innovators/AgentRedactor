// macOS SecureStorage: OpenSSL AES-256-GCM + PBKDF2-HMAC-SHA256, with the
// machine key in the macOS Keychain (Security.framework).
//
// The on-disk envelope is byte-identical to the Windows/Linux format
// (_enc/_mode/_iv/_tag, base64; AES-256-GCM, 12-byte IV, 16-byte tag;
// PBKDF2-HMAC-SHA256 with 600000 iterations for the master-password KEK), so
// settings files are cross-platform portable: "aes"-mode fields decrypt
// anywhere the password is known. "machine"-mode fields are per-machine by
// design (DPAPI on Windows, machine-id+keyring on Linux, Keychain+host-UUID
// here) — like the other platforms they are not portable across machines.
//
// Crypto backend choice: OpenSSL (Homebrew openssl@3) rather than
// CommonCrypto, so the AES-GCM/PBKDF2/base64 code paths are literally the
// Linux ones and the envelope can never drift between platforms.
//
// Machine key flow (mirror of secure_storage_linux.cpp):
//   - Unprotected fields use a random 32-byte machine key stored in the
//     Keychain generic-password item "machine-key" (envelope _mode "machine";
//     the DPAPI/libsecret counterpart).
//   - The random key is additionally wrapped by a key PBKDF2-derived from the
//     hardware UUID (IOKit IOPlatformUUID, the /etc/machine-id counterpart)
//     and persisted in machine.key next to settings.json, so settings stay
//     readable when the Keychain is unreachable (headless/SSH) — same
//     semantics as the Linux keyring-less fallback.
//   - Final fallback is the derived key itself, which only obscures secrets
//     from other users on the same machine (first-run warning is logged).
//   - AGENTREDACTOR_DISABLE_KEYRING=1 skips the Keychain entirely (tests/CI).
//
//   - Protected: a typed master password. A random 32-byte AES session key is
//     wrapped with a PBKDF2-HMAC-SHA256 key derived from the password and only
//     the wrapped blob is persisted (master_password.password in settings.json).
//     The session starts locked; UnlockWithPassword recovers the session key.

#include "secure_storage.h"
#include "utils.h"
#include "logging.h"

#ifdef __APPLE__

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <Security/Security.h>
#include <cstdlib>
#include <mutex>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace AgentRedactor {

namespace {

constexpr uint32_t kPbkdf2Iterations = 600000;
constexpr size_t kSessionKeyBytes = 32;  // AES-256
constexpr size_t kSaltBytes = 16;
constexpr size_t kGcmIvBytes = 12;
constexpr size_t kGcmTagBytes = 16;

// Keychain identity of the machine key item (mirrors the Linux libsecret
// schema name so exports/debugging look familiar across platforms).
CFStringRef MachineKeyService() {
    return CFSTR("com.negativestarinnovators.AgentRedactor");
}
CFStringRef MachineKeyAccount() {
    return CFSTR("machine-key");
}

// Reads the machine key (base64) from the login Keychain. Nullopt when the
// item does not exist or the Keychain is unavailable — either way the caller
// falls back to the wrapped/derived machine key, exactly like the Linux
// headless path. Unlike libsecret-over-D-Bus, the SecItem API fails fast when
// no keychain is reachable, so no ownership pre-probe is needed.
std::optional<std::string> KeychainReadMachineKey() {
    const void* keys[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData };
    const void* values[] = { kSecClassGenericPassword, MachineKeyService(),
                             MachineKeyAccount(), kCFBooleanTrue };
    CFDictionaryRef query = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 4,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!query) return std::nullopt;
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status != errSecSuccess || !result) return std::nullopt;
    if (CFGetTypeID(result) != CFDataGetTypeID()) {
        CFRelease(result);
        return std::nullopt;
    }
    std::string value(reinterpret_cast<const char*>(CFDataGetBytePtr(static_cast<CFDataRef>(result))),
        static_cast<size_t>(CFDataGetLength(static_cast<CFDataRef>(result))));
    CFRelease(result);
    return value;
}

// Stable hardware UUID via IOKit (the /etc/machine-id counterpart; the
// kern.hostuuid sysctl is not available on modern macOS). Nullopt when the
// platform expert device cannot be read.
std::optional<std::string> PlatformUuid() {
    io_service_t platform = IOServiceGetMatchingService(kIOMainPortDefault,
        IOServiceMatching("IOPlatformExpertDevice"));
    if (platform == 0) return std::nullopt;
    CFTypeRef uuidRef = IORegistryEntryCreateCFProperty(platform,
        CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0);
    IOObjectRelease(platform);
    if (!uuidRef) return std::nullopt;
    std::string result;
    if (CFGetTypeID(uuidRef) == CFStringGetTypeID()) {
        char buf[256] = {};
        if (CFStringGetCString(static_cast<CFStringRef>(uuidRef), buf, sizeof(buf),
                kCFStringEncodingUTF8)) {
            result = buf;
        }
    }
    CFRelease(uuidRef);
    if (result.empty()) return std::nullopt;
    return result;
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

bool SecureStorage::Initialize(const json& config) {
    initialized_ = false;
    masterPasswordEnabled_ = false;
    helloEnabled_ = false;
    aesKey_.clear();
    passwordSalt_.clear();
    passwordIterations_ = 0;
    wrappedKey_.clear();
    wrappedKeyIv_.clear();
    wrappedKeyTag_.clear();

    if (config.contains("enabled") && config["enabled"].get<bool>()) {
        // Typed-master-password protection. A config written on another
        // platform (Windows Hello blob) or a legacy block this build cannot
        // unwrap degrades to unprotected so the user can re-enable.
        if (config.contains("password") && config["password"].is_object()) {
            const auto& pw = config["password"];
            auto salt = Base64Decode(pw.value("salt", ""));
            auto wrapped = Base64Decode(pw.value("wrapped_key", ""));
            auto iv = Base64Decode(pw.value("iv", ""));
            auto tag = Base64Decode(pw.value("tag", ""));
            uint32_t iterations = pw.value("iterations", 0u);
            if (!salt.empty() && !wrapped.empty() && !iv.empty() && !tag.empty() && iterations > 0) {
                masterPasswordEnabled_ = true;
                passwordSalt_ = std::move(salt);
                passwordIterations_ = iterations;
                wrappedKey_ = std::move(wrapped);
                wrappedKeyIv_ = std::move(iv);
                wrappedKeyTag_ = std::move(tag);
            }
        }
    }

    // The storage is ready to encrypt/decrypt in the current mode (machine
    // key when unprotected; a protected session stays locked until
    // UnlockWithPassword). Sensitive fields stay encrypted until unlocked.
    initialized_ = !masterPasswordEnabled_;
    return true;
}

json SecureStorage::Encrypt(const std::wstring& plaintext) const {
    if (!initialized_) {
        return json{{"_enc", ""}, {"_mode", "none"}};
    }

    if (plaintext.empty()) {
        return json{{"_enc", ""}, {"_mode", masterPasswordEnabled_ ? "aes" : "machine"}};
    }

    std::string utf8Plain = Utils::WideToUtf8(plaintext);
    std::vector<BYTE> plainBytes(utf8Plain.begin(), utf8Plain.end());

    std::vector<BYTE> key;
    std::string mode;
    if (masterPasswordEnabled_) {
        key = aesKey_;
        mode = "aes";
    } else {
        auto machineKey = MachineKey();
        if (!machineKey) {
            return json{{"_enc", ""}, {"_mode", "machine"}};
        }
        key = std::move(*machineKey);
        mode = "machine";
    }

    std::vector<BYTE> ciphertext, iv, tag;
    if (!AesGcmEncrypt(plainBytes, key, ciphertext, iv, tag)) {
        return json{{"_enc", ""}, {"_mode", mode}};
    }
    return json{
        {"_enc", Base64Encode(ciphertext)},
        {"_mode", mode},
        {"_iv", Base64Encode(iv)},
        {"_tag", Base64Encode(tag)}
    };
}

std::optional<std::wstring> SecureStorage::Decrypt(const json& fieldJson) const {
    if (!initialized_) {
        return std::nullopt;
    }

    if (!fieldJson.is_object() || !fieldJson.contains("_enc")) {
        return std::nullopt;
    }

    std::string mode = fieldJson.value("_mode", "machine");
    std::vector<BYTE> ciphertext = Base64Decode(fieldJson.value("_enc", ""));
    if (ciphertext.empty()) {
        return L"";
    }

    std::vector<BYTE> key;
    if (mode == "aes") {
        key = aesKey_;
    } else {
        auto machineKey = MachineKey();
        if (!machineKey) return std::nullopt;
        key = std::move(*machineKey);
    }

    std::vector<BYTE> iv = Base64Decode(fieldJson.value("_iv", ""));
    std::vector<BYTE> tag = Base64Decode(fieldJson.value("_tag", ""));
    auto decrypted = AesGcmDecrypt(ciphertext, key, iv, tag);
    if (!decrypted && mode == "machine") {
        // Fallback for legacy data or a transition between keychain and
        // host-uuid-only installs: try the raw host-uuid-derived key.
        auto fallbackKey = MachineIdDerivedKey();
        if (fallbackKey) {
            decrypted = AesGcmDecrypt(ciphertext, *fallbackKey, iv, tag);
        }
    }
    if (!decrypted) return std::nullopt;
    std::string utf8Result(decrypted->begin(), decrypted->end());
    return Utils::Utf8ToWide(utf8Result);
}

json SecureStorage::GetConfig() const {
    json config;
    config["enabled"] = masterPasswordEnabled_;
    if (masterPasswordEnabled_) {
        config["password"] = json{
            {"salt", Base64Encode(passwordSalt_)},
            {"iterations", passwordIterations_},
            {"wrapped_key", Base64Encode(wrappedKey_)},
            {"iv", Base64Encode(wrappedKeyIv_)},
            {"tag", Base64Encode(wrappedKeyTag_)},
        };
    }
    return config;
}

bool SecureStorage::EnableMasterPassword(const std::wstring& password) {
    if (masterPasswordEnabled_) return false;
    if (password.empty()) return false;

    auto sessionKey = GenerateRandomBytes(kSessionKeyBytes);
    auto salt = GenerateRandomBytes(kSaltBytes);
    if (sessionKey.size() != kSessionKeyBytes || salt.size() != kSaltBytes) return false;

    auto kek = Pbkdf2(Utils::WideToUtf8(password), salt, kPbkdf2Iterations);
    std::vector<BYTE> wrapped, iv, tag;
    if (!AesGcmEncrypt(sessionKey, kek, wrapped, iv, tag)) return false;

    aesKey_ = sessionKey;
    passwordSalt_ = salt;
    passwordIterations_ = kPbkdf2Iterations;
    wrappedKey_ = std::move(wrapped);
    wrappedKeyIv_ = std::move(iv);
    wrappedKeyTag_ = std::move(tag);
    masterPasswordEnabled_ = true;
    initialized_ = true;
    return true;
}

void SecureStorage::DisableMasterPassword() {
    masterPasswordEnabled_ = false;
    initialized_ = true;
    aesKey_.clear();
    passwordSalt_.clear();
    passwordIterations_ = 0;
    wrappedKey_.clear();
    wrappedKeyIv_.clear();
    wrappedKeyTag_.clear();
}

bool SecureStorage::UnlockWithPassword(const std::wstring& password) {
    if (!masterPasswordEnabled_ || passwordSalt_.empty() || wrappedKey_.empty()) return false;
    auto kek = Pbkdf2(Utils::WideToUtf8(password), passwordSalt_, passwordIterations_);
    auto sessionKey = AesGcmDecrypt(wrappedKey_, kek, wrappedKeyIv_, wrappedKeyTag_);
    if (!sessionKey || sessionKey->size() != kSessionKeyBytes) return false;
    aesKey_ = *sessionKey;
    initialized_ = true;
    return true;
}

void SecureStorage::Lock() {
    initialized_ = false;
}

// ============================================================================
// Machine key (unprotected at-rest encryption)
// ============================================================================

// Stable key derived from the hardware UUID (IOKit IOPlatformUUID, the
// /etc/machine-id counterpart). Used both as a fallback encryption key and as
// the wrapping key for the random keychain key stored in machine.key.
std::optional<std::vector<BYTE>> SecureStorage::MachineIdDerivedKey() {
    const auto uuid = PlatformUuid();
    if (!uuid) return std::nullopt;
    static const std::vector<BYTE> kMachineSalt = {
        'a','g','e','n','t','r','e','d','a','c','t','o','r','-','m','k'
    };
    auto derived = Pbkdf2(*uuid, kMachineSalt, 10000);
    if (derived.size() != kSessionKeyBytes) return std::nullopt;
    return derived;
}

static std::filesystem::path WrappedMachineKeyPath() {
    return Utils::GetAppDataPath() / "machine.key";
}

std::optional<std::vector<BYTE>> SecureStorage::ReadWrappedMachineKey(
    const std::vector<BYTE>& machineIdKey) {
    std::ifstream file(WrappedMachineKeyPath(), std::ios::binary);
    if (!file) return std::nullopt;
    try {
        json j;
        file >> j;
        auto ciphertext = Base64Decode(j.value("_enc", ""));
        auto iv = Base64Decode(j.value("_iv", ""));
        auto tag = Base64Decode(j.value("_tag", ""));
        if (ciphertext.empty()) return std::nullopt;
        auto plaintext = AesGcmDecrypt(ciphertext, machineIdKey, iv, tag);
        if (!plaintext || plaintext->size() != kSessionKeyBytes) return std::nullopt;
        return *plaintext;
    } catch (...) {
        return std::nullopt;
    }
}

bool SecureStorage::WriteWrappedMachineKey(const std::vector<BYTE>& machineKey,
    const std::vector<BYTE>& machineIdKey) {
    auto configDir = Utils::GetAppDataPath();
    // Best-effort mkdir: create_directories reports false when the directory
    // already exists (always, here — settings.json lives in it), so the
    // ofstream result below is the real success check.
    Utils::CreateDirectoryRecursive(configDir);
    std::vector<BYTE> ciphertext, iv, tag;
    if (!AesGcmEncrypt(machineKey, machineIdKey, ciphertext, iv, tag)) return false;
    json j = {
        {"_enc", Base64Encode(ciphertext)},
        {"_iv", Base64Encode(iv)},
        {"_tag", Base64Encode(tag)}
    };
    std::ofstream file(WrappedMachineKeyPath(), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << j.dump(2);
    return file.good();
}

std::optional<std::vector<BYTE>> SecureStorage::MachineKey() {
    // Cached: resolving the machine key on every field encrypt/decrypt would
    // be needlessly slow. The mutex makes the cache safe for concurrent first
    // use (the engine warms the key from a startup thread, see Prewarm).
    static std::mutex cacheMutex;
    std::lock_guard cacheLock(cacheMutex);
    static std::optional<std::vector<BYTE>> cached;
    static bool warnedFallback = false;
    if (cached) return cached;

    auto machineIdKey = MachineIdDerivedKey();
    if (!machineIdKey) return std::nullopt;

    // Primary store: the wrapped machine key file (machine.key in the config
    // dir, AES-256-GCM wrapped by the host-uuid-derived key). Fast, silent,
    // works headless, and never blocks on a Keychain authorization prompt.
    // Security posture matches the Linux libsecret session collection and
    // Windows DPAPI user scope: the wrapping key is derivable by any process
    // running as this user (IOPlatformUUID is user-readable), so the Keychain
    // ACL prompt added no real protection — only a ~10 s first-access stall
    // (or an unanswerable prompt) inside control-API requests.
    if (auto wrapped = ReadWrappedMachineKey(*machineIdKey)) {
        cached = *wrapped;
        return cached;
    }

    // No wrapped key yet. Legacy installs (early macOS builds) may have the
    // machine key in the Keychain only — migrate it to the wrapped file once.
    // AGENTREDACTOR_DISABLE_KEYRING=1 skips even this read (headless/SSH/CI).
    const bool keychainAllowed = [] {
        if (const char* dis = std::getenv("AGENTREDACTOR_DISABLE_KEYRING");
            dis && *dis && std::string(dis) != "0") {
            return false;
        }
        return true;
    }();
    if (keychainAllowed) {
        if (auto stored = KeychainReadMachineKey()) {
            auto key = Base64Decode(*stored);
            if (key.size() == kSessionKeyBytes && WriteWrappedMachineKey(key, *machineIdKey)) {
                cached = key;
                return cached;
            }
        }
    }

    // Fresh install: generate a random machine key and persist it in the
    // wrapped file (never the Keychain — see above).
    auto key = GenerateRandomBytes(kSessionKeyBytes);
    if (key.size() == kSessionKeyBytes && WriteWrappedMachineKey(key, *machineIdKey)) {
        cached = key;
        return cached;
    }

    // Final fallback: use the host-uuid-derived key directly. This matches the
    // legacy headless behavior and keeps old installs readable.
    if (!warnedFallback) {
        warnedFallback = true;
        LOG_LIFECYCLE(L"[SecureStorage] No wrapped machine key could be persisted; "
            L"deriving the at-rest key from the machine UUID. Secrets are only obfuscated, not protected.");
    }
    cached = *machineIdKey;
    return cached;
}

void SecureStorage::Prewarm() {
    // Resolve (and cache) the machine key before the control API starts
    // serving requests: the first SecItemCopyMatching in a process can block
    // on a Keychain authorization prompt (seconds), which must never happen
    // on a control-request thread — the GUI would freeze mid-action.
    (void)MachineKey();
}

// ============================================================================
// PBKDF2-HMAC-SHA256 (OpenSSL)
// ============================================================================

std::vector<BYTE> SecureStorage::Pbkdf2(const std::string& password, const std::vector<BYTE>& salt, uint32_t iterations) {
    std::vector<BYTE> out(kSessionKeyBytes);
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
            salt.data(), static_cast<int>(salt.size()),
            static_cast<int>(iterations), EVP_sha256(),
            static_cast<int>(out.size()), out.data()) != 1) {
        return {};
    }
    return out;
}

// ============================================================================
// AES-256-GCM (OpenSSL EVP — identical implementation/envelope to Linux)
// ============================================================================

bool SecureStorage::AesGcmEncrypt(const std::vector<BYTE>& plaintext, const std::vector<BYTE>& key,
    std::vector<BYTE>& ciphertext, std::vector<BYTE>& iv, std::vector<BYTE>& tag) {
    if (key.size() != kSessionKeyBytes) return false;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    bool ok = false;
    do {
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kGcmIvBytes, nullptr) != 1) break;
        iv = GenerateRandomBytes(kGcmIvBytes);
        if (iv.size() != kGcmIvBytes) break;
        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) != 1) break;

        ciphertext.resize(plaintext.size());
        int len = 0;
        if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                plaintext.data(), static_cast<int>(plaintext.size())) != 1) break;
        int total = len;
        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + total, &len) != 1) break;
        total += len;
        ciphertext.resize(static_cast<size_t>(total));

        tag.resize(kGcmTagBytes);
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kGcmTagBytes, tag.data()) != 1) break;
        ok = true;
    } while (false);
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

std::optional<std::vector<BYTE>> SecureStorage::AesGcmDecrypt(const std::vector<BYTE>& ciphertext,
    const std::vector<BYTE>& key, const std::vector<BYTE>& iv, const std::vector<BYTE>& tag) {
    if (key.size() != kSessionKeyBytes || iv.empty() || tag.size() != kGcmTagBytes) return std::nullopt;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return std::nullopt;
    std::optional<std::vector<BYTE>> result;
    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1) break;
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) != 1) break;

        std::vector<BYTE> plaintext(ciphertext.size());
        int len = 0;
        if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) break;
        int total = len;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()),
                const_cast<BYTE*>(tag.data())) != 1) break;
        // A wrong key/tag fails here (GCM authentication).
        if (EVP_DecryptFinal_ex(ctx, plaintext.data() + total, &len) != 1) break;
        total += len;
        plaintext.resize(static_cast<size_t>(total));
        result = std::move(plaintext);
    } while (false);
    EVP_CIPHER_CTX_free(ctx);
    return result;
}

// ============================================================================
// Random bytes (OpenSSL RAND)
// ============================================================================

std::vector<BYTE> SecureStorage::GenerateRandomBytes(size_t count) {
    std::vector<BYTE> result(count);
    if (count == 0) return result;
    if (RAND_bytes(result.data(), static_cast<int>(result.size())) != 1) {
        result.clear();
    }
    return result;
}

// ============================================================================
// Base64 (OpenSSL EVP)
// ============================================================================

std::string SecureStorage::Base64Encode(const std::vector<BYTE>& data) {
    if (data.empty()) return "";
    std::string result(4 * ((data.size() + 2) / 3), '\0');
    int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(result.data()),
        data.data(), static_cast<int>(data.size()));
    if (len < 0) return "";
    result.resize(static_cast<size_t>(len));
    return result;
}

std::vector<BYTE> SecureStorage::Base64Decode(const std::string& str) {
    if (str.empty()) return {};
    std::vector<BYTE> result(3 * str.size() / 4 + 1);
    int len = EVP_DecodeBlock(result.data(),
        reinterpret_cast<const unsigned char*>(str.data()), static_cast<int>(str.size()));
    if (len < 0) return {};
    size_t out = static_cast<size_t>(len);
    // EVP_DecodeBlock counts padding bytes; strip them.
    size_t pad = 0;
    if (!str.empty() && str.back() == '=') ++pad;
    if (str.size() > 1 && str[str.size() - 2] == '=') ++pad;
    result.resize(out >= pad ? out - pad : 0);
    return result;
}

} // namespace AgentRedactor

#endif // __APPLE__
