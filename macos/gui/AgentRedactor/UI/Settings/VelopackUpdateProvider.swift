#if SELFRELEASE

import Foundation

// MARK: - Velopack libc binding
//
// The pinned velopack_libc dylib (macos/third_party/velopack, copied into
// Contents/Frameworks by build-app.sh --selfrelease) is loaded with dlopen so
// dev builds — and any future store/sandbox build shipping without the
// dylib — never hard-depend on it (no header/link dependency at build time;
// the include dir is only needed to regenerate this mirror). Hardened-runtime
// releases load it under the com.apple.security.cs.disable-library-validation
// entitlement (the dylib is third-party/ad-hoc signed, same as the bundled
// onnxruntime and OpenSSL).
//
// The struct layouts below mirror include/Velopack.h (cbindgen output) field
// for field; they only serve as reading lenses over library-owned memory and
// are never passed by value (Swift structs are not C-representable, so the
// function pointer signatures below speak in raw pointers and rebind).

typealias vpkc_manager = OpaquePointer

/// Layout mirror of `vpkc_asset_t`.
struct vpkc_asset_c {
    var PackageId: UnsafeMutablePointer<CChar>?
    var Version: UnsafeMutablePointer<CChar>?
    var AssetType: UnsafeMutablePointer<CChar>? // C field `Type`
    var FileName: UnsafeMutablePointer<CChar>?
    var SHA1: UnsafeMutablePointer<CChar>?
    var SHA256: UnsafeMutablePointer<CChar>?
    var Size: UInt64
    var NotesMarkdown: UnsafeMutablePointer<CChar>?
    var NotesHtml: UnsafeMutablePointer<CChar>?
}

/// Layout mirror of `vpkc_update_info_t`.
struct vpkc_update_info_c {
    var TargetFullRelease: UnsafeMutablePointer<vpkc_asset_c>?
    var BaseRelease: UnsafeMutablePointer<vpkc_asset_c>?
    var DeltasToTarget: UnsafeMutablePointer<UnsafeMutablePointer<vpkc_asset_c>?>?
    var DeltasToTargetCount: Int
    var IsDowngrade: Bool
}

extension UnsafeMutableRawPointer {
    /// Rebinds library-owned memory to the `vpkc_update_info_t` layout mirror.
    fileprivate var updateInfo: UnsafeMutablePointer<vpkc_update_info_c> {
        assumingMemoryBound(to: vpkc_update_info_c.self)
    }
}

typealias vpkc_new_update_manager_fn = @convention(c) (
    UnsafePointer<CChar>?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?,
    UnsafeMutablePointer<vpkc_manager?>?) -> Bool
typealias vpkc_check_for_updates_fn = @convention(c) (
    vpkc_manager?, UnsafeMutablePointer<UnsafeMutableRawPointer?>?) -> Int8
typealias vpkc_download_updates_fn = @convention(c) (
    vpkc_manager?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?, UnsafeMutableRawPointer?) -> Bool
typealias vpkc_wait_exit_then_apply_updates_fn = @convention(c) (
    vpkc_manager?, UnsafeMutableRawPointer?, Bool, Bool,
    UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, Int) -> Bool
typealias vpkc_free_update_manager_fn = @convention(c) (vpkc_manager?) -> Void
typealias vpkc_free_update_info_fn = @convention(c) (UnsafeMutableRawPointer?) -> Void
typealias vpkc_get_last_error_fn = @convention(c) (UnsafeMutablePointer<CChar>?, Int) -> Int
typealias vpkc_app_run_fn = @convention(c) (UnsafeMutableRawPointer?) -> Void
typealias vpkc_log_callback_t = @convention(c) (
    UnsafeMutableRawPointer?, UnsafePointer<CChar>?, UnsafePointer<CChar>?) -> Void
typealias vpkc_set_logger_fn = @convention(c) (vpkc_log_callback_t?, UnsafeMutableRawPointer?) -> Void

func velopackLogCallback(_ userData: UnsafeMutableRawPointer?,
                         _ level: UnsafePointer<CChar>?,
                         _ message: UnsafePointer<CChar>?) {
    let lvl = level.map { String(cString: $0) } ?? "info"
    let msg = message.map { String(cString: $0) } ?? ""
    NSLog("[Velopack:%@] %@", lvl, msg)
}

/// dlopen'd handle to velopack_libc_osx.dylib with the handful of C API
/// entry points this app uses, resolved once. A shared singleton loads from
/// the app bundle; the update harness injects an explicit dylib path instead.
final class VelopackLibC {
    static let shared: VelopackLibC? = VelopackLibC(dylibPath: nil)

