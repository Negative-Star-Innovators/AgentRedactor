# Release and product improvements

This is a planning list, not authorization to change the release process or application. **Ask the user before implementing any item on this list.** No item should be implemented merely because it appears here.

## macOS: promote tested candidates

Status: approved and implemented.

Signed candidate builds on `main` retain the package, installation, update, Keychain, and LocalAuthentication tests. Once the exact candidate has been tested and approved, it is published or submitted without a tag-time rebuild.

For self-distribution, the protected publish-only job verifies the candidate run and version; on tags it also verifies that the candidate and tag identify the same commit. It rechecks the Developer ID signature and notarization, uploads the existing DMG and Velopack files to R2, and verifies that the live DMG is identical to the candidate.

For the App Store, manually submit the existing Apple Distribution-signed ZIP from the tested `main` candidate. Version tags do not trigger another Store build. Record the candidate run ID and commit used for each submission so the submitted binary remains traceable.

The candidate must already contain the final version. Changing the version after testing requires a new candidate. If the candidate artifact has expired, rebuild and test a new candidate instead of bypassing the checks.

## Linux self-release: reuse the main candidate

Status: approved and implemented; cross-platform CI validation is required before publication.

`build-linux.yml` builds final-version x64/ARM64 candidates on `main`. Tags resolve the successful candidate at the same commit; manual publication accepts its run ID. The publish-only path verifies the run, versions, and packages, then uploads the existing artifacts. Pull-request artifacts cannot be promoted, and both architectures must have succeeded in the candidate run.

## Windows self-release: final-candidate reuse

Status: approved and implemented; cross-platform CI validation is required before publication.

`release-selfrelease.yml` now produces final-version, fully tested x64/ARM64 candidates on `main`. Tags and manual publication promote those exact packages. The candidate run includes the existing installation, upgrade, migration, locked-save, and GUI-test gates. PR-only synthetic versions cannot be promoted.

## Coordinated cross-platform release: selective CI and publication

Status: approved and implemented; full cross-platform validation is required before staged publication.

`scripts/ci_platform_changes.py` selects affected platforms for pull requests and merge candidates. Platform-only changes skip the other platforms; shared C++, models, translations, common tests, version files, and workflow changes select every affected platform. Build-step gates preserve the existing required per-architecture check names.

Merges build final-version candidates only for affected platforms and never publish self-release updates automatically. Each platform's publication promotes only its own tested candidate, allowing the coordinated code to be released separately to macOS, Windows, and Linux. Full automated installation, upgrade, GUI, and self-release tests remain required, with manual smoke-testing before publication.

## Windows MSIX: retain artifact reuse

No change is currently recommended. The MSIX workflow already publishes the exact packages built and tested on `main` to the rolling `latest` prerelease. Use this as a reference for artifact reuse rather than adding another build stage.

## Cross-platform locked-settings preservation

Status: approved and implemented; cross-platform CI validation is required before publication.

The shared C++ settings manager encrypts in-memory sensitive fields before locking and refuses to save plaintext sensitive fields while locked. A shared `--selftest-locked-settings` regression hook covers global saves while locked, unlock, and restart on all three platforms.

Previous-live upgrade tests on macOS, Windows, and Linux seed and verify API-key, keyword, and regex content and entry flags, profile identity, port, and statistics. Each runs unprotected and password-enabled cases. The protected case unlocks, locks, saves a global setting, unlocks again, and verifies every field after the upgrade.

## Post-unlock GUI profile refresh

Status: approved and implemented; cross-platform GUI validation is required before publication.

All three GUIs reload the complete profile after a locked-to-unlocked transition, including external control-API unlocks. This prevents empty locked-session snapshots from being shown or saved over the decrypted API key, keywords, and regexes. GUI regression tests restore populated profiles after a full restart, verify the actual controls contain the values, save an unrelated profile setting, and confirm that no sensitive field is lost.

## macOS localization completeness

Status: deferred; not implemented.

An audit found complete runtime coverage of all 215 localization keys across all 53 supported languages, with no empty values, missing referenced keys, or numbered-placeholder mismatches. The following gaps remain:

- Move user-facing hardcoded English errors into the shared translation catalogs. This includes engine-connection errors in `macos/gui/AgentRedactor/Core/EngineClient.swift`, authentication and Keychain errors in `LocalAuthGate.swift`, updater errors in `UI/Settings/UpdateProvider.swift` and `VelopackUpdateProvider.swift`, and the unlock fallback in `Core/AppState.swift`.
- Localize the supplemental model-download failure and retry status supplied by the C++ engine. The normal first-install download title, progress, failure heading, and Retry button are already localized; the additional engine-provided status can still be English.
- Review translations that exactly match English where this is not clearly intentional. Product names and technical terms such as Agent Redactor, PII, Regex, URL, CPU, and OK are expected to remain unchanged. Candidates for native-language review include `Agent Redactor - Crash` in eight languages and `AI Detection` in Filipino.
- Repair the `macos/tools/resw_to_xcstrings.py --check` consistency failure. Xcode has changed the String Catalog format/version and normalized `az-Latn` and `ha-Latn` to `az` and `ha`. The runtime `strings-manifest.json` is complete and matches the `.resw` sources, so this is a generated-catalog/tooling issue rather than missing runtime translations.

Preserve the existing verbatim-copy localization workflow: do not regenerate or retranslate existing valid translations. Reuse existing translations and translate only genuinely new or missing strings.
