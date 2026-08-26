// Linux SecureStorage: OpenSSL AES-256-GCM + PBKDF2-HMAC-SHA256.
//
// Two modes, mirroring the Windows semantics:
//   - Unprotected: fields are encrypted with a random 32-byte machine key
//     stored in the libsecret keyring (envelope _mode "machine"; the DPAPI
//     counterpart). On headless servers without a keyring the key is derived
//     from /etc/machine-id via PBKDF2, which only obscures secrets from other
//     users on the same machine — a first-run warning is logged.
//   - Protected: a typed master password. A random 32-byte AES session key is
//     wrapped with a PBKDF2-HMAC-SHA256 key derived from the password and only
//     the wrapped blob is persisted (master_password.password in settings.json).
//     The session starts locked; UnlockWithPassword recovers the session key.

#include "secure_storage.h"
#include "utils.h"
#include "logging.h"

#ifndef _WIN32

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <libsecret/secret.h>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace AgentRedactor {

namespace {

constexpr uint32_t kPbkdf2Iterations = 600000;
constexpr size_t kSessionKeyBytes = 32;  // AES-256
constexpr size_t kSaltBytes = 16;
constexpr size_t kGcmIvBytes = 12;
constexpr size_t kGcmTagBytes = 16;

// libsecret schema for the machine key entry.
const SecretSchema* MachineKeySchema() {
    static const SecretSchema schema = {
        "com.negativestarinnovators.AgentRedactor", SECRET_SCHEMA_NONE,
        {
            { "app", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING },
        }
    };
    return &schema;
}

// True when a Secret Service daemon owns org.freedesktop.secrets on the
// session bus. secret_password_*_sync below block for the full default D-Bus
// timeout (~25 s each) when the name is activatable but never actually
// answers — e.g. a dbus-run-session/systemd --user session where keyring
// activation starts a daemon that cannot serve (locked keyring, prompter
// with no display, or a stale GNOME_KEYRING_CONTROL pointing at another
// session's daemon). Two such calls stalled headless engine startup ~52 s
// and tripped the GUI's engine-spawn watchdog. A service that owns the name
// answers promptly; anything else takes the machine-id fallback, which the
// headless path below already implements.
bool SecretServiceOwned() {
    GError* error = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (error) g_error_free(error);
    if (!bus) return false;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "NameHasOwner",
        g_variant_new("(s)", "org.freedesktop.secrets"),
        G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE,
        2000, nullptr, &error);
    g_object_unref(bus);
    if (!reply) {
        if (error) g_error_free(error);
        return false;
    }
    gboolean owned = FALSE;
    g_variant_get(reply, "(b)", &owned);
    g_variant_unref(reply);
    return owned == TRUE;
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

std::optional<std::vector<BYTE>> SecureStorage::MachineKey() {
    // Cached: keyring round-trips on every field encrypt/decrypt would be
    // needlessly slow.
    static std::optional<std::vector<BYTE>> cached;
    static bool warnedFallback = false;
    if (cached) return cached;

    // Only talk to libsecret when a Secret Service actually owns the name;
    // otherwise each sync call can stall for the full D-Bus timeout before
    // failing (see SecretServiceOwned). AGENTREDACTOR_DISABLE_KEYRING=1 skips
    // the keyring entirely — for headless servers and CI, where an
    // activatable-but-broken daemon can still own the name without serving.
    static const bool keyringOwned = [] {
        if (const char* dis = std::getenv("AGENTREDACTOR_DISABLE_KEYRING");
            dis && *dis && std::string(dis) != "0") {
            return false;
        }
        return SecretServiceOwned();
    }();
    if (keyringOwned) {
        GError* error = nullptr;
        gchar* stored = secret_password_lookup_sync(MachineKeySchema(), nullptr, &error,
            "app", "agentredactor", nullptr);
        if (stored) {
            auto key = Base64Decode(stored);
            secret_password_free(stored);
            if (key.size() == kSessionKeyBytes) {
                cached = key;
                return cached;
            }
        }
        if (error) {
            g_error_free(error);
            error = nullptr;
        }

        auto key = GenerateRandomBytes(kSessionKeyBytes);
        if (key.size() == kSessionKeyBytes) {
            const std::string encoded = Base64Encode(key);
            if (secret_password_store_sync(MachineKeySchema(), SECRET_COLLECTION_DEFAULT,
                    "Agent Redactor machine key", encoded.c_str(), nullptr, &error,
                    "app", "agentredactor", nullptr)) {
                cached = key;
                return cached;
            }
            if (error) {
                g_error_free(error);
                error = nullptr;
            }
        }
    }

    // No keyring (headless server): derive a stable key from the machine id.
    // This only protects secrets from other local users; anyone reading both
    // /etc/machine-id and settings.json can unwrap them.
    if (!warnedFallback) {
        warnedFallback = true;
        LOG_LIFECYCLE(L"[SecureStorage] No secret keyring available; deriving the at-rest key "
            L"from the machine id. Secrets are only obfuscated, not protected. "
            L"Install gnome-keyring (or another Secret Service provider) for stronger storage.");
    }
    std::ifstream machineId("/etc/machine-id", std::ios::binary);
    if (!machineId) return std::nullopt;
    std::string id((std::istreambuf_iterator<char>(machineId)), std::istreambuf_iterator<char>());
    if (id.empty()) return std::nullopt;
    static const std::vector<BYTE> kMachineSalt = {
        'a','g','e','n','t','r','e','d','a','c','t','o','r','-','m','k'
    };
    auto derived = Pbkdf2(id, kMachineSalt, 10000);
    if (derived.size() != kSessionKeyBytes) return std::nullopt;
    cached = derived;
    return cached;
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
// AES-256-GCM (OpenSSL EVP)
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

#endif // !_WIN32