    let newUpdateManager: vpkc_new_update_manager_fn
    let checkForUpdates: vpkc_check_for_updates_fn
    let downloadUpdates: vpkc_download_updates_fn
    let waitExitThenApplyUpdates: vpkc_wait_exit_then_apply_updates_fn
    let freeUpdateManager: vpkc_free_update_manager_fn
    let freeUpdateInfo: vpkc_free_update_info_fn
    let getLastError: vpkc_get_last_error_fn
    let appRun: vpkc_app_run_fn
    private let handle: UnsafeMutableRawPointer

    /// nil when the dylib is missing or an expected entry point failed to
    /// resolve (the caller falls back to the feed-check stub).
    init?(dylibPath: URL?) {
        let url = dylibPath
            ?? Bundle.main.privateFrameworksURL?.appendingPathComponent("velopack_libc_osx.dylib")
            ?? Bundle.main.bundleURL.appendingPathComponent("Contents/Frameworks/velopack_libc_osx.dylib")
        guard let handle = dlopen(url.path, RTLD_LAZY) else {
            NSLog("[VelopackLibC] dlopen failed: %@", url.path)
            return nil
        }
        guard
            let newUpdateManager = Self.symbol(handle, "vpkc_new_update_manager", as: vpkc_new_update_manager_fn.self),
            let checkForUpdates = Self.symbol(handle, "vpkc_check_for_updates", as: vpkc_check_for_updates_fn.self),
            let downloadUpdates = Self.symbol(handle, "vpkc_download_updates", as: vpkc_download_updates_fn.self),
            let waitExitThenApplyUpdates = Self.symbol(handle, "vpkc_wait_exit_then_apply_updates", as: vpkc_wait_exit_then_apply_updates_fn.self),
            let freeUpdateManager = Self.symbol(handle, "vpkc_free_update_manager", as: vpkc_free_update_manager_fn.self),
            let freeUpdateInfo = Self.symbol(handle, "vpkc_free_update_info", as: vpkc_free_update_info_fn.self),
            let getLastError = Self.symbol(handle, "vpkc_get_last_error", as: vpkc_get_last_error_fn.self),
            let appRun = Self.symbol(handle, "vpkc_app_run", as: vpkc_app_run_fn.self)
        else {
            return nil
        }
        self.handle = handle
        self.newUpdateManager = newUpdateManager
        self.checkForUpdates = checkForUpdates
        self.downloadUpdates = downloadUpdates
        self.waitExitThenApplyUpdates = waitExitThenApplyUpdates
        self.freeUpdateManager = freeUpdateManager
        self.freeUpdateInfo = freeUpdateInfo
        self.getLastError = getLastError
        self.appRun = appRun
        if let setLogger = Self.symbol(handle, "vpkc_set_logger", as: vpkc_set_logger_fn.self) {
            setLogger(velopackLogCallback, nil)
        }
    }

    private static func symbol<T>(_ handle: UnsafeMutableRawPointer, _ name: String, as type: T.Type) -> T? {
        guard let pointer = dlsym(handle, name) else {
            NSLog("[VelopackLibC] missing symbol: %@", name)
            return nil
        }
        return unsafeBitCast(pointer, to: T.self)
    }

    func lastErrorMessage() -> String {
        var buffer = [CChar](repeating: 0, count: 2048)
        let written = getLastError(&buffer, buffer.count)
        let message = String(cString: buffer)
        return written > 0 && !message.isEmpty ? message : "Unknown Velopack error"
    }
}

/// Real self-update provider backed by the Velopack C API — the macOS port
/// of linux/gui/update_manager.cpp: check the feed, download the full/delta
/// nupkg, then apply on exit. Applying spawns Velopack's macOS updater
/// (UpdateMac, which vpk places in Contents/MacOS at pack time) with
/// waitPid semantics; it swaps the app bundle after this process exits and
/// relaunches — no hand-rolled file swapping here.
final class VelopackUpdateProvider: UpdateProvider {
    /// vpkc_update_check_t values (Velopack.h).
    private enum CheckResult: Int8 {
        case updateError = -1
        case updateAvailable = 0
        case noUpdateAvailable = 1
        case remoteIsEmpty = 2
    }

    private let lib: VelopackLibC
    private let feedBaseURL: String

    /// Downloaded update info awaiting the user's restart confirmation;
    /// owned until applied or freed (AppUpdateManager::Impl::pending parity).
    private var staged: UnsafeMutableRawPointer?

    /// Loads the bundled dylib; nil when it is missing or unusable so the
    /// caller can fall back to VelopackFeedUpdateProvider.
    static func loadIfAvailable() -> VelopackUpdateProvider? {
        guard let lib = VelopackLibC.shared else { return nil }
        return VelopackUpdateProvider(lib: lib)
    }

