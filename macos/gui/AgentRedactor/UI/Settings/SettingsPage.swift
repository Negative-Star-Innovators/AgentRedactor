import SwiftUI
import ServiceManagement

/// Settings page: language combo (53 languages, live switch), start-on-login
/// (SMAppService — the macOS equivalent of the Windows startup task / Linux
/// autostart entry), update check, and master-password enable/disable through
/// direct Apple authentication followed by the control API.
struct SettingsPage: View {
    @EnvironmentObject var appState: AppState
    @ObservedObject private var loc = LocalizationManager.shared

    var body: some View {
        VStack(spacing: 16) {
            MasterPasswordCard()
            LanguageCard()
            StartOnLoginCard()
            #if !APPSTORE
            UpdatesCard()
            #endif
        }
    }
}

// MARK: - Language

private struct LanguageCard: View {
    @EnvironmentObject var appState: AppState
    @ObservedObject private var loc = LocalizationManager.shared

    /// Effective engine tag; an unrecognized value maps to System default
    /// (the engine always reports the OS-resolved effective tag, so this is
    /// normally a supported tag already).
    private var selectedTag: String {
        let tag = appState.settings.appLanguage
        return isSupportedLanguage(tag) ? tag : ""
    }

    var body: some View {
        Card("SettingsPage_Language_Header.Header") {
            Picker(selection: Binding(
                get: { selectedTag },
                set: { newTag in
                    // Apply immediately so the UI switches without waiting for
                    // the settings-poll round-trip; the poll reconciles.
                    LocalizationManager.shared.apply(languageTag: newTag)
                    Task {
                        _ = await appState.putSetting(key: "appLanguage", value: newTag)
                    }
                }
            )) {
                LocText("SettingsPage_Language_SystemDefault").tag("")
                ForEach(kSupportedLanguages, id: \.tag) { language in
                    Text(language.nativeName).tag(language.tag)
                }
            } label: {
                EmptyView()
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .frame(maxWidth: 260, alignment: .leading)
        }
    }
}

// MARK: - Start on login

private struct StartOnLoginCard: View {
    @EnvironmentObject var appState: AppState
    @State private var serviceError: String?

    var body: some View {
        Card {
            VStack(alignment: .leading, spacing: 8) {
                Toggle(isOn: Binding(
                    get: { appState.startOnBootEffective },
                    set: { newValue in
                        // Instant: AppState publishes the override now and
                        // serializes the engine write; the override holds
                        // until the poll confirms (no bounce).
                        Task {
                            switch await appState.setStartOnBoot(newValue) {
                            case .success:
                                setLoginItemEnabled(newValue)
                            case .failure(let error):
                                serviceError = error.surfacedMessage
                            }
                        }
                    }
                )) {
                    LocText("MacSettings_StartOnLogin")
                }
                .toggleStyle(.checkbox)
                if let serviceError {
                    Text(serviceError)
                        .font(.callout)
                        .foregroundStyle(.red)
                }
            }
        }
    }

    /// SMAppService is the macOS equivalent of the Windows startup task /
    /// Linux autostart .desktop entry. Kept in agreement with the persisted
    /// engine setting by the settings poll (see AppState.syncLoginItem).
    func setLoginItemEnabled(_ enabled: Bool) {
        do {
            if enabled {
                try SMAppService.mainApp.register()
            } else {
                try SMAppService.mainApp.unregister()
            }
        } catch {
            serviceError = error.localizedDescription
        }
    }
}

// MARK: - Updates

private struct UpdatesCard: View {
    @EnvironmentObject var appState: AppState
    @State private var checking = false
    @State private var alert: ErrorAlert?

    private var versionText: String {
        LocFormat("SettingsPage_Updates_CurrentVersion",
                  (Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String) ?? "dev")
    }

    var body: some View {
        Card("SettingsPage_Updates_Header") {
            VStack(alignment: .leading, spacing: 10) {
                Text(versionText).foregroundStyle(.secondary)
                Button {
                    Task { await check() }
                } label: {
                    if checking {
                        ProgressView()
                            .controlSize(.small)
                        Text(" ")
                        LocText("SettingsPage_Updates_Checking")
                    } else {
                        LocText("SettingsPage_Updates_CheckButton")
                    }
                }
                .disabled(checking)
            }
        }
        .alert(item: $alert) { item in
            Alert(
                title: Text(LocString(item.titleKey)),
                message: Text(item.message),
                dismissButton: .default(Text(LocString("Dialog_OKButton"))))
        }
    }

    private func check() async {
        checking = true
        defer { checking = false }
        switch await appState.checkForUpdatesFromSettings() {
        case .success(.upToDate):
            alert = ErrorAlert(titleKey: "SettingsPage_Updates_Header", message: LocString("SettingsPage_Updates_UpToDate"))
        case .success(.updateReady):
            break // the root view shows the restart-to-update prompt
        case .failure(let error):
            alert = ErrorAlert(titleKey: "SettingsPage_Updates_Header", message: error.surfacedMessage)
        }
    }
}

// MARK: - Master password

struct MasterPasswordCard: View {
    @EnvironmentObject var appState: AppState
    @State private var alert: ErrorAlert?
    @State private var busy = false

    var body: some View {
        Card("SettingsPage_MasterPassword_Header.Header") {
            VStack(alignment: .leading, spacing: 8) {
                if appState.settings.masterPasswordEnabled {
                    Button(role: .destructive) {
                        Task { await disable() }
                    } label: {
                        LocText("Dialog_DisableButton")
                    }
                } else {
                    Button {
                        Task { await enable() }
                    } label: {
                        LocText("Dialog_EnableButton")
                    }
                }
            }
            .disabled(busy)
        }
        .alert(item: $alert) { item in
            Alert(
                title: Text(LocString(item.titleKey)),
                message: Text(item.message),
                dismissButton: .default(Text(LocString("Dialog_OKButton"))))
        }
    }

    private func enable() async {
        busy = true
        defer { busy = false }
        // Enabling does not require authentication; disabling and unlocking do.
        let secret = appState.generateProtectionSecret()
        if let storeError = appState.authGate.storeSecret(secret) {
            showError(LocString("MacPassword_EnableFailed") + " " + storeError)
            return
        }
        if case .failure(let error) = await appState.enableMasterPassword(secret: secret) {
            appState.authGate.deleteSecret()
            showError(LocString("MacPassword_EnableFailed") + " " + error.surfacedMessage)
        }
    }

    private func disable() async {
        busy = true
        defer { busy = false }
        switch await appState.unlockWithStoredPassword() {
        case .failure(let error):
            if !isCancellation(error) { showError(error.surfacedMessage) }
            return
        case .success:
            break
        }
        switch await appState.disableMasterPassword() {
        case .success:
            appState.authGate.deleteSecret()
        case .failure(let error):
            showError(error.surfacedMessage)
        }
    }

    private func isCancellation(_ error: Error) -> Bool {
        if let gateError = error as? AuthGateError, case .canceled = gateError {
            return true
        }
        return false
    }

    private func showError(_ message: String) {
        alert = ErrorAlert(
            titleKey: "SettingsPage_MasterPassword_Header.Header",
            message: message)
    }
}
