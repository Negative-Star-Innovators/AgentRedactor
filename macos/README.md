# Agent Redactor — macOS build

For installing and *using* Agent Redactor (end users), see the
[root README](../README.md), and for the scriptable `agentredactor` CLI see
[docs/cli.md](../docs/cli.md) — this document covers building and developing
on macOS (Apple silicon; engine + CLI via CMake, the SwiftUI GUI, and the
Velopack release packaging).

Build the engine/CLI (`agentredactor`) on macOS 14+:

```bash
brew install cmake ninja nlohmann-json openssl@3

# Fetches the pinned osx-arm64 onnxruntime tarball (sha256-checked) into
# macos/third_party/ (gitignored) and verifies the prerequisites:
macos/fetch-deps.sh

cmake -B macos/build -G Ninja -S macos
cmake --build macos/build
```

The engine also needs the NER model files. `config.json`, `tokenizer.json`,
`viterbi_calibration.json` and `onnx/model_quantized.onnx` are staged next to
the dev binary by CMake; the ~1.6 GB `onnx/model_quantized.onnx_data` weights
are downloaded automatically on first run (or grab them directly from
<https://api.agentredactor.negativestarinnovators.com/models/model_quantized.onnx_data>).

Notes:

- Secure storage uses OpenSSL 3 (Homebrew) for the AES-256-GCM envelope,
  byte-identical to the Linux envelope, so settings files stay cross-platform
  portable. The machine key is a random 256-bit key persisted in
  `machine.key` (config dir), wrapped by a key derived from the IOPlatformUUID
  — fast, silent, headless-safe, and never blocks on a Keychain authorization
  prompt. (Early macOS builds stored the machine key in the Keychain; it is
  still read once from there for migration if no `machine.key` exists, then
  never written back. A per-app Keychain ACL was rejected deliberately: the
  first `SecItemCopyMatching` stalled ~10 s inside control-API requests,
  freezing the GUI mid-action, and added no real protection beyond the
  Linux-libsecret / DPAPI-user-scope threat model.) The engine resolves and
  caches the machine key on a background thread at startup
  (`SecureStorage::Prewarm`), so requests never pay the first-access cost.
  `AGENTREDACTOR_DISABLE_KEYRING=1` skips even the one-time migration read
  (headless/SSH/CI).
- The config dir is `~/Library/Application Support/AgentRedactor`
  (`AGENTREDACTOR_CONFIG_DIR` overrides it, same as the other platforms).
- CLI on PATH: the engine binary doubles as the CLI. On launch the app
  installs it silently as `~/.agentredactor/bin/agentredactor` and appends
  the dir to the user's shell PATH (`~/.kimi-code/bin` pattern — no admin
  prompt; Velopack's pkg takes no custom post-install script). A sandboxed
  App Store build cannot offer this (sandboxed apps cannot create PATH
  commands) and skips the install.
- Single-instance is an `flock` on `<configDir>/engine.lock`; a bare run in a
  terminal starts the engine detached and returns, `--console` runs it in the
  foreground.

Run the tests from the repo root (one pytest process per suite — the suites
share the `conftest` module name and cannot be collected together):

```bash
cd tests
python -m pytest cli -q
python -m pytest migration -q
```

Like the Linux CI, the CLI suite needs the model weights reachable: either let
the engine's first-run download finish, or place
`model_quantized.onnx_data` into `macos/build/engine/models/onnx/`. Point the
suites at the dev binary with:

```bash
AGENTREDACTOR_ENGINE_BIN=$PWD/../macos/build/engine/agentredactor python -m pytest cli -q
AGENTREDACTOR_EXE=$PWD/../macos/build/engine/agentredactor python -m pytest migration -q
```

## Release packaging (Velopack, osx-arm64)

`macos/build-release.sh` produces the self-release channel artifacts for
Apple silicon (channel `osx-arm64`): release engine build + `build-app.sh`,
staged `AgentRedactor.app` (with model companions and bundled dylibs), a
drag-and-drop `AgentRedactor-osx-arm64.dmg` containing the app and an
Applications shortcut, plus Velopack `*-full.nupkg` and
`releases.osx-arm64.json` for self-updates. Users open the DMG and drag Agent
Redactor to Applications once; subsequent updates use the nupkg feed, not new
DMGs. App/DMG signing and DMG notarization are driven by `SIGN_APP_IDENTITY` /
`SIGN_ENTITLEMENTS` / `NOTARY_PROFILE`; `--no-sign` creates a local/CI test
artifact. No installer certificate is needed. Upload to R2 with `vpk upload s3
... -c osx-arm64 --prefix osx-arm64` (same bucket and secrets as
Windows/Linux); `.github/workflows/release-macos.yml` runs the whole flow on
tags and as a dry-run on PRs. Prereqs: the .NET SDK and the pinned Velopack
CLI (`dotnet tool install -g vpk --version 1.2.0` — keep it in sync with
`macos/fetch-velopack.sh`).

## Mac App Store build (sandboxed channel)

`macos/build-store.sh` produces the sandboxed Store variant in
`macos/build-store/app/AgentRedactor.app` (MSIX parity):

- `build-app.sh --appstore` compiles the `APPSTORE` Swift condition: no
  Velopack updater (the Store updates the app) and no CLI-on-PATH auto
  install (sandboxed apps cannot create PATH entries).
- The AI model weights are **baked in** (unlike the self-release channel's
  first-run download): Store updates are delta-based, so the 1.6 GB is
  downloaded once. Weights are fetched from the R2 models endpoint into
  `build-store/models-cache/` when not already present (they are not
  committed, same as the MSIX CI).
- Entitlements (`packaging/AgentRedactor-Store.entitlements`): app sandbox,
  `network.client`/`network.server` (the loopback proxy), and a
  Team-ID-substituted `keychain-access-groups` entry so the GUI and the
  bundled engine binary share Keychain items. The config dir needs no code
  change: sandboxed processes get `HOME` = the app container, so the
  existing `~/Library/Application Support/AgentRedactor` path resolves
  inside it. The nested engine binary inherits the sandbox at spawn.
- Signing: `SIGN_APP_IDENTITY` ("Apple Distribution: ...") +
  `APPLE_TEAM_ID`. The bundled ONNX Runtime / OpenSSL dylibs are re-signed
  with the same identity — the App Store rejects
  `disable-library-validation`. Without credentials (`--no-sign`, the
  default) the app is ad-hoc signed **with the store entitlements**, giving
  a sandboxed local test build before the Developer Program account exists.
- Upload: zip the staged app (`ditto -c -k --sequesterRsrc --keepParent`),
  then `xcrun altool --validate-app` / `--upload-app -t macos` (or
  Transporter); release manually in App Store Connect like the Windows MSIX
  flow. A store CI workflow is added when the account/certs exist.

## Self-updating (self-release only)

The GUI self-update flow is the macOS port of `linux/gui/update_manager.cpp`
and mirrors the Windows build's gating:

- `build-app.sh --selfrelease` (used by `build-release.sh`) compiles the
  updater in via the `SELFRELEASE` Swift condition and bundles
  `velopack_libc_osx.dylib` (fetched by `fetch-velopack.sh`) into
  `Contents/Frameworks/`. Dev builds (`macos/build-app.sh`) leave the
  updater out entirely — the settings update entry keeps a feed-check-only
  stub that says apply is unavailable, exactly like the MSIX build excluding
  `Update.exe`; a future sandboxed/App-Store build gets the same exclusion
  for free by not passing the flag.
- The dylib is **dlopen'd at runtime** (`VelopackUpdateProvider.swift`), not
  linked — its install name is a build-machine path, and dev builds must
  compile and run without the gitignored blob. There is no build-time header
  dependency either; the struct mirrors in that file correspond field for
  field to `third_party/velopack/include/Velopack.h`.
- Flow: launch-time background feed check (Linux `MainWindow` parity) →
  newer release found → nupkg downloaded through the libc API (delta-aware,
  falls back to full) → restart prompt with version + release notes →
  `vpkc_wait_exit_then_apply_updates`: Velopack's own `UpdateMac` — which
  `vpk pack` places in `Contents/MacOS/` — waits (60 s) for the process to
  exit, swaps the app bundle, and relaunches. No hand-rolled file swapping.
  `vpkc_app_run` at launch (SELFRELEASE) consumes post-update hooks and
  applies a staged package if a previous run crashed mid-apply.
- Test hooks (self-release builds only, same contract as Windows/Linux):
  `AGENTREDACTOR_UPDATE_FEED` overrides the feed URL, **loopback http only**
  (`http://127.0.0.1*`/`http://localhost*`); anything else is ignored.
  `AGENTREDACTOR_UPDATE_AUTOAPPLY=1` applies without prompting.
- Hardened runtime: the app uses
  `com.apple.security.cs.disable-library-validation` from
  `macos/packaging/AgentRedactor.entitlements`. Release builds re-sign the
  Velopack, ONNX Runtime, and OpenSSL dylibs with the same Developer ID before
  signing the outer app; vpk receives the same entitlement for its update
  package copy. Ad-hoc-signed dev builds have no library validation to begin
  with.
- The release E2Es live in `tests/macos/` (see its README). Without
  dotnet/vpk, `macos/tools/velopack-harness/run.sh` verifies the binding
  against a loopback feed: dlopen + symbol resolution, loopback gating,
  version detection with release notes, and nupkg download into Velopack's
  package cache (apply itself needs a real vpk-packed bundle).

## Clean slate (fresh-install testing)

`macos/clean-slate.sh` (add `--yes` to skip the prompt) wipes everything the
app persists across all local flavors — dev, Velopack self-release, and the
sandboxed App Store build: both config locations (`~/Library/Application
Support/AgentRedactor` and the store container), settings, profiles, stored
API keys, logs, the machine key, downloaded models, and both Keychain items.
Built apps and the repo are untouched.