    init(lib: VelopackLibC, feedBaseURL: String = UpdateFeed.resolvedBaseURL) {
        self.lib = lib
        self.feedBaseURL = feedBaseURL
    }

    deinit {
        freeStaged()
    }

    /// Velopack startup hooks (Linux main.cpp VelopackApp::Build().Run()
    /// parity): consumes post-install/update CLI args, and applies a staged
    /// package on startup if a previous run downloaded one but exited before
    /// applying (the library re-execs the updater and exits this process).
    /// Safe no-op when the app is not a Velopack install: the library logs
    /// the locator error and returns. Call before anything else at launch.
    static func runStartupHooks() {
        guard let lib = VelopackLibC.shared else { return }
        lib.appRun(nil)
    }

    // MARK: - UpdateProvider

    func checkForUpdates() async -> Result<UpdateCheckResult, Error> {
        // The libc calls block (network + disk); keep them off the main
        // thread so the UI stays responsive during a download.
        await Task.detached(priority: .userInitiated) { self.checkAndStage() }.value
    }

    func applyStagedUpdate() async -> Result<Void, Error> {
        await Task.detached(priority: .userInitiated) { self.applyStaged() }.value
    }

    // MARK: - Blocking libc flow (background queue)

    private func makeManager() -> vpkc_manager? {
        var manager: vpkc_manager?
        let ok = feedBaseURL.withCString { cFeed in
            lib.newUpdateManager(cFeed, nil, nil, &manager)
        }
        if !ok {
            NSLog("[UpdateManager] vpkc_new_update_manager failed: %@", lib.lastErrorMessage())
        }
        return manager
    }

    /// Feed check + download, exactly like AppUpdateManager::CheckForUpdates:
    /// the update is fully staged before the UI prompts, so confirming the
    /// restart applies immediately.
    private func checkAndStage() -> Result<UpdateCheckResult, Error> {
        freeStaged()
        guard let manager = makeManager() else {
            return .failure(Self.velopackError("Couldn't initialize the updater: \(lib.lastErrorMessage())"))
        }
        defer { lib.freeUpdateManager(manager) }

        var info: UnsafeMutableRawPointer?
        let check = CheckResult(rawValue: lib.checkForUpdates(manager, &info))
            ?? .updateError
        switch check {
        case .updateAvailable:
            guard let info else {
                return .failure(Self.velopackError("The update feed returned an incomplete release."))
            }
            guard let target = info.updateInfo.pointee.TargetFullRelease,
                  let versionPointer = target.pointee.Version else {
                lib.freeUpdateInfo(info)
                return .failure(Self.velopackError("The update feed returned an incomplete release."))
            }
            let version = String(cString: versionPointer)
            let notes = target.pointee.NotesMarkdown.map { String(cString: $0) } ?? ""
            // The library picks delta packages when available and falls
            // back to the full nupkg on its own.
            guard lib.downloadUpdates(manager, info, nil, nil) else {
                let message = lib.lastErrorMessage()
                lib.freeUpdateInfo(info)
                return .failure(Self.velopackError("Couldn't download the update: \(message)"))
            }
            staged = info
            NSLog("[UpdateManager] staged update to %@", version)
            return .success(.updateReady(UpdateReleaseInfo(
                version: version,
                notesMarkdown: notes.isEmpty ? nil : notes)))
        case .noUpdateAvailable, .remoteIsEmpty:
            return .success(.upToDate)
        case .updateError:
            return .failure(Self.velopackError(lib.lastErrorMessage()))
        }
    }

    /// Apply on exit like AppUpdateManager::ApplyAndRestart: UpdateMac is
    /// told to wait (60 s) for this process to exit, then swaps the app
    /// bundle from the staged nupkg and relaunches the app. On success the
    /// caller terminates the process promptly.
    private func applyStaged() -> Result<Void, Error> {
        guard let staged, let asset = staged.updateInfo.pointee.TargetFullRelease else {
            return .failure(UpdateProviderError.applyUnavailable)
        }
        guard let manager = makeManager() else {
            return .failure(Self.velopackError("Couldn't initialize the updater: \(lib.lastErrorMessage())"))
        }
        defer { lib.freeUpdateManager(manager) }
        guard lib.waitExitThenApplyUpdates(manager, UnsafeMutableRawPointer(asset), false, true, nil, 0) else {
            return .failure(Self.velopackError(lib.lastErrorMessage()))
        }
        return .success(())
    }

    private func freeStaged() {
        if let staged {
            lib.freeUpdateInfo(staged)
            self.staged = nil
        }
    }

    private static func velopackError(_ message: String) -> NSError {
        NSError(domain: "AgentRedactor.VelopackUpdate", code: 1,
                userInfo: [NSLocalizedDescriptionKey: message])
    }
}

#endif // SELFRELEASE
