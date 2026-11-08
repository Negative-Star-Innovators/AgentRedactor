#pragma once

// macOS-only helper for the CLI's native protection flow (the Windows Hello
// counterpart). The GUI (LocalAuthGate.swift) enables protection with a
// random secret stored in a device-only Keychain item. Every read is preceded
// by LocalAuthentication (Touch ID / Mac login password). The CLI uses the
// same item — interactive sessions unlock with
// Touch ID instead of a typed master password, and `password enable`
// generates/stores the secret the same way, so GUI and CLI interoperate.
// There is no file fallback: Keychain failures are surfaced. The typed flow is
// used only for piped sessions or when no native secret exists (protection
// enabled by an older build). Only macos/engine wires this; other platforms
// never include the header's callers.

#include <string>

namespace AgentRedactor {
namespace CliNativeAuth {

enum class SecretReadResult {
    Ok,       // out holds the UTF-8 secret
    Canceled, // user dismissed the Touch ID / Mac password prompt
    NotFound, // no native secret stored (legacy typed-flow secret)
    Failed,   // Keychain error or authentication failure
};

// Authenticates with LocalAuthentication before reading the GUI-stored secret.
// Honors AGENTREDACTOR_DISABLE_KEYRING=1 (Keychain skipped, yielding NotFound).
SecretReadResult ReadProtectionSecret(std::string& out);

// Generates and stores a random 256-bit secret exactly the way the GUI does,
// in a device-only Keychain item. Returns false on Keychain failure.
bool CreateAndStoreProtectionSecret(std::string& secretOut);

// Removes the stored secret (protection disabled / uninstall cleanup).
void DeleteProtectionSecret();

} // namespace CliNativeAuth
} // namespace AgentRedactor
