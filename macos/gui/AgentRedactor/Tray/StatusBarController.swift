import AppKit
import SwiftUI
import Combine

/// System tray icon (Windows shell notify icon / Linux QSystemTrayIcon
/// parity): Open, Start on Boot (checkable), Language submenu (53 entries with
/// checkmarks, live switch), Quit. The menu is rebuilt when the language
/// changes so every label follows the active catalog; the Start-on-Boot
/// checkmark tracks the setting LIVE (the GUI checkbox and tray stay in
/// agreement no matter which one you flip).
@MainActor
final class StatusBarController: NSObject, ObservableObject {
    private let appState: AppState
    private var statusItem: NSStatusItem?
    private var cancellable: Any?
    private var bootStateCancellable: AnyCancellable?

    var onOpen: (() -> Void)?
    var onQuit: (() -> Void)?

    init(appState: AppState) {
        self.appState = appState
        super.init()
        cancellable = NotificationCenter.default.addObserver(
            forName: .init("LocalizationChanged"),
            object: nil,
            queue: .main) { [weak self] _ in
            DispatchQueue.main.async {
                self?.rebuildMenu()
            }
        }
        // Live checkmark: update whenever the effective start-on-boot value
        // changes (engine poll, GUI toggle with pending override, or a tray
        // click below) — no stale state from menu-build time.
        bootStateCancellable = Publishers.CombineLatest(appState.$settings, appState.$startOnBootOverride)
            .receive(on: DispatchQueue.main)
            .sink { [weak self] settings, override in
                guard let self else { return }
                self.statusItem?.menu?
                    .item(withTag: self.startOnBootTag)?
                    .state = (override ?? settings.startOnBoot) ? .on : .off
            }
    }

    func install() {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let button = item.button {
            if let image = NSImage(named: "StatusIcon") {
                image.isTemplate = true
                button.image = image
            } else {
                button.image = NSImage(systemSymbolName: "lock.shield", accessibilityDescription: "Agent Redactor")
            }
            button.toolTip = LocString("AppDisplayName")
            button.action = #selector(statusItemClicked)
            button.target = self
        }
        statusItem = item
        rebuildMenu()
    }

    @objc private func statusItemClicked() {
        onOpen?()
    }

    // MARK: - Menu

    private let openTag = 1
    private let startOnBootTag = 2
    private let languageMenuTag = 3
    private let quitTag = 4

    func rebuildMenu() {
        guard let item = statusItem else { return }
        let menu = NSMenu(title: LocString("AppDisplayName"))

        let openItem = NSMenuItem(title: LocString("TrayMenu_Open"), action: #selector(openClicked), keyEquivalent: "")
        openItem.tag = openTag
        openItem.target = self
        menu.addItem(openItem)

        let bootItem = NSMenuItem(title: LocString("TrayMenu_StartOnBoot"), action: #selector(startOnBootClicked), keyEquivalent: "")
        bootItem.tag = startOnBootTag
        bootItem.target = self
        bootItem.state = appState.settings.startOnBoot ? .on : .off
        menu.addItem(bootItem)

        let languageItem = NSMenuItem(title: LocString("TrayMenu_Language"), action: nil, keyEquivalent: "")
        languageItem.tag = languageMenuTag
        let submenu = NSMenu(title: LocString("TrayMenu_Language"))
        let currentTag = LocalizationManager.shared.effectiveTag
        for language in kSupportedLanguages {
            let entry = NSMenuItem(title: language.nativeName, action: #selector(languageClicked(_:)), keyEquivalent: "")
            entry.representedObject = language.tag
            entry.state = language.tag == currentTag ? .on : .off
            entry.target = self
            submenu.addItem(entry)
        }
        languageItem.submenu = submenu
        menu.addItem(languageItem)

        menu.addItem(.separator())

        let quitItem = NSMenuItem(title: LocString("TrayMenu_Quit"), action: #selector(quitClicked), keyEquivalent: "")
        quitItem.tag = quitTag
        quitItem.target = self
        menu.addItem(quitItem)

        item.menu = menu
    }

    @objc private func openClicked() {
        onOpen?()
    }

    @objc private func startOnBootClicked() {
        let newValue = !appState.startOnBootEffective
        Task {
            // Same authoritative path as the GUI checkbox: instant override,
            // serialized writes, poll-confirmed handoff (and the poll's
            // syncLoginItem reconciles SMAppService registration).
            _ = await appState.setStartOnBoot(newValue)
        }
    }

    @objc private func languageClicked(_ sender: NSMenuItem) {
        guard let tag = sender.representedObject as? String else { return }
        // Apply immediately so the UI switches without waiting for the
        // settings-poll round-trip; the poll later reconciles the persisted tag.
        LocalizationManager.shared.apply(languageTag: tag)
        Task {
            _ = await appState.putSetting(key: "appLanguage", value: tag)
        }
        rebuildMenu()
    }

    @objc private func quitClicked() {
        onQuit?()
    }
}
