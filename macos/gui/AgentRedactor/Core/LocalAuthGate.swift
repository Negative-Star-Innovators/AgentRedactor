import Foundation
import LocalAuthentication

enum AuthGateError: Error, LocalizedError {
    case notAvailable
    case canceled
    case failed
    case secretNotFound

    var errorDescription: String? {
        switch self {
        case .notAvailable: return "Keychain protection is not available on this Mac."
        case .canceled: return LocString("MacPassword_Canceled")
        case .failed: return "Authentication did not succeed."
        case .secretNotFound: return "No protection key is stored for this Mac. If you enabled protection with an older build, reset the app configuration or use the CLI with your original password."
        }
    }
}

/// Windows-Hello-style secret storage for the macOS GUI.
///
/// Enabling protection generates a random secret and stores it in a device-only
/// Keychain item. Every retrieval first requires LocalAuthentication with
/// `.deviceOwnerAuthentication`, which accepts Touch ID or the Mac password.
/// The engine receives the secret through the same POST /unlock flow as Linux.
/// Keychain/authentication failures are surfaced; there is no file fallback.
final class LocalAuthGate {
    private let service = "com.negativestarinnovators.agentredactor"
    private let account = "master-password"

    // MARK: - Store / replace (no prompt; writes by the creating app are free)

    /// Stores (or replaces) the secret. Returns nil on success, or a
    /// user-presentable failure reason.
    @discardableResult
    func storeSecret(_ secret: String) -> String? {
        guard let data = secret.data(using: .utf8) else { return "Invalid secret encoding." }
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
            kSecValueData as String: data,
        ]
        let status = SecItemAdd(query as CFDictionary, nil)
        let result: String?
        if status == errSecDuplicateItem {
            let update: [String: Any] = [kSecValueData as String: data]
            let updateStatus = SecItemUpdate(existingQuery() as CFDictionary, update as CFDictionary)
            result = updateStatus == errSecSuccess
                ? nil : "Keychain update failed (OSStatus \(updateStatus))."
        } else {
            result = status == errSecSuccess
                ? nil : "Keychain store failed (OSStatus \(status))."
        }
        if result == nil { deleteLegacyFileSecret() }
        return result
    }

    // MARK: - Authenticate + retrieve

    /// Presents Apple’s Touch ID / Mac-password prompt directly for disable and
    /// unlock; enabling protection intentionally does not authenticate.
    func authenticate() async -> Result<Void, AuthGateError> {
        let context = LAContext()
        do {
            try await context.evaluatePolicy(
                .deviceOwnerAuthentication,
                localizedReason: LocString("MacLock_Subtitle"))
            return .success(())
        } catch let error as LAError {
            switch error.code {
            case .userCancel, .systemCancel:
                return .failure(.canceled)
            case .biometryNotAvailable, .biometryNotEnrolled, .passcodeNotSet:
                return .failure(.notAvailable)
            default:
                return .failure(.failed)
            }
        } catch {
            return .failure(.failed)
        }
    }

    /// Authenticates before each Keychain read. Canceling is reported
    /// separately from authentication failure.
    func retrieveSecret() async -> Result<String, AuthGateError> {
        switch await authenticate() {
        case .failure(let error):
            return .failure(error)
        case .success:
            return readKeychainSecret()
        }
    }

    private func readKeychainSecret() -> Result<String, AuthGateError> {
        var query = existingQuery()
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        var item: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &item)
        switch status {
        case errSecSuccess:
            guard let data = item as? Data,
                  let secret = String(data: data, encoding: .utf8) else {
                return .failure(.failed)
            }
            return .success(secret)
        case errSecUserCanceled:
            return .failure(.canceled)
        case errSecItemNotFound:
            return .failure(.secretNotFound)
        default:
            return .failure(.failed)
        }
    }

    func deleteSecret() {
        SecItemDelete(existingQuery() as CFDictionary)
        deleteLegacyFileSecret()
    }

    private func deleteLegacyFileSecret() {
        // Pre-release builds used this silent fallback. Never read it; remove
        // it after a successful Keychain write or when protection is disabled.
        try? FileManager.default.removeItem(
            at: AppPaths.configDir.appendingPathComponent("protection.key"))
    }

    private func existingQuery() -> [String: Any] {
        [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
        ]
    }
}
