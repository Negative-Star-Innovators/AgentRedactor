import Foundation
import AppKit
import ServiceManagement

/// GUI-side application state: the macOS port of linux/gui/app_state.cpp +
/// the Linux MainWindow poll/lock wiring. Owns the EngineClient + facades,
/// spawns/respawns the engine, polls /status + /settings every second, and
/// publishes snapshots for the SwiftUI views. All engine I/O runs on a
/// dedicated serial queue; published state is only mutated on the main queue.
/// Cross-queue safety: engine objects are touched only on `engineQueue`;
/// snapshots cross to the main queue via DispatchQueue.main.async.
final class AppState: ObservableObject, @unchecked Sendable {
    // MARK: Published state

    @Published private(set) var status = EngineStatus()
    @Published private(set) var settings = EngineSettings()

    // MARK: - Authoritative start-on-boot state (optimistic, write-serialized)

    /// GUI-side source of truth for start-on-boot while a write is in flight
    /// (or ahead of the engine's poll). The checkbox binds
    /// `startOnBootEffective` and responds instantly; the override is
    /// surrendered only when the engine's poll CONFIRMS the value, so the
    /// control never bounces.
    @Published private(set) var startOnBootOverride: Bool?
    /// Serializes start-on-boot writes: each click chains behind the previous
    /// write (latest value wins, order preserved) — the mutex the GUI needs
    /// so rapid clicks apply exactly once each, in order.
    private var startOnBootWriteChain: Task<Result<Void, Error>, Never>?

    var startOnBootEffective: Bool {
        startOnBootOverride ?? settings.startOnBoot
    }

    /// Instant UI update + ordered background write. Returns the write
    /// result (failure also reverts the override so the poll's engine truth
    /// shows again).
    @MainActor
    @discardableResult
    func setStartOnBoot(_ value: Bool) async -> Result<Void, Error> {
        startOnBootOverride = value
        let engine = self.client
        let previous = startOnBootWriteChain
        let task = Task { [weak self] () -> Result<Void, Error> in
            _ = await previous?.value
            let result = await Task { () -> Result<Void, Error> in
                do {
                    _ = try engine.put("/settings/startOnBoot", body: ["value": value])
                    return .success(())
                } catch {
                    return .failure(error)
                }
            }.value
            guard let self else { return result }
            if case .failure = result {
                await MainActor.run { self.startOnBootOverride = nil }
            }
            // On success the override STAYS until the poll confirms the
            // value (see pollTick) — that is what prevents the checkbox flash.
            return result
        }
        startOnBootWriteChain = task
        return await task.value
    }

    // MARK: - Optimistic logging toggles (same principle as start-on-boot)

    @Published private(set) var loggingEnabledOverride: Bool?
    @Published private(set) var showSensitiveOverride: Bool?
    private var loggingEnabledWriteChain: Task<Result<Void, Error>, Never>?
    private var showSensitiveWriteChain: Task<Result<Void, Error>, Never>?

    var loggingEnabledEffective: Bool { loggingEnabledOverride ?? settings.loggingEnabled }
    var showSensitiveEffective: Bool { showSensitiveOverride ?? settings.showSensitive }

    /// Instant checkbox flip + ordered background write; the override holds
    /// until the poll confirms the engine value (no round-trip lag, no bounce).
    @MainActor
    @discardableResult
    func setLoggingEnabledOptimistic(_ value: Bool) async -> Result<Void, Error> {
        await writeOptimisticSetting(value, endpoint: "/settings/loggingEnabled",
                                     override: \.loggingEnabledOverride,
                                     chain: &loggingEnabledWriteChain)
    }

    /// Arming while logging is off still fails with the engine's 400
    /// "enable logging first" — the override is dropped and the caller
    /// surfaces the hint, exactly like the pre-optimistic behavior.
    @MainActor
    @discardableResult
    func setShowSensitiveOptimistic(_ value: Bool) async -> Result<Void, Error> {
        await writeOptimisticSetting(value, endpoint: "/settings/showSensitive",
                                     override: \.showSensitiveOverride,
                                     chain: &showSensitiveWriteChain)
    }

