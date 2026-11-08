# Agent Redactor — macOS Agent Notes

This folder builds the engine/CLI (`macos/engine/`, CMake — see
[README.md](README.md)) and the SwiftUI GUI (`macos/gui/`).

## Layout

- `engine/` — dual-mode engine/CLI binary (`agentredactor`) build via CMake;
  output lands in `macos/build/engine/agentredactor`.
- `gui/` — SwiftUI thin client over the engine's loopback control API (ZERO
  C++; HTTP+JSON only). Project is generated with XcodeGen from
  `gui/project.yml`; sources live in `gui/AgentRedactor/`.
- `build-app.sh` — xcodegen + xcodebuild Release build, then assembles
  `macos/build/AgentRedactor.app` with the engine copied to
  `Contents/Resources/agentredactor` and ad-hoc signed (no sandbox; hardened
  runtime comes with release packaging). The engine binary is expected at the
  canonical path `macos/build/agentredactor` (the CMake output
  `macos/build/engine/agentredactor` is picked up automatically).

## GUI quick build

```bash
brew install xcodegen   # once
macos/build-app.sh      # needs the engine built (see README.md above)
open macos/build/AgentRedactor.app
```

## Conventions

- The GUI mirrors the Windows/Linux thin-client architecture: read
  `<configDir>/control.json`, bearer-authenticated requests to
  `127.0.0.1:<port>`; `<configDir>` is
  `~/Library/Application Support/AgentRedactor` with the shared
  `AGENTREDACTOR_CONFIG_DIR` override. Control-API shapes must match
  `core/src/engine_app.cpp` exactly — the same contract the other two GUIs use.
- Localization: `gui/AgentRedactor/Localizable.xcstrings` (String Catalog) +
  `strings-manifest.json` (runtime manifest used by `Loc.swift` for live
  language switching; fallback = exact tag -> base language -> en, mirroring
  `linux/gui/translator_loader.cpp`). Keys are `.resw` identifiers; mac-only
  strings use the `Mac*` prefix; translated per-language overlays live in
  `macos/strings/<lang>/Resources.resw` (merged on top of the windows/Strings/
  catalogs; missing keys fall back to English). The 53-language list lives in `Localization/Languages.swift` — keep
  it in sync with `SUPPORTED_LANGUAGES` in `core/include/constants.h`.
- Master password: the engine side uses the typed-password flow (non-Windows
  path). The GUI gates it with LocalAuthentication (Touch ID / Mac password)
  and stores the secret in the Keychain (device-only accessibility) purely so
  Touch ID can unlock: LA -> Keychain -> POST /unlock. Touch ID never replaces
  the engine-side secret. The CLI shares that Keychain item
  (`core/src/cli_native_auth_mac.mm`): interactive terminals unlock gated
  commands with the Touch ID / Mac password prompt and `password enable`
  generates the random secret the same way; piped/non-interactive invocations
  keep the typed flow (Linux parity). There is no file-based fallback: Keychain
  failures are surfaced, and this feature is tested with properly signed builds.
  The GUI locks immediately when hidden to the tray, after 30 seconds with the
  app inactive (canceled when reactivated), and after 10 minutes of inactivity.
  Enabling protection does not prompt; disabling and unlocking authenticate.
  Background focus locking never interrupts another app: authentication is
  deferred until the user returns to a visible Agent Redactor window.
- Update check: `UI/Settings/UpdateProvider.swift` fetches the Velopack feed
  `https://api.agentredactor.negativestarinnovators.com/updates/osx-arm64`;
  `UI/Settings/VelopackUpdateProvider.swift` (compiled in only with
  `SELFRELEASE`, set by `build-app.sh --selfrelease` / `build-release.sh`)
  performs the real check → download → apply-on-exit through the dlopen'd
  `velopack_libc_osx.dylib` bundled in `Contents/Frameworks`; without the
  flag the feed-check stub remains. Release packaging (`build-release.sh`,
  channel `osx-arm64`) lives in this folder; the release E2Es are in
  `tests/macos/`, and `tools/velopack-harness/` verifies the binding without
  vpk.
- Three build flavors (mirrors the Windows MSIX/self-release split):
  `build-app.sh` (dev, no updater), `--selfrelease` (Velopack updater,
  hardened runtime via `build-release.sh`), and
  `--appstore` (`APPSTORE` condition: updater + `UpdatesCard`
  compiled out; sandbox entitlements via
  `build-store.sh`, which also BAKES the model weights in — Store updates are
  delta-based). Never mix `--selfrelease` and `--appstore`.

## Release signing and distribution

- Never commit private keys (`.p8` / `.p12`), keychain exports, API
  credentials, or real account, identity, and team details. Local signing uses
  the login Keychain and a saved notarytool profile. GitHub signing secrets live
  only in protected environments; PRs never receive them.
- `release-macos.yml` is the reusable unsigned PR/signed-candidate build. The
  protected `macos-selfrelease` environment in `release-macos-signed.yml` gates
  Developer ID signing, notarization, signed tests, and R2 publication. Missing
  signing credentials are fatal for a signed run, and publication verifies the
  stapled DMG again before upload. Merges to main produce signed test candidates;
  tags are the normal publication path and manual dispatch supports dry-runs/recovery.
- `release-macos-store.yml` builds an unsigned sandboxed Store package on PRs.
  Its separate protected `macos-appstore` environment gates Apple Distribution
  signing and signed Store tests. The signed candidate is retained for manual
  download/submission through App Store Connect; there is no automatic upload,
  R2 operation, or required TestFlight step.
- **Self-distribution:** `build-release.sh` uses a Developer ID Application
  identity (`SIGN_APP_IDENTITY`) for the app, nested code, and DMG,
  `packaging/AgentRedactor.entitlements` through `SIGN_ENTITLEMENTS`, and a
  saved notarytool profile through `NOTARY_PROFILE`:

  ```bash
  SIGN_APP_IDENTITY="Developer ID Application: ..." \
  SIGN_ENTITLEMENTS="$PWD/macos/packaging/AgentRedactor.entitlements" \
  NOTARY_PROFILE="..." ./macos/build-release.sh
  ```

  The script signs the engine and every bundled dylib innermost-first with the
  hardened runtime and secure timestamps, then signs the outer app. It creates
  `AgentRedactor-osx-arm64.dmg` containing the app and an `/Applications`
  symlink for drag-and-drop installation, signs/notarizes/staples the DMG, and
  uses Velopack only for the nupkg/feed self-update artifacts. No Developer ID
  Installer certificate or PKG is needed. Never use `--no-sign` for artifacts
  intended for users. Output lands in `build-release/velopack/`; do not upload
  it to the live update feed as part of a signing test.
- **Mac App Store:** `build-store.sh` uses an Apple Distribution identity
  (`SIGN_APP_IDENTITY`) plus `APPLE_TEAM_ID` and the store entitlements. It is
  sandboxed and has no Velopack updater; Developer ID certificates and
  notarytool are not part of the Store upload flow. Package the staged app with
  `ditto` and upload through Transporter or the documented `altool` flow.
- Before publishing, verify the app/DMG signatures, notarization ticket
  (`xcrun stapler validate`), and Gatekeeper assessment (`codesign` and
  `spctl`). Signed authentication coverage is in `tests/macos/test_signed_auth.py`;
  the unsigned SwiftUI Accessibility GUI tests live in `tests/gui/macos/`.
  A successful local build alone does not prove release validation passed.
