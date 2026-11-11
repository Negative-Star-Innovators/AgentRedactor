import SwiftUI

/// Keywords page: list editor for the selected profile with per-entry
/// case sensitivity (Linux keywords card / Windows KeywordsPage parity).
struct KeywordsPage: View {
    @EnvironmentObject var appState: AppState
    @State private var newKeyword: String = ""
    @State private var newCaseSensitive = true
    @State private var errorAlert: ErrorAlert?

    private var profile: ApiKeyProfileModel? { appState.selectedProfile }

    var body: some View {
        VStack(spacing: 16) {
            Card("HomePage_Keywords_Title.Text") {
                VStack(alignment: .leading, spacing: 8) {
                    HintText(key: "HomePage_Keywords_Description.Text")
                    if let profile {
                        if !profile.keywords.isEmpty {
                            HStack(spacing: 12) {
                                LocText("HomePage_Keywords_EnabledHeader.Text")
                                    .frame(width: 60, alignment: .leading)
                                LocText("HomePage_Keywords_CaseHeader.Text")
                                    .frame(width: 64, alignment: .leading)
                                LocText("HomePage_Keywords_KeywordHeader.Text")
                                Spacer()
                            }
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                        }
                        ForEach(Array(profile.keywords.enumerated()), id: \.offset) { index, _ in
                            KeywordRow(profile: profile, index: index, errorAlert: $errorAlert)
                        }
                        HStack(spacing: 8) {
                            TextField(LocString("HomePage_Keywords_Placeholder.PlaceholderText"), text: $newKeyword)
                                .textFieldStyle(.roundedBorder)
                                .onSubmit { addKeyword() }
                                .accessibilityIdentifier("keywords.new.text")
                            Toggle(isOn: $newCaseSensitive) {
                                LocText("HomePage_Keywords_CaseSensitiveCheck.Content")
                            }
                            .toggleStyle(.checkbox)
                            .accessibilityIdentifier("keywords.new.case-sensitive")
                            Button {
                                addKeyword()
                            } label: {
                                LocText("HomePage_Keywords_AddButton.Content")
                            }
                            .accessibilityIdentifier("keywords.add")
                        }
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

    private func addKeyword() {
        guard var updated = profile else { return }
        let text = newKeyword.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }
        if updated.keywords.contains(where: { $0.text == text && $0.caseSensitive == newCaseSensitive }) {
            errorAlert = ErrorAlert(
                titleKey: "Dialog_ValidationError_Title",
                message: LocString("Validation_DuplicateEntry"))
            return
        }
        updated.keywords.append(KeywordEntryModel(text: text, caseSensitive: newCaseSensitive, enabled: true))
        Task {
            switch await appState.saveProfile(updated) {
            case .success:
                newKeyword = ""
            case .failure(let error):
                errorAlert = ErrorAlert(titleKey: "Dialog_Error_Title", message: error.surfacedMessage)
            }
        }
    }
}

private struct KeywordRow: View {
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
        self._text = State(initialValue: profile.keywords[index].text)
    }

    var body: some View {
        HStack(spacing: 12) {
            Toggle("", isOn: Binding(
                get: { profile.keywords[index].enabled },
                set: { newValue in
                    var updated = profile
                    updated.keywords[index].enabled = newValue
                    commit(updated)
                }
            ))
            .toggleStyle(.checkbox)
            .labelsHidden()
            .frame(width: 60, alignment: .leading)

            // The Case column is a Yes/No toggle with the real value in the
            // label (Windows keywordCaseButtons parity).
            Button {
                var updated = profile
                updated.keywords[index].caseSensitive.toggle()
                commit(updated)
            } label: {
                LocText(profile.keywords[index].caseSensitive ? "Common_Yes" : "Common_No")
                    .frame(width: 64, alignment: .leading)
            }
            .buttonStyle(.plain)

            TextField("", text: $text)
                .textFieldStyle(.roundedBorder)
                .accessibilityIdentifier("keywords.row.\(index).text")
                .focused($focused)
                .onSubmit { commitText() }

            Button {
                var updated = profile
                updated.keywords.remove(at: index)
                commit(updated)
            } label: {
                Image(systemName: "xmark.circle.fill")
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            .help(LocString("Dialog_RemoveButton"))
        }
        .onChange(of: focused) { _, isFocused in
            if !isFocused { commitText() }
        }
    }

    private func commitText() {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard trimmed == profile.keywords[index].text else {
            var updated = profile
            updated.keywords[index].text = trimmed
            commit(updated)
            return
        }
    }

    private func commit(_ updated: ApiKeyProfileModel) {
        Task {
            if case .failure(let error) = await appState.saveProfile(updated) {
                errorAlert = ErrorAlert(titleKey: "Dialog_Error_Title", message: error.surfacedMessage)
            }
        }
    }
}
