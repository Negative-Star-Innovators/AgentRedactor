#!/usr/bin/env bash
# One-time setup of a stable self-signed code-signing identity for LOCAL
# Agent Redactor builds ("Agent Redactor Dev"), the macOS equivalent of the
# Windows self-signed MSIX cert: every local build signed with this identity
# is the SAME signer, so Keychain items persist across rebuilds and the
# Hello-style protection (Touch ID / Mac password) can be tested without the
# Apple Developer Program account.
#
# Gatekeeper/distribution: NOT trusted — this is for local testing only.
# The sandbox is unaffected (sandboxed Keychain access still needs real
# provisioning); use the unsigned/ad-hoc store build for sandbox testing.
#
# Idempotent: if the identity already exists in the login keychain, exits 0.
set -euo pipefail

CERT_NAME="Agent Redactor Dev"
KEYCHAIN="${HOME}/Library/Keychains/login.keychain-db"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "${WORKDIR}"' EXIT

if security find-identity -v -p codesigning "${KEYCHAIN}" 2>/dev/null | grep -q "${CERT_NAME}"; then
    echo "==> '${CERT_NAME}' already exists in the login keychain — nothing to do."
    security find-identity -v -p codesigning "${KEYCHAIN}" | grep "${CERT_NAME}"
    exit 0
fi

echo "==> Generating a self-signed code-signing certificate (ECC 256)"
# ECC P-256 private key + self-signed certificate with the codesigning EKU.
openssl ecparam -genkey -name prime256v1 -noout -out "${WORKDIR}/dev.key"
openssl req -new -x509 -key "${WORKDIR}/dev.key" -out "${WORKDIR}/dev.crt" -days 3650 \
    -subj "/CN=${CERT_NAME}/O=Negative Star Innovators (local dev)" \
    -addext "keyUsage=critical,digitalSignature" \
    -addext "extendedKeyUsage=codeSigning"

# The p12 password protects the transient bundle only; it is discarded with
# the work dir. Import may prompt to allow keychain access — approve it.
# -legacy: OpenSSL 3's default PBES2 algorithms are not accepted by macOS's
# security importer.
openssl pkcs12 -export -out "${WORKDIR}/dev.p12" \
    -inkey "${WORKDIR}/dev.key" -in "${WORKDIR}/dev.crt" \
    -name "${CERT_NAME}" -passout pass:agentredactor-dev -legacy

echo "==> Importing into the login keychain (may prompt for keychain access)"
security import "${WORKDIR}/dev.p12" -k "${KEYCHAIN}" -P agentredactor-dev \
    -T /usr/bin/codesign -T /usr/bin/security

echo "==> Trusting the certificate for code signing (local)"
# Make the cert trusted for code signing in the user trust settings so
# codesign accepts it without fuss.
sudo security add-trusted-cert -d -r trustRoot -k /Library/Keychains/System.keychain \
    -p codeSign "${WORKDIR}/dev.crt" 2>/dev/null \
    || security add-trusted-cert -r trustAsRoot -k "${KEYCHAIN}" -p codeSign "${WORKDIR}/dev.crt" \
    || echo "    (trust setting skipped — codesign still works; verify with the next command's output)"

security find-identity -v -p codesigning "${KEYCHAIN}" | grep "${CERT_NAME}"
echo "==> Done. Sign builds with: codesign -s '${CERT_NAME}' ..."
