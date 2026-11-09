# Release pipeline improvements

This is a planning list, not authorization to change the release process. **Ask the user before implementing any item on this list.** No item should be implemented merely because it appears here.

## macOS: promote tested candidates

Status: approved and implemented.

Signed candidate builds on `main` retain the package, installation, update, Keychain, and LocalAuthentication tests. Once the exact candidate has been tested and approved, it is published or submitted without a tag-time rebuild.

For self-distribution, the protected publish-only job verifies the candidate run and version; on tags it also verifies that the candidate and tag identify the same commit. It rechecks the Developer ID signature and notarization, uploads the existing DMG and Velopack files to R2, and verifies that the live DMG is identical to the candidate.

For the App Store, manually submit the existing Apple Distribution-signed ZIP from the tested `main` candidate. Version tags do not trigger another Store build. Record the candidate run ID and commit used for each submission so the submitted binary remains traceable.

The candidate must already contain the final version. Changing the version after testing requires a new candidate. If the candidate artifact has expired, rebuild and test a new candidate instead of bypassing the checks.

## Linux self-release: reuse the main candidate

Status: deferred until a coordinated cross-platform release; not implemented.

`build-linux.yml` currently builds on pull requests, merges to `main`, and version tags. A tag for an already-tested `main` commit therefore repeats the build and tests. Add a protected publish-only path that verifies the commit and version and uploads the exact tested x64 and ARM64 artifacts from the successful `main` run.

Do not publish pull-request artifacts. Preserve the requirement that both architectures build and test successfully before either channel is published.

## Windows self-release: evaluate final-candidate reuse

Status: deferred until a coordinated cross-platform release; not implemented.

The Windows self-release workflow currently builds on pull requests and version tags, not ordinary merges to `main`. It therefore does not have the same `main`-to-tag duplication as Linux and macOS. Pull-request artifacts may also use a synthetic version and must never be promoted directly.

As part of the coordinated release redesign, evaluate producing a final-version, fully tested candidate on `main` or through an explicit candidate dispatch, then promoting those exact x64 and ARM64 packages to R2. Preserve the existing GUI-test gates and both-architecture publication requirement.

## Coordinated cross-platform release: selective CI and publication

Status: deferred until the next release that intentionally includes Windows, Linux, and macOS; not implemented.

Add platform-aware change detection for pull requests, merge candidates, and releases. Pull requests should still build and test immediately, but only on affected platforms. Platform-only changes should not trigger the other platforms; shared C++ code, shared model files, shared tests, and release-workflow changes should trigger every platform they can affect. Preserve successful required-check reporting when an unrelated platform's expensive jobs are skipped.

On merge, build final-version candidates only for affected platforms. On release, publish only the exact tested candidates for those platforms, leaving the other platforms' R2 channels and released versions unchanged. Merges must never publish self-release updates automatically.

Before enabling this shared release design, run full dry-runs and automated installation, upgrade, GUI, and self-release tests for all three platforms. Manually smoke-test the Windows and Linux self-release artifacts as well as macOS, then publish all three only after the coordinated release is approved. Thereafter, platform-only releases can use the selective process without rebuilding unchanged platforms.

## Windows MSIX: retain artifact reuse

No change is currently recommended. The MSIX workflow already publishes the exact packages built and tested on `main` to the rolling `latest` prerelease. Use this as a reference for artifact reuse rather than adding another build stage.
