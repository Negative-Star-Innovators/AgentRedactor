// macOS CLI protection uses the GUI's device-only Keychain item. Every read is
// preceded by LocalAuthentication (Touch ID / Mac password). There is no file
// fallback; Keychain and authentication failures are surfaced.

#include "cli_native_auth.h"

#ifdef __APPLE__

#include "utils.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <CoreFoundation/CoreFoundation.h>
#include <LocalAuthentication/LocalAuthentication.h>
#include <Security/Security.h>
#include <dispatch/dispatch.h>

#include <cstdlib>
#include <filesystem>

namespace AgentRedactor {
namespace CliNativeAuth {

namespace {

constexpr size_t kSecretBytes = 32;

CFStringRef ProtectionService() {
    return CFSTR("com.negativestarinnovators.agentredactor");
}
CFStringRef ProtectionAccount() {
    return CFSTR("master-password");
}

bool KeychainAllowed() {
    if (const char* dis = std::getenv("AGENTREDACTOR_DISABLE_KEYRING");
        dis && *dis && std::string(dis) != "0") {
        return false;
    }
    return true;
}

void DeleteLegacyFileSecret() {
    std::error_code ec;
    std::filesystem::remove(Utils::GetAppDataPath() / "protection.key", ec);
}

std::string Base64Encode(const std::vector<uint8_t>& data) {
    if (data.empty()) return "";
    std::string result(4 * ((data.size() + 2) / 3), '\0');
    const int len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(result.data()),
        data.data(), static_cast<int>(data.size()));
    if (len < 0) return "";
    result.resize(static_cast<size_t>(len));
    return result;
}

std::vector<uint8_t> RandomBytes(size_t count) {
    std::vector<uint8_t> result(count);
    if (count != 0 && RAND_bytes(result.data(), static_cast<int>(count)) != 1) {
        result.clear();
    }
    return result;
}

CFDictionaryRef ExistingItemQuery() {
    const void* keys[] = { kSecClass, kSecAttrService, kSecAttrAccount };
    const void* values[] = { kSecClassGenericPassword, ProtectionService(), ProtectionAccount() };
    return CFDictionaryCreate(kCFAllocatorDefault, keys, values, 3,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

bool KeychainItemExists() {
    CFDictionaryRef query = ExistingItemQuery();
    if (!query) return false;
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    if (result) CFRelease(result);
    CFRelease(query);
    return status == errSecSuccess;
}

SecretReadResult AuthenticateUser() {
    LAContext* context = [[LAContext alloc] init];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block SecretReadResult result = SecretReadResult::Failed;
    [context evaluatePolicy:LAPolicyDeviceOwnerAuthentication
            localizedReason:@"Unlock Agent Redactor"
                      reply:^(BOOL success, NSError* error) {
        if (success) {
            result = SecretReadResult::Ok;
        } else if (error.code == LAErrorUserCancel || error.code == LAErrorSystemCancel) {
            result = SecretReadResult::Canceled;
        }
        dispatch_semaphore_signal(done);
    }];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    return result;
}

bool StoreInKeychain(const std::string& secret) {
    CFDataRef data = CFDataCreate(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(secret.data()), static_cast<CFIndex>(secret.size()));
    if (!data) return false;
    const void* keys[] = {
        kSecClass, kSecAttrService, kSecAttrAccount, kSecAttrAccessible, kSecValueData
    };
    const void* values[] = {
        kSecClassGenericPassword, ProtectionService(), ProtectionAccount(),
        kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly, data
    };
    CFDictionaryRef query = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 5,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const OSStatus status = query ? SecItemAdd(query, nullptr) : errSecParam;
    if (query) CFRelease(query);
    CFRelease(data);
    if (status == errSecSuccess) return true;
    if (status != errSecDuplicateItem) return false;

    CFDictionaryRef existing = ExistingItemQuery();
    CFDataRef updated = CFDataCreate(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(secret.data()), static_cast<CFIndex>(secret.size()));
    if (!updated) {
        if (existing) CFRelease(existing);
        return false;
    }
    const void* updateKeys[] = { kSecValueData };
    const void* updateValues[] = { updated };
    CFDictionaryRef update = CFDictionaryCreate(kCFAllocatorDefault,
        updateKeys, updateValues, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const OSStatus updateStatus = (existing && update)
        ? SecItemUpdate(existing, update) : errSecParam;
    if (existing) CFRelease(existing);
    if (update) CFRelease(update);
    CFRelease(updated);
    return updateStatus == errSecSuccess;
}

SecretReadResult ReadFromKeychain(std::string& out) {
    CFDictionaryRef base = ExistingItemQuery();
    if (!base) return SecretReadResult::Failed;
    CFMutableDictionaryRef query = CFDictionaryCreateMutableCopy(
        kCFAllocatorDefault, 4, base);
    CFRelease(base);
    if (!query) return SecretReadResult::Failed;
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status == errSecSuccess && result) {
        if (CFGetTypeID(result) == CFDataGetTypeID()) {
            const CFDataRef data = static_cast<CFDataRef>(result);
            out.assign(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                static_cast<size_t>(CFDataGetLength(data)));
            CFRelease(result);
            return out.empty() ? SecretReadResult::Failed : SecretReadResult::Ok;
        }
        CFRelease(result);
        return SecretReadResult::Failed;
    }
    if (status == errSecUserCanceled) return SecretReadResult::Canceled;
    if (status == errSecItemNotFound) return SecretReadResult::NotFound;
    return SecretReadResult::Failed;
}

} // anonymous namespace

SecretReadResult ReadProtectionSecret(std::string& out) {
    if (!KeychainAllowed() || !KeychainItemExists()) return SecretReadResult::NotFound;
    const SecretReadResult authenticated = AuthenticateUser();
    if (authenticated != SecretReadResult::Ok) return authenticated;
    return ReadFromKeychain(out);
}

bool CreateAndStoreProtectionSecret(std::string& secretOut) {
    if (!KeychainAllowed()) return false;
    const auto secret = RandomBytes(kSecretBytes);
    if (secret.size() != kSecretBytes) return false;
    const std::string text = Base64Encode(secret);
    if (text.empty() || !StoreInKeychain(text)) return false;
    DeleteLegacyFileSecret();
    secretOut = text;
    return true;
}

void DeleteProtectionSecret() {
    if (KeychainAllowed()) {
        if (CFDictionaryRef query = ExistingItemQuery()) {
            SecItemDelete(query);
            CFRelease(query);
        }
    }
    DeleteLegacyFileSecret();
}

} // namespace CliNativeAuth
} // namespace AgentRedactor

#else // !__APPLE__

namespace AgentRedactor {
namespace CliNativeAuth {

SecretReadResult ReadProtectionSecret(std::string&) { return SecretReadResult::NotFound; }
bool CreateAndStoreProtectionSecret(std::string&) { return false; }
void DeleteProtectionSecret() {}

} // namespace CliNativeAuth
} // namespace AgentRedactor

#endif // __APPLE__
