import SwiftUI

/// Regex page: list editor for the selected profile (Linux regex card /
/// Windows RegexPage parity). Client-side validation mirrors the CLI and the
/// other GUIs (normalize braces, ICU compile check, duplicate detection); the
/// engine remains the authority and its errors surface in alerts.
struct RegexPage: View {
    @EnvironmentObject var appState: AppState
    @State private var newPattern: String = ""
    @State private var errorAlert: ErrorAlert?

    private var profile: ApiKeyProfileModel? { appState.selectedProfile }

    var body: some View {
        VStack(spacing: 16) {
            Card("HomePage_Regex_Title.Text") {
                VStack(alignment: .leading, spacing: 8) {
                    HintText(key: "HomePage_Regex_Description.Text")
                    if let profile {
                        if !profile.regexPatterns.isEmpty {
                            HStack(spacing: 12) {
                                LocText("HomePage_Regex_EnabledHeader.Text")
                                    .frame(width: 60, alignment: .leading)
                                LocText("HomePage_Regex_PatternHeader.Text")
                                Spacer()
                            }
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                        }
                        ForEach(Array(profile.regexPatterns.enumerated()), id: \.offset) { index, _ in
                            RegexRow(profile: profile, index: index, errorAlert: $errorAlert)
                        }
                        HStack(spacing: 8) {
                            TextField(LocString("HomePage_Regex_Placeholder.PlaceholderText"), text: $newPattern)
                                .textFieldStyle(.roundedBorder)
                                .font(.system(.body, design: .monospaced))
                                .onSubmit { addRegex() }
                            Button {
                                addRegex()
                            } label: {
                                LocText("HomePage_Regex_AddButton.Content")
                            }
                        }
                    } else {
                        HintText(key: "HomePage_ProfilesHeader.Text")
                    }
                }
            }
        }
        .alert(item: $errorAlert) { alert in
            Alert(
                title: Text(LocString(alert.titleKey)),
                message: Text(alert.message),
                dismissButton: .default(Text(LocString("Dialog_OKButton"))))
        }
    }

    private func addRegex() {
        guard var updated = profile else { return }
        let normalized = RegexSupport.normalizeBraces(newPattern.trimmingCharacters(in: .whitespacesAndNewlines))
        guard !normalized.isEmpty else { return }
        if let error = RegexSupport.validationError(normalized) {
            errorAlert = ErrorAlert(
                titleKey: "Dialog_ValidationError_Title",
                message: LocFormat("Validation_InvalidRegex", error))
            return
        }
        if updated.regexPatterns.contains(where: { $0.pattern == normalized }) {
            errorAlert = ErrorAlert(
                titleKey: "Dialog_ValidationError_Title",
                message: LocString("Validation_DuplicateEntry"))
            return
        }
        updated.regexPatterns.append(RegexEntryModel(pattern: normalized, enabled: true))
        Task {
            switch await appState.saveProfile(updated) {
            case .success:
                newPattern = ""
            case .failure(let error):
                errorAlert = ErrorAlert(titleKey: "Dialog_Error_Title", message: error.surfacedMessage)
            }
        }
    }
}

private struct RegexRow: View {
    let profile: ApiKeyProfileModel
    let index: Int
    @Binding var errorAlert: ErrorAlert?
    @EnvironmentObject var appState: AppState
    @State private var text: String
    @FocusState private var focused: Bool

    init(profile: ApiKeyProfileModel, index: Int, errorAlert: Binding<ErrorAlert?>) {
        self.profile = profile
        self.index = index
        self._errorAlert = errorAlert
        self._text = State(initialValue: profile.regexPatterns[index].pattern)
    }

    var body: some View {
        HStack(spacing: 12) {
            Toggle("", isOn: Binding(
                get: { profile.regexPatterns[index].enabled },
                set: { newValue in
                    var updated = profile
                    updated.regexPatterns[index].enabled = newValue
                    commit(updated)
                }
            ))
            .toggleStyle(.checkbox)
            .labelsHidden()
            .frame(width: 60, alignment: .leading)

            TextField("", text: $text)
                .textFieldStyle(.roundedBorder)
                .font(.system(.body, design: .monospaced))
                .focused($focused)
                .onSubmit { commitText() }

            Button {
                var updated = profile
                updated.regexPatterns.remove(at: index)
                commit(updated)
            } label: {
                Image(systemName: "xmark.circle.fill")
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            .help(LocString("Dialog_RemoveButton"))
        }
        .onChange(of: focused) { _, isFocused in
            if !isFocused { commitText() } // LostFocus validation parity
        }
    }

    private func commitText() {
        let normalized = RegexSupport.normalizeBraces(text.trimmingCharacters(in: .whitespacesAndNewlines))
        guard normalized == profile.regexPatterns[index].pattern else {
            if let error = RegexSupport.validationError(normalized) {
                errorAlert = ErrorAlert(
                    titleKey: "Dialog_ValidationError_Title",
                    message: LocFormat("Validation_InvalidRegex", error))
                text = profile.regexPatterns[index].pattern // revert
                return
            }
            var updated = profile
            updated.regexPatterns[index].pattern = normalized
            commit(updated)
            return
        }
        // Re-enable a row whose pattern matches the stored one but toggled off?
        // No-op: keep the stored value authoritative.
    }

    private func commit(_ updated: ApiKeyProfileModel) {
        Task {
            if case .failure(let error) = await appState.saveProfile(updated) {
                errorAlert = ErrorAlert(titleKey: "Dialog_Error_Title", message: error.surfacedMessage)
            }
        }
    }
}
