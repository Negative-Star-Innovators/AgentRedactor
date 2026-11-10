import AppKit
import SwiftUI

/// Application delegate: single-instance guard, engine bootstrap, tray,
/// close-to-tray, quit confirmation, 30-second focus-loss locking, and the
/// 10-minute inactivity re-lock (input while unlocked resets that timer).
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    static weak var current: AppDelegate?
    let appState = AppState()
    private var statusBar: StatusBarController?
    private var lockFileHandle: FileHandle?
    private var activityMonitor: Any?
    private var focusLossLockItem: DispatchWorkItem?
    private var quitInProgress = false
    private var terminating = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        Self.current = self
        AppPaths.ensureConfigDir()

        #if SELFRELEASE
        // Velopack startup logic: handles post-update restart/apply hooks and
        // may exit or restart the process; must run before anything else
        // (Linux main.cpp VelopackApp::Build().Run() parity). No-op when this
        // bundle is not a Velopack install.
        VelopackUpdateProvider.runStartupHooks()
        #endif

        // Single-instance guard: a second launch activates the running
        // instance (distributed notification) and exits.
        if let handle = SingleInstance.acquireLock(configDir: AppPaths.configDir) {
            lockFileHandle = handle
            DistributedNotificationCenter.default().addObserver(
                self,
                selector: #selector(activateFromSecondInstance),
                name: SingleInstance.activateNotification,
                object: nil)
        } else {
            SingleInstance.notifyRunningInstanceAndExit()
        }

        NSApp.setActivationPolicy(.regular)

        #if !APPSTORE
        // Silent user-space CLI install (Kimi-Code/Homebrew pattern):
        // ~/.agentredactor/bin + PATH export in the shell rc. No prompt, no
        // root; takes effect in new Terminal sessions. Runs on every launch
        // (idempotent: refreshes the symlink, appends the line only once).
        if let installError = CliInstaller.installUserSpace() {
            NSLog("[AppDelegate] user-space CLI install: %@", installError)
        }
        #endif

        let tray = StatusBarController(appState: appState)
        tray.onOpen = { [weak self] in self?.showMainWindow() }
        tray.onQuit = { [weak self] in self?.quitConfirmed() }
        tray.install()
        statusBar = tray

        // Inactivity re-lock: any keyboard/mouse activity in the app resets
        // the timer (Linux qApp event filter parity).
        activityMonitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .leftMouseDown, .rightMouseDown, .otherMouseDown, .scrollWheel]) { [weak self] event in
            self?.appState.noteUserActivity()
            return event
        }
        NotificationCenter.default.addObserver(self,
            selector: #selector(handleDidBecomeActive(_:)),
            name: NSApplication.didBecomeActiveNotification, object: NSApp)
        NotificationCenter.default.addObserver(self,
            selector: #selector(handleDidResignActive(_:)),
            name: NSApplication.didResignActiveNotification, object: NSApp)
        NotificationCenter.default.addObserver(self,
            selector: #selector(handleWindowDidBecomeMain(_:)),
            name: NSWindow.didBecomeMainNotification, object: nil)
        // Apply the persisted language override as early as possible.
        LocalizationManager.shared.apply(languageTag: Locale.current.identifier)

        Task { @MainActor in
            let ok = await self.appState.bootstrap()
            self.statusBar?.rebuildMenu()
            if !ok {
                self.showEngineStartFailure()
                return
            }
            self.showMainWindow()
            // Background self-update check (Linux MainWindow parity: runs
            // whether or not the settings page is ever opened; prompts only
            // when an update is ready).
            self.appState.checkForUpdatesOnLaunch()
        }
    }

    @objc private func handleWindowDidBecomeMain(_ notification: Notification) {
        guard let window = notification.object as? NSWindow else { return }
        window.delegate = self
        window.title = LocString("AppDisplayName")
    }

    /// Attach to an existing window when showing it; didBecomeMain covers a
    /// SwiftUI window created after the initial bootstrap completes.
    func attachWindowDelegate() {
        guard let window = NSApp.windows.first(where: { $0.isVisible || $0.title != "" }) else { return }
        window.delegate = self
        window.center()
        window.title = LocString("AppDisplayName")
    }

    // MARK: - Window management

    func showMainWindow() {
        attachWindowDelegate()
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        if let window = NSApp.mainWindow ?? NSApp.windows.first {
            window.makeKeyAndOrderFront(nil)
        }
        appState.noteUserActivity()
        requestUnlockOnReturn()
    }

    private func requestUnlockOnReturn() {
        guard !quitInProgress, !terminating, NSApp.isActive,
              NSApp.windows.contains(where: { $0.isVisible }) else { return }
        Task {
            await appState.refreshLockState()
            guard !quitInProgress, !terminating, appState.isLocked, NSApp.isActive,
                  NSApp.windows.contains(where: { $0.isVisible }) else { return }
            _ = await appState.unlockWithLocalAuth()
        }
    }

    func hideWindowToTray() {
        focusLossLockItem?.cancel()
        NSApp.windows.first?.orderOut(nil)
        // Close-to-tray re-lock (Linux closeEvent parity). Hide first so no
        // return-to-app prompt can authenticate a window on its way out. Lock
        // unconditionally: cached state may not yet show a just-enabled password,
        // and the engine treats locking an unprotected session as a no-op.
        Task { await appState.lockSession() }
    }

    // MARK: - NSWindowDelegate

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        hideWindowToTray()
        return false
    }

    // MARK: - Application focus-loss locking

    @objc private func handleDidBecomeActive(_ notification: Notification) {
        focusLossLockItem?.cancel()
        focusLossLockItem = nil
        requestUnlockOnReturn()
    }

    @objc private func handleDidResignActive(_ notification: Notification) {
        guard !terminating, appState.status.unlocked else { return }
        focusLossLockItem?.cancel()
        // Schedule even when the settings poll has not observed a just-enabled
        // password yet; the 30-second callback revalidates the engine state.
        // Explicit notification observers avoid optional NSApplicationDelegate
        // forwarding gaps in the SwiftUI application lifecycle.
        let item = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.focusLossLockItem = nil
            guard !self.terminating, self.appState.protectionEnabled,
                  self.appState.status.unlocked else { return }
            Task { await self.appState.lockSession() }
        }
        focusLossLockItem = item
        DispatchQueue.main.asyncAfter(deadline: .now() + 30, execute: item)
    }

    // MARK: - Second instance

    @objc private func activateFromSecondInstance() {
        showMainWindow()
    }

    // MARK: - Quit

    /// Tray Quit / Cmd+Q path. The confirmation mirrors the Linux GUI: when
    /// this GUI owns the engine, quitting stops protection entirely; when the
    /// engine survives, it keeps running (locked) in the background.
    func quitConfirmed() {
        if terminating || quitInProgress { return }
        quitInProgress = true
        defer { quitInProgress = false }
        let alert = NSAlert()
        alert.messageText = LocString("Dialog_Quit_Title")
        if appState.engineSpawned {
            alert.informativeText = LocString("Dialog_Quit_Message")
        } else {
            alert.informativeText = LocString("MacQuit_KeepEngine_Message")
        }
        alert.addButton(withTitle: LocString("Dialog_QuitButton"))
        alert.addButton(withTitle: LocString("Dialog_CancelButton"))
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        terminating = true
        appState.shutdown()
        NSApp.terminate(nil)
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        // Approved quit paths (tray Quit dialog, update restart) set
        // `terminating`; let the termination proceed.
        if terminating { return .terminateNow }
        // System logout/shutdown/restart arrive as a quit Apple event with a
        // quit-reason attribute. Returning .terminateCancel there CANCELS the
        // whole shutdown, so the confirmation dialog is skipped on this path;
        // cleanup still runs in applicationWillTerminate.
        if isSystemInitiatedTermination() {
            terminating = true
            return .terminateNow
        }
        // User quit (Cmd+Q / Dock Quit): run the same confirmed flow.
        quitConfirmed()
        return .terminateCancel
    }

    /// Logout/shutdown/restart/Finder-Quit-All carry a `kAEQuitReason`
    /// (`logo`/`shut`/`rest`/`qual`); user-initiated quits (Cmd+Q, Dock Quit,
    /// tray Quit) have no such attribute.
    private func isSystemInitiatedTermination() -> Bool {
        guard let event = NSAppleEventManager.shared().currentAppleEvent,
              let reason = event.attributeDescriptor(forKeyword: AEKeyword(kAEQuitReason)) else {
            return false
        }
        switch reason.enumCodeValue {
        case kAELogOut, kAEShutDown, kAERestart, kAEQuitAll:
            return true
        default:
            return false
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return false // the tray keeps the app alive
    }

    func applicationWillTerminate(_ notification: Notification) {
        focusLossLockItem?.cancel()
        appState.shutdown()
    }

    /// Quit path for a Velopack update restart: the updater is waiting (60 s)
    /// for this process to exit so it can swap the app bundle — stop the
    /// engine and terminate without the quit confirmation dialog.
    func terminateForUpdateRestart() {
        if terminating { return }
        terminating = true
        appState.shutdown()
        NSApp.terminate(nil)
    }

    // MARK: - Startup failure

    private func showEngineStartFailure() {
        let alert = NSAlert()
        alert.messageText = LocString("AppDisplayName")
        alert.informativeText = LocString("MacEngine_StartFailed_Message")
        alert.addButton(withTitle: LocString("Dialog_ExitButton"))
        alert.runModal()
        NSApp.terminate(nil)
    }
}