    /// Shared engine of the optimistic pattern (see setStartOnBoot): publish
    /// the override now, serialize the write behind any in-flight one, drop
    /// the override on failure so the poll's engine truth shows again, and on
    /// success let the poll surrender the override on confirmation.
    @MainActor
    private func writeOptimisticSetting(
        _ value: Bool, endpoint: String,
        override: ReferenceWritableKeyPath<AppState, Bool?>,
        chain: inout Task<Result<Void, Error>, Never>?
    ) async -> Result<Void, Error> {
        self[keyPath: override] = value
        let engine = self.client
        let previous = chain
        let task = Task { [weak self] () -> Result<Void, Error> in
            _ = await previous?.value
            let result: Result<Void, Error>
            do {
                _ = try engine.put(endpoint, body: ["value": value])
                result = .success(())
            } catch {
                result = .failure(error)
            }
            guard let self else { return result }
            if case .failure = result {
                await MainActor.run { self[keyPath: override] = nil }
            }
            return result
        }
        chain = task
        return await task.value
    }
    @Published private(set) var profiles: [ApiKeyProfileModel] = []
    @Published private(set) var matches: [SessionMatchModel] = []
    @Published private(set) var selectedProfileID: String?
    @Published private(set) var connectionLost = false
    @Published private(set) var engineSpawned = false
    @Published private(set) var engineReady = false
    /// True while protection is on and the engine session is locked.
    @Published private(set) var isLocked = false

    let client = EngineClient()
    private(set) lazy var settingsFacade = SettingsFacade(client: client)
    private(set) lazy var proxyFacade = ProxyFacade(client: client)

    // MARK: Internals

    private let engineQueue = DispatchQueue(label: "com.negativestarinnovators.agentredactor.engine")
    private var pollSource: DispatchSourceTimer?
    private var stopPolling = false
    private var consecutivePollFailures = 0
    private var profilesRevision = -1
    private var profileSnapshotWasLocked = false
    private var lastAppliedLanguage: String?
    private var shutdownDone = false
    private var inactivityReset: DispatchWorkItem?
    private var localAuthInFlight = false

