import SwiftUI

/// Statistics card: request/redaction counters for the selected profile,
/// refreshed live by the status poll (Windows UpdateStats parity).
struct StatisticsCard: View {
    @EnvironmentObject var appState: AppState

    private var profile: ApiKeyProfileModel? { appState.selectedProfile }

    var body: some View {
        Card("HomePage_Statistics_Title.Text") {
            HStack {
                if let stats = profile?.stats {
                    Text(LocFormat(
                        "MacStats_Format",
                        String(stats.totalRequests),
                        String(stats.totalPIIDetected),
                        String(stats.totalRegexMatches),
                        String(stats.totalKeywordMatches)))
                } else {
                    Text(LocFormat("MacStats_Format", "0", "0", "0", "0"))
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Button {
                    guard var updated = profile else { return }
                    updated.stats = .empty
                    Task { await appState.saveProfile(updated) }
                } label: {
                    LocText("HomePage_Statistics_ClearButton.Content")
                }
                .disabled(profile == nil)
            }
        }
    }
}

/// Session matches card: the redactions detected for the selected profile in
/// the current session (newest first), with a Clear button.
struct MatchesCard: View {
    @EnvironmentObject var appState: AppState

    var body: some View {
        Card("HomePage_SessionRedactions_Title.Text") {
            VStack(alignment: .leading, spacing: 8) {
                HintText(key: "HomePage_SessionRedactions_Description.Text")
                if appState.matches.isEmpty {
                    HintText(key: "HomePage_SessionRedactions_Empty")
                } else {
                    // Independent scrolling region: a plain List scrolls on
                    // macOS inside the page's ScrollView (a nested VStack
                    // would be clipped, not scrolled — the Windows/Linux
                    // nested scroll viewers do scroll).
                    List {
                        ForEach(appState.matches.reversed()) { match in
                            Text(LocFormat(
                                "MacMatches_LineFormat",
                                match.timestamp,
                                localizedMatchType(match.type),
                                match.detail,
                                match.matchedText))
                            .font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                            .listRowSeparator(.hidden)
                            .listRowBackground(Color.clear)
                            .listRowInsets(EdgeInsets(top: 2, leading: 8, bottom: 2, trailing: 8))
                        }
                    }
                    .listStyle(.plain)
                    .scrollContentBackground(.hidden)
                    // Explicit height: a List inside the page ScrollView
                    // collapses to ~zero on macOS without one (the card then
                    // renders "empty" even with matches present).
                    .frame(height: min(max(CGFloat(appState.matches.count) * 22 + 12, 48), 220))
                }
                HStack {
                    Spacer()
                    Button {
                        Task { await appState.clearMatches() }
                    } label: {
                        LocText("HomePage_SessionRedactions_ClearButton.Content")
                    }
                    .disabled(appState.matches.isEmpty)
                }
            }
        }
    }

    private func localizedMatchType(_ type: String) -> String {
        switch type.lowercased() {
        case "keyword": return LocString("MatchType_Keyword")
        case "regex": return LocString("MatchType_Regex")
        case "pii": return LocString("MatchType_PII")
        default: return type
        }
    }
}

/// Logs card: enable logging, show-sensitive (verbose) toggle with the warning
/// dialog, open log file / folder (Finder), and delete-all-logs.
struct LogsCard: View {
    @EnvironmentObject var appState: AppState
    @State private var errorAlert: ErrorAlert?
    @State private var showSensitiveWarning = false

    var body: some View {
        Card("HomePage_Logs_Title.Text") {
            VStack(alignment: .leading, spacing: 10) {
                Toggle(isOn: Binding(
                    get: { appState.loggingEnabledEffective },
                    set: { newValue in
                        Task {
                            if case .failure(let error) = await appState.setLoggingEnabledOptimistic(newValue) {
                                errorAlert = ErrorAlert(titleKey: "Dialog_Error_Title", message: error.surfacedMessage)
                            }
                        }
                    }
                )) {
                    LocText("HomePage_Logs_EnableLoggingCheck.Content")
                }
                .toggleStyle(.checkbox)
                .accessibilityIdentifier("logging.enabled")

                Toggle(isOn: Binding(
                    get: { appState.showSensitiveEffective },
                    set: { newValue in
                        if newValue {
                            showSensitiveWarning = true
                        } else {
                            setShowSensitive(false)
                        }
                    }
                )) {
                    LocText("HomePage_Logs_SensitiveCheck.Content")
                }
                .toggleStyle(.checkbox)
                .accessibilityIdentifier("logging.show-sensitive")

                HStack(spacing: 12) {
                    Button {
                        NSWorkspace.shared.open(AppPaths.logFile)
                    } label: {
                        LocText("HomePage_Logs_OpenLogButton.Content")
                    }
                    Button {
                        NSWorkspace.shared.open(AppPaths.configDir)
                    } label: {
                        LocText("HomePage_Logs_OpenFolderButton.Content")
                    }
                    Spacer()
                    Button(role: .destructive) {
                        clearLogsOnDisk()
                    } label: {
                        LocText("HomePage_Logs_ClearButton.Content")
                    }
                }
                HintText(key: "MacLogs_Disclaimer")
            }
        }
        .alert(item: $errorAlert) { alert in
            Alert(
                title: Text(LocString(alert.titleKey)),
                message: Text(alert.message),
                dismissButton: .default(Text(LocString("Dialog_OKButton"))))
        }
        .confirmationDialog(
            Text(LocString("Dialog_EnableSensitiveInfo_Title")),
            isPresented: $showSensitiveWarning,
            titleVisibility: .visible
        ) {
            Button(LocString("Common_Yes")) { setShowSensitive(true) }
            Button(LocString("Common_No"), role: .cancel) {}
        } message: {
            LocText("Dialog_EnableSensitiveInfo_Message")
        }
    }

    private func setShowSensitive(_ value: Bool) {
        Task {
            if case .failure(let error) = await appState.setShowSensitiveOptimistic(value) {
                // The engine refuses to arm sensitive logging while logging is
                // off — surface the hint like the Linux status bar message.
                errorAlert = ErrorAlert(
                    titleKey: "Dialog_Error_Title",
                    message: error.surfacedMessage)
            }
        }
    }

    /// Same files the Windows/Linux GUIs delete directly on disk: the current
    /// log plus every archived session log.
    private func clearLogsOnDisk() {
        let alert = NSAlert()
        alert.messageText = LocString("Dialog_DeleteAllLogs_Title")
        alert.informativeText = LocString("Dialog_DeleteAllLogs_Message")
        alert.addButton(withTitle: LocString("Dialog_DeleteAllLogs_Button"))
        alert.addButton(withTitle: LocString("Dialog_CancelButton"))
        guard alert.runModal() == .alertFirstButtonReturn else { return }

        try? FileManager.default.removeItem(at: AppPaths.logFile)
        if let entries = try? FileManager.default.contentsOfDirectory(
            at: AppPaths.sessionsDir, includingPropertiesForKeys: nil) {
            for entry in entries {
                try? FileManager.default.removeItem(at: entry)
            }
        }
    }
}