    /// App version stamped into Info.plist (version.txt parity on other
    /// platforms comes at release-packaging time).
    private var appVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"
    }

    var selectedProfile: ApiKeyProfileModel? {
        profiles.first { $0.id == selectedProfileID }
    }

    var protectionEnabled: Bool { settings.masterPasswordEnabled }

    /// LA availability gate + Keychain-backed password (see LocalAuthGate.swift).
    let authGate = LocalAuthGate()

    // MARK: - Bootstrap

    /// Called once from the app delegate after the single-instance guard.
    func bootstrap() async -> Bool {
        AppPaths.ensureConfigDir()
        let ok = await withCheckedContinuation { cont in
            engineQueue.async {
                let success = self.ensureEngineRunningBlocking()
                cont.resume(returning: success)
            }
        }
        if !ok { return false }
        await refreshProfilesFull()
        startPolling()
        return true
    }

    /// Mirrors AppState::EnsureEngineRunning: connect + ping; restart the
    /// engine when its version doesn't match the GUI's; spawn the bundled
    /// binary when nothing answers; wait up to 30 s for control.json.
    /// All @Published writes MUST happen on the main queue: Combine's
    /// Published/ObservableObjectPublisher locks are not safe when two
    /// threads mutate the same ObservableObject concurrently — a real
    /// profile-add freeze traced to engineQueue writing `engineReady` here
    /// while the main queue published the reloaded profile list (main ended
    /// blocked in `Published` setters per the macOS hang report).
    private func publishOnMain(_ mutate: @escaping () -> Void) {
        DispatchQueue.main.async(execute: mutate)
    }

    private func ensureEngineRunningBlocking() -> Bool {
        stopPolling = false
        if connectAndPing() {
            if let version = try? statusVersion(), version != "dev", version != appVersion {
                NSLog("[AppState] engine version %@ does not match GUI %@; restarting", version, appVersion)
                _ = try? client.post("/engine/stop")
                waitForEngineExit(maxSeconds: 5)
            } else {
                publishOnMain { self.engineReady = true }
                return true
            }
        }
        guard spawnEngine() else { return false }
        return waitForEngineReady(maxSeconds: 30)
    }

    private func statusVersion() throws -> String? {
        try client.get("/status").body.flatMap { ($0 as? [String: Any])?["engineVersion"] as? String }
    }

    private func spawnEngine() -> Bool {
        guard let engineURL = AppPaths.bundledEngine,
              FileManager.default.isExecutableFile(atPath: engineURL.path) else {
            NSLog("[AppState] bundled engine binary missing at %@", AppPaths.bundledEngine?.path ?? "?")
            return false
        }
        let process = Process()
        process.executableURL = engineURL
        // Inherit the environment so AGENTREDACTOR_CONFIG_DIR test overrides
        // propagate to the engine exactly like the other platforms.
        do {
            try process.run()
        } catch {
            NSLog("[AppState] failed to spawn engine: %@", error.localizedDescription)
            return false
        }
        publishOnMain { self.engineSpawned = true }
        return true
    }

    private func waitForEngineReady(maxSeconds: Int) -> Bool {
        for _ in 0..<(maxSeconds * 10) {
            if stopPolling { return false }
            Thread.sleep(forTimeInterval: 0.1)
            if connectAndPing() {
                publishOnMain { self.engineReady = true }
                return true
            }
        }
        return false
    }

    private func waitForEngineExit(maxSeconds: Int) {
        for _ in 0..<(maxSeconds * 10) {
            Thread.sleep(forTimeInterval: 0.1)
            client.connect(configDir: AppPaths.configDir)
            if !client.ping() { return }
        }
    }

    /// Connect + ping with one transparent control.json re-read (the engine
    /// rotates the token/port on every restart).
    private func connectAndPing() -> Bool {
        if client.connect(configDir: AppPaths.configDir), client.ping() { return true }
        return client.connect(configDir: AppPaths.configDir) && client.ping()
    }

    // MARK: - Polling (linux/gui/app_state.cpp PollThreadMain parity)

    private func startPolling() {
        let timer = DispatchSource.makeTimerSource(queue: engineQueue)
        timer.schedule(deadline: .now(), repeating: 1.0)
        timer.setEventHandler { [weak self] in self?.pollTick() }
        pollSource = timer
        timer.resume()
    }

    private func pollTick() {
        guard !stopPolling else { return }
        do {
            let statusResponse = try client.get("/status")
            consecutivePollFailures = 0
            let newStatus = EngineStatus.fromJSON(statusResponse.body)
            let settingsResponse = try client.get("/settings")
            let newSettings = EngineSettings.fromJSON(settingsResponse.body)

            DispatchQueue.main.async {
                let wasLocked = self.isLocked
                self.status = newStatus
                self.settings = newSettings
                self.isLocked = newSettings.masterPasswordEnabled && !newStatus.unlocked
                if (wasLocked || self.profileSnapshotWasLocked) && !self.isLocked {
                    self.profileSnapshotWasLocked = false
                    Task { await self.refreshProfilesFull() }
                }
                self.connectionLost = false
                self.applyLanguageIfNeeded(newSettings.appLanguage)
                // Surrender the optimistic overrides only on engine
                // confirmation — no visible bounce while a write is pending.
                if let override = self.startOnBootOverride,
                   override == newSettings.startOnBoot {
                    self.startOnBootOverride = nil
                }
                if let override = self.loggingEnabledOverride,
                   override == newSettings.loggingEnabled {
                    self.loggingEnabledOverride = nil
                }
                if let override = self.showSensitiveOverride,
                   override == newSettings.showSensitive {
                    self.showSensitiveOverride = nil
                }
                self.syncLoginItem(with: newSettings)
            }

            // Profile mutations bump profilesRevision; cheap full-reload trigger.
            if newSettings.profilesRevision != profilesRevision {
                profilesRevision = newSettings.profilesRevision
                reloadProfilesBlocking()
            }
            refreshLiveStatsBlocking()
            refreshMatchesBlocking()
        } catch {
            DispatchQueue.main.async { self.connectionLost = true }
            consecutivePollFailures += 1
            // Engine died mid-session: respawn after ~5 s of failures, then
            // roughly every 30 s until it is back (AppState::RespawnEngine).
            if consecutivePollFailures >= 5 && (consecutivePollFailures - 5) % 30 == 0 {
                respawnEngineBlocking()
            }
        }
    }

    private func respawnEngineBlocking() {
        guard !stopPolling else { return }
        if connectAndPing() { return }
        NSLog("[AppState] engine unreachable; respawning it")
        _ = spawnEngine() || engineSpawned
        _ = waitForEngineReady(maxSeconds: 30)
    }

    // MARK: - Profiles

    private func reloadProfilesBlocking() {
        guard let list = try? settingsFacade.getProfiles() else { return }
        let status = try? EngineStatus.fromJSON(self.client.get("/status").body)
        if list.isEmpty {
            // Seed a default profile on first run (Windows/Linux parity) so the
            // user never lands on an empty form. POST restarts listeners
            // engine-side; the next revision bump triggers a follow-up reload.
            let seeded = ApiKeyProfileModel(
                id: "",
                alias: LocString("Profile_DefaultName"),
                upstreamUrl: "",
                apiKeyMasked: "",
                port: max(PortCheck.findAvailablePort(startPort: 8080, excluding: []), 8080),
                useOpenAIModel: true,
                enabledPIITypes: DEFAULT_PII_TYPES,
                piiConfidenceThreshold: 0.9,
                regexPatterns: [],
                keywords: [],
                stats: .empty,
                enabled: true)
            _ = try? settingsFacade.addProfile(seeded)
            return
        }
        DispatchQueue.main.async {
            if let status {
                self.profileSnapshotWasLocked = status.masterPasswordEnabled && !status.unlocked
            }
            var selected = self.selectedProfileID
            if selected == nil || !list.contains(where: { $0.id == selected }) {
                selected = list.first?.id
            }
            self.publishProfiles(list, selectedID: selected)
        }
    }

    /// Full reload exposed to views (after add/delete and on demand).
    func refreshProfilesFull() async {
        await runOnEngineQueue {
            if let snapshot = try? self.settingsFacade.getSettings() {
                self.profilesRevision = snapshot.profilesRevision
            }
            self.reloadProfilesBlocking()
        }
    }

    @MainActor
    func selectProfile(id: String) {
        guard id != selectedProfileID else { return }
        selectedProfileID = id
        Task { await refreshMatches() }
    }

    /// Merges /profiles into the published list, keeping per-port proxyRunning
    /// from /status (the two endpoints serve different slices).
    private func publishProfiles(_ list: [ApiKeyProfileModel], selectedID: String?) {
        var merged = list
        for i in merged.indices {
            if let sp = status.profiles.first(where: { $0.id == merged[i].id }) {
                merged[i].proxyRunning = sp.proxyRunning
            }
        }
        if merged != profiles { profiles = merged }
        selectedProfileID = selectedID
    }

    /// Stats change on every proxied request without bumping profilesRevision,
    /// so refresh just the stats from the engine each tick (Linux parity).
    private func refreshLiveStatsBlocking() {
        guard let live = try? settingsFacade.getProfiles() else { return }
        DispatchQueue.main.async {
            var updated = false
            var newList = self.profiles
            for i in newList.indices {
                guard let lp = live.first(where: { $0.id == newList[i].id }) else { continue }
                if lp.stats != newList[i].stats {
                    newList[i].stats = lp.stats
                    updated = true
                }
                if let sp = self.status.profiles.first(where: { $0.id == newList[i].id }),
                   sp.proxyRunning != newList[i].proxyRunning {
                    newList[i].proxyRunning = sp.proxyRunning
                    updated = true
                }
            }
            if updated { self.profiles = newList }
        }
    }

    // MARK: - Session matches

    private func refreshMatchesBlocking() {
        guard let id = selectedProfileID else { return }
        guard let list = try? proxyFacade.getSessionMatches(profileId: id) else { return }
        DispatchQueue.main.async {
            if list != self.matches { self.matches = list }
        }
    }

    func refreshMatches() async {
        await runOnEngineQueue { self.refreshMatchesBlocking() }
    }

    @MainActor
    func clearMatches() async {
        guard let id = selectedProfileID else { return }
        let result = await runOnEngineQueueResult { try self.proxyFacade.clearSessionMatches(profileId: id) }
        if case .success = result {
            matches = []
        }
    }

    // MARK: - Profile mutations

    @discardableResult
    func saveProfile(_ profile: ApiKeyProfileModel) async -> Result<Void, Error> {
        let result = await runOnEngineQueueResult { try self.settingsFacade.updateProfile(profile) }
        if case .success = result { await refreshProfilesFull() }
        return result
    }

    /// Optimistic profile mutation for instant UI feedback (detection toggles,
    /// sliders, checkboxes): publishes the change locally FIRST so the control
    /// responds immediately, saves in the background, and reverts + reports on
    /// failure. The settings poll reconciles with server truth once the save
    /// lands (the engine bumps profilesRevision, triggering a full reload).
    @MainActor
    @discardableResult
    func applyProfileChange(_ updated: ApiKeyProfileModel) async -> Result<Void, Error> {
        guard let idx = profiles.firstIndex(where: { $0.id == updated.id }) else {
            return await saveProfile(updated)
        }
        let previous = profiles[idx]
        profiles[idx] = updated
        let result = await runOnEngineQueueResult { try self.settingsFacade.updateProfile(updated) }
        if case .failure = result {
            profiles[idx] = previous // revert the optimistic change
        }
        return result.map { _ in () }
    }

    @MainActor
    @discardableResult
    func addProfile() async -> Result<Void, Error> {
        let ports = profiles.map { $0.port }
        let newPort = max(PortCheck.findAvailablePort(startPort: 8080, excluding: ports), 8080)
        let result = await runOnEngineQueueResult { () -> String in
            var body = ApiKeyProfileModel(
                id: "",
                alias: LocString("Profile_NewName"),
                upstreamUrl: "",
                apiKeyMasked: "",
                port: newPort,
                useOpenAIModel: true,
                enabledPIITypes: DEFAULT_PII_TYPES,
                piiConfidenceThreshold: 0.9,
                regexPatterns: [],
                keywords: [],
                stats: .empty,
                enabled: true)
            body.id = ""
            return try self.settingsFacade.addProfile(body)
        }
        switch result {
        case .success(let id):
            await refreshProfilesFull()
            selectedProfileID = id
            return .success(())
        case .failure(let error):
            return .failure(error)
        }
    }

    @discardableResult
    func removeProfile(id: String) async -> Result<Void, Error> {
        let result = await runOnEngineQueueResult { try self.settingsFacade.removeProfile(id: id) }
        if case .success = result {
            if selectedProfileID == id { selectedProfileID = nil }
            await refreshProfilesFull()
        }
        return result
    }

    /// The one secret-serving endpoint; 403 while locked (caller keeps masked).
    func fetchProfileApiKey(id: String) async -> Result<String, Error> {
        await runOnEngineQueueResult { try self.settingsFacade.getProfileApiKey(id: id) }
    }

    // MARK: - Settings

    @discardableResult
    func putSetting(key: String, value: Any) async -> Result<Void, Error> {
        await runOnEngineQueueResult { try self.settingsFacade.putSetting(key: key, value: value) }
    }

    // MARK: - Master password + lock (Hello-style: no custom password)

    /// Refresh before a return-to-app prompt so startup and focus changes do
    /// not depend on the one-second poll having published the latest state.
    @MainActor
    func refreshLockState() async {
        let result = await runOnEngineQueueResult {
            try EngineStatus.fromJSON(self.client.get("/status").body)
        }
        if case .success(let newStatus) = result {
            status = newStatus
            isLocked = newStatus.masterPasswordEnabled && !newStatus.unlocked
        }
    }

    /// LocalAuthentication (Touch ID / Mac password) -> Keychain -> POST
    /// /unlock. The engine uses the same typed-secret path as Linux; the random
    /// secret is only revealed after successful OS authentication. A concurrent
    /// caller is rejected rather than opening a second prompt.
    @MainActor
    func unlockWithLocalAuth() async -> Result<UnlockResult, Error> {
        guard !localAuthInFlight else { return .failure(AuthGateError.canceled) }
        localAuthInFlight = true
        defer { localAuthInFlight = false }
        switch await authGate.retrieveSecret() {
        case .failure(let error):
            return .failure(error)
        case .success(let password):
            let result = await runOnEngineQueueResult { try self.settingsFacade.unlock(password: password) }
            if case .success(let unlock) = result, unlock.ok {
                await refreshProfilesFull()
            }
            return result
        }
    }

    func lockSession() async {
        _ = await runOnEngineQueueResult { try self.settingsFacade.lockSession() }
    }

    /// Generates a fresh random protection secret (256 bits). Enabling stores
    /// it in the Keychain FIRST, then registers it with the engine — so a
    /// Keychain failure never leaves "engine enabled, key lost".
    func generateProtectionSecret() -> String {
        var bytes = [UInt8](repeating: 0, count: 32)
        _ = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
        return Data(bytes).base64EncodedString()
    }

    /// Registers a previously stored secret with the engine. A 409 "already
    /// enabled" is mapped to a dedicated error so the UI can explain instead
    /// of showing a generic failure.
    func enableMasterPassword(secret: String) async -> Result<Void, Error> {
        let result = await runOnEngineQueueResult { try self.settingsFacade.enableMasterPassword(password: secret) }
        if case .failure(let error as EngineClientError) = result,
           case .serverError(let code, _) = error, code == 409 {
            return .failure(NSError(domain: "AgentRedactor", code: 409,
                                    userInfo: [NSLocalizedDescriptionKey: LocString("MacPassword_AlreadyEnabled")]))
        }
        return result.map { _ in () }
    }

    func disableMasterPassword() async -> Result<Void, Error> {
        await runOnEngineQueueResult { try self.settingsFacade.disableMasterPassword() }
    }

    /// Gate -> Keychain -> POST /unlock, for flows needing an unlocked engine
    /// (disable) without any typed password.
    func unlockWithStoredPassword() async -> Result<Void, Error> {
        switch await unlockWithLocalAuth() {
        case .success(let result):
            return result.ok ? .success(()) : .failure(NSError(domain: "AgentRedactor", code: 1, userInfo: [NSLocalizedDescriptionKey: result.error ?? "unlock failed"]))
        case .failure(let error):
            return .failure(error)
        }
    }

    // MARK: - Model download

    func startModelDownloadIfNeeded() async {
        let needsKick = status.modelDownloadRequired
            && !status.modelDownloadInProgress
            && !status.modelDownloadFailed
        if needsKick {
            _ = await runOnEngineQueueResult { try self.client.post("/engine/download-model") }
        }
    }

    func retryModelDownload() async {
        _ = await runOnEngineQueueResult { try self.client.post("/engine/download-model") }
    }

    // MARK: - Updates (linux/gui AppUpdateManager + MainWindow parity)

    /// The real Velopack updater in self-release builds (when the bundled
    /// dylib loaded), the feed-check-only stub otherwise. MSIX parity: a
    /// future store/sandbox build compiles the updater out entirely via the
    /// SELFRELEASE flag, leaving the stub.
    #if SELFRELEASE
    let updateProvider: UpdateProvider = VelopackUpdateProvider.loadIfAvailable()
        ?? VelopackFeedUpdateProvider()
    #else
    let updateProvider: UpdateProvider = VelopackFeedUpdateProvider()
    #endif

    /// Restart-to-update prompt content; RootView renders it as an alert
    /// (the Linux GUI's updateDownloaded dialog).
    @Published private(set) var updatePrompt: UpdateReleaseInfo?

    private var updateCheckInFlight = false

    /// Test hook (self-release builds only): AGENTREDACTOR_UPDATE_AUTOAPPLY=1
    /// skips the restart prompt and applies immediately. Unset in normal use.
    private static var autoApplyUpdates: Bool {
        ProcessInfo.processInfo.environment["AGENTREDACTOR_UPDATE_AUTOAPPLY"] == "1"
    }

    /// Background launch-time check (MainWindow CheckForUpdates(false)
    /// parity): silent on failure/up-to-date, prompts when an update is
    /// ready (or applies it directly under AGENTREDACTOR_UPDATE_AUTOAPPLY=1).
    func checkForUpdatesOnLaunch() {
        Task { await runUpdateCheck(userInitiated: false) }
    }

    /// Settings "Check for updates" button (CheckForUpdates(true) parity);
    /// the caller surfaces up-to-date/errors, the root view prompts.
    @discardableResult
    func checkForUpdatesFromSettings() async -> Result<UpdateCheckResult, Error> {
        await runUpdateCheck(userInitiated: true)
    }

    private func runUpdateCheck(userInitiated: Bool) async -> Result<UpdateCheckResult, Error> {
        guard !updateCheckInFlight else { return .failure(UpdateProviderError.updateCheckBusy) }
        updateCheckInFlight = true
        defer { updateCheckInFlight = false }
        let result = await updateProvider.checkForUpdates()
        switch result {
        case .success(.updateReady(let info)):
            if Self.autoApplyUpdates {
                NSLog("[UpdateManager] AGENTREDACTOR_UPDATE_AUTOAPPLY=1; applying without prompting")
                if let applyError = await applyStagedUpdateAndRestart() {
                    // Silent apply failed: fall back to prompting so the
                    // user can retry from the dialog.
                    NSLog("[UpdateManager] auto-apply failed: %@", applyError.localizedDescription)
                } else {
                    return result // unreachable in practice: apply terminates the app
                }
            }
            DispatchQueue.main.async { self.updatePrompt = info }
        case .failure(let error):
            if !userInitiated {
                NSLog("[UpdateManager] update check failed: %@", error.localizedDescription)
            }
        default:
            break
        }
        return result
    }

    /// Restart-now action behind the prompt (ApplyAndRestart parity). On
    /// success the updater is waiting for this process to exit, so the app
    /// terminates immediately (no quit confirmation). Returns the error to
    /// surface when applying fails.
    func applyStagedUpdateAndRestart() async -> Error? {
        if case .failure(let error) = await updateProvider.applyStagedUpdate() {
            return error
        }
        await MainActor.run {
            AppDelegate.current?.terminateForUpdateRestart()
        }
        return nil
    }

    func dismissUpdatePrompt() {
        updatePrompt = nil
    }

    // MARK: - Inactivity re-lock (10 min, reset by app input — Linux parity)

    func noteUserActivity() {
        inactivityReset?.cancel()
        let item = DispatchWorkItem { [weak self] in
            guard let self, self.protectionEnabled, self.status.unlocked else { return }
            Task { await self.lockSession() }
        }
        inactivityReset = item
        DispatchQueue.main.asyncAfter(deadline: .now() + 600, execute: item)
    }

    // MARK: - Language

    private func applyLanguageIfNeeded(_ tag: String) {
        let resolved = LocalizationManager.resolve(tag)
        if resolved != lastAppliedLanguage {
            lastAppliedLanguage = resolved
            LocalizationManager.shared.apply(languageTag: resolved)
        }
    }

    // MARK: - Start on login (SMAppService ↔ persisted setting agreement)

    /// Keeps the login item in agreement with the persisted setting (e.g.
    /// changed via the CLI while the GUI was closed) — the Linux Autostart
    /// reconciliation in app_state.cpp, for SMAppService.
    private func syncLoginItem(with settings: EngineSettings) {
        let service = SMAppService.mainApp
        let registered = service.status == .enabled
        if settings.startOnBoot != registered {
            if settings.startOnBoot {
                try? service.register()
            } else {
                try? service.unregister()
            }
        }
    }

    // MARK: - Shutdown

    /// Called on every quit path (tray Quit, Cmd+Q, terminate). Mirrors
    /// AppState::Shutdown: this GUI owns the engine only when it spawned it.
    func shutdown() {
        guard !shutdownDone else { return }
        shutdownDone = true
        stopPolling = true
        pollSource?.cancel()
        let spawned = engineSpawned
        let protect = protectionEnabled
        engineQueue.async {
            if spawned {
                _ = try? self.client.post("/engine/stop")
            } else if protect {
                _ = try? self.settingsFacade.lockSession()
            }
        }
    }

    // MARK: - Queue helpers

    private func runOnEngineQueue(_ operation: @escaping () -> Void) async {
        await withCheckedContinuation { cont in
            engineQueue.async {
                operation()
                cont.resume()
            }
        }
    }

    private func runOnEngineQueueResult<T>(_ operation: @escaping () throws -> T) async -> Result<T, Error> {
        await withCheckedContinuation { cont in
            engineQueue.async {
                do {
                    cont.resume(returning: .success(try operation()))
                } catch {
                    cont.resume(returning: .failure(error))
                }
            }
        }
    }
}

/// PII types from core/include/constants.h (DEFAULT_PII_TYPES).
let DEFAULT_PII_TYPES: [String] = [
    "account_number",
    "private_address",
    "private_date",
    "private_email",
    "private_person",
    "private_phone",
    "private_url",
    "secret",
]

/// PII categories from core/include/constants.h (PII_CATEGORIES).
struct PIICategory: Equatable {
    let id: String
    let labelKey: String
    let types: [String]
}

let PII_CATEGORIES: [PIICategory] = [
    PIICategory(id: "FINANCIAL", labelKey: "PII_Category_Financial", types: ["account_number"]),
    PIICategory(id: "CONTACT", labelKey: "PII_Category_Contact", types: ["private_address", "private_email", "private_phone"]),
    PIICategory(id: "PERSONAL", labelKey: "PII_Category_Personal", types: ["private_person", "private_date"]),
    PIICategory(id: "DIGITAL", labelKey: "PII_Category_Digital", types: ["private_url", "secret"]),
]

func piiTypeLabelKey(_ type: String) -> String { "PII_Type_\(type)" }
