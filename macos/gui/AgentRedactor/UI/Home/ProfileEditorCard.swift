import SwiftUI

/// One card per ApiKeyProfile (alias, local URL with port availability, upstream
/// URL, masked/revealable API key) with a Save button. Mirrors the Linux
/// profile form card including validation, the HTTP warning, and dirty-state
/// handling across poll-triggered refreshes.
struct ProfileEditorCard: View {
    @EnvironmentObject var appState: AppState
    let profile: ApiKeyProfileModel

    @State private var alias: String = ""
    @State private var portText: String = ""
    @State private var upstreamUrl: String = ""
    @State private var apiKey: String = ""
    @State private var apiKeyLoaded = false
    @State private var lastFetchedKey: String?
    @State private var showKey = false
    @State private var dirty = false
    @State private var errorAlert: ErrorAlert?
    @State private var showHttpWarning = false
    @State private var copiedNotice = false

    private var portNumber: Int? { Int(portText) }

    /// Linux updatePortStatus parity: another profile's saved port is always
    /// "taken"; this profile's running proxy counts as available; otherwise a
    /// live bind test decides.
    private var portVerdict: PortVerdict {
        guard let port = portNumber, (1024...65535).contains(port) else { return .hidden }
        for other in appState.profiles where other.id != profile.id {
            if other.port == port {
                return .usedByProfile(other.alias)
            }
        }
        if appState.status.profiles.contains(where: { $0.port == port && $0.proxyRunning }) {
            return .available
        }
        return PortCheck.isPortAvailable(port) ? .available : .inUse
    }

    var body: some View {
        Card("HomePage_ApiProxy_Title.Text") {
            VStack(alignment: .leading, spacing: 10) {
                formField(label: "HomePage_ProfileName_Label.Text") {
                    TextField(LocString("HomePage_ProfileName_Placeholder.PlaceholderText"), text: $alias)
                        .textFieldStyle(.roundedBorder)
                        .onChange(of: alias) { recomputeDirty() }
                }

                formField(label: "HomePage_LocalUrl_Label.Text") {
                    HStack(spacing: 6) {
                        Text("http://localhost:").foregroundStyle(.secondary)
                        TextField("8080", text: $portText)
                            .textFieldStyle(.roundedBorder)
                            .frame(width: 72)
                            .onChange(of: portText) { recomputeDirty() }
                        Text("/").foregroundStyle(.secondary)
                        Button {
                            copyProxyURL()
                        } label: {
                            LocText("HomePage_CopyButton.Content")
                        }
                    }
                }
                HintText(key: "HomePage_LocalUrl_Hint.Text")

                portStatusLabel

                formField(label: "HomePage_ForwardTo_Label.Text") {
                    TextField(LocString("HomePage_ForwardTo_Placeholder.PlaceholderText"), text: $upstreamUrl)
                        .textFieldStyle(.roundedBorder)
                        .onChange(of: upstreamUrl) { recomputeDirty() }
                }
                HintText(key: "HomePage_ForwardTo_Hint.Text")

                formField(label: "HomePage_ApiKey_Label.Text") {
                    Group {
                        if showKey {
                            TextField("", text: $apiKey)
                        } else {
                            SecureField("", text: $apiKey)
                        }
                    }
                    .textFieldStyle(.roundedBorder)
                    .accessibilityIdentifier("profile.api-key")
                    .onChange(of: apiKey) { dirty = true }
                }
                Toggle(isOn: $showKey) {
                    LocText("HomePage_ShowKeyCheck.Content")
                }
                .toggleStyle(.checkbox)
                .onChange(of: showKey) { _, visible in
                    if visible { loadRealApiKey() }
                }

                HStack {
                    Spacer()
                    Button {
                        Task { await save() }
                    } label: {
                        LocText("HomePage_SaveAllSettingsButton.Content")
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(!dirty)
                }
            }
        }
        .onAppear { loadFromProfile() }
        .onChange(of: profile.id) { loadFromProfile() }
        .onChange(of: profile.apiKeyMasked) { oldValue, newValue in
            guard apiKey == oldValue else { return }
            apiKey = newValue
            apiKeyLoaded = false
            lastFetchedKey = nil
            recomputeDirty()
        }
        .alert(item: $errorAlert) { alert in
            Alert(
                title: Text(LocString(alert.titleKey)),
                message: Text(alert.message),
                dismissButton: .default(Text(LocString("Dialog_OKButton"))))
        }
        .confirmationDialog(
            Text(LocString("Dialog_SecurityWarning_Title")),
            isPresented: $showHttpWarning,
            titleVisibility: .visible
        ) {
            Button(LocString("Common_Yes")) { Task { await performSave() } }
            Button(LocString("Common_No"), role: .cancel) {}
        } message: {
            LocText("Dialog_HttpWarning_Message")
        }
    }

    @ViewBuilder private var portStatusLabel: some View {
        switch portVerdict {
        case .hidden:
            EmptyView()
        case .available:
            statusLine("checkmark.circle.fill", .green, LocFormat("HomePage_ProxyStatus_PortAvailable", portText))
        case .inUse:
            statusLine("xmark.circle.fill", .red, LocFormat("HomePage_ProxyStatus_PortInUse", portText))
        case .usedByProfile(let otherAlias):
            statusLine("xmark.circle.fill", .red,
                       LocFormat("HomePage_ProxyStatus_PortUsedByProfile", portText, otherAlias))
        }
    }

    private func statusLine(_ systemImage: String, _ color: Color, _ text: String) -> some View {
        HStack(spacing: 4) {
            Image(systemName: systemImage).foregroundStyle(color)
            Text(text).foregroundStyle(color)
            if copiedNotice {
                Text("·").foregroundStyle(.secondary)
                LocText("MacHome_UrlCopied").foregroundStyle(.secondary)
            }
        }
        .font(.callout)
    }

    private func formField<Content: View>(label: String, @ViewBuilder content: () -> Content) -> some View {
        HStack(alignment: .firstTextBaseline) {
            LocText(label)
                .frame(width: 110, alignment: .leading)
            content()
        }
    }

    // MARK: - Load

    /// Dirtiness is COMPUTED (fields vs profile), not a sticky flag: the
    /// onChange handlers also fire for programmatic writes (e.g. loading the
    /// masked key at startup), which made a sticky flag lock the reveal
    /// fetch forever after a relaunch.
    private func recomputeDirty() {
        dirty = alias != profile.alias
            || (Int(portText) ?? profile.port) != profile.port
            || upstreamUrl != profile.upstreamUrl
            || (apiKey != profile.apiKeyMasked && apiKey != lastFetchedKey)
    }

    private func loadFromProfile() {
        // Always resync when (re)binding to a profile — including discarding
        // unsaved drafts on selection change. The previous dirty-guard kept
        // stale field values when the user switched profiles, so saving after
        // a switch wrote the previously selected profile's port/URL/name into
        // the other profile.
        alias = profile.alias
        portText = String(profile.port)
        upstreamUrl = profile.upstreamUrl
        apiKey = profile.apiKeyMasked
        apiKeyLoaded = false
        lastFetchedKey = nil
        showKey = false
        dirty = false
    }

    /// The profiles list only ever serves the masked key; fetch the real one
    /// (403 while locked — then keep the masked placeholder). Never clobbers
    /// an unsaved draft: toggling "show key" mid-typing just swaps the field
    /// style, it does not overwrite what the user typed.
    private func loadRealApiKey() {
        guard !apiKeyLoaded else { return }
        let profileID = profile.id
        let placeholder = profile.apiKeyMasked
        Task {
            switch await appState.fetchProfileApiKey(id: profileID) {
            case .success(let key):
                // Never clobber a typed draft: only adopt the fetched key
                // when the field still shows the masked placeholder.
                if apiKey == placeholder || apiKey.isEmpty {
                    apiKey = key
                }
                lastFetchedKey = key
                apiKeyLoaded = true
            case .failure:
                break
            }
        }
    }

    // MARK: - Save (Linux onSaveProfile parity)

    private func save() async {
        // Port range + cross-profile conflicts.
        guard let port = portNumber, (1024...65535).contains(port) else {
            errorAlert = ErrorAlert(titleKey: "Dialog_ValidationError_Title", message: LocString("Validation_PortRange"))
            return
        }
        if let other = appState.profiles.first(where: { $0.id != profile.id && $0.port == port }) {
            errorAlert = ErrorAlert(
                titleKey: "Dialog_ValidationError_Title",
                message: LocFormat("Validation_PortUsed", String(port), other.alias))
            return
        }

        // Upstream URL.
        let trimmedUrl = upstreamUrl.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedUrl.isEmpty else {
            errorAlert = ErrorAlert(titleKey: "Dialog_ValidationError_Title", message: LocString("Validation_UrlEmpty"))
            return
        }
        guard trimmedUrl.hasPrefix("http://") || trimmedUrl.hasPrefix("https://"),
              let host = URLComponents(string: trimmedUrl)?.host, !host.isEmpty else {
            errorAlert = ErrorAlert(titleKey: "Dialog_ValidationError_Title", message: LocString("Validation_UrlProtocol"))
            return
        }

        let isInsecureHttp = trimmedUrl.hasPrefix("http://")
            && host != "localhost" && !host.hasPrefix("127.")
        if isInsecureHttp {
            showHttpWarning = true
            return
        }
        await performSave()
    }

    private func performSave() async {
        var updated = profile
        updated.alias = alias
        updated.port = portNumber ?? profile.port
        updated.upstreamUrl = upstreamUrl.trimmingCharacters(in: .whitespacesAndNewlines)
        let trimmedKey = apiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        // A masked placeholder means "unchanged" engine-side. When the user
        // typed nothing and never revealed the real key, preserve the stored
        // key — even when the masked value itself is unavailable (locked
        // session / legacy-encrypted field arrived empty): send an explicit
        // masked token so a blank field can never wipe the stored key.
        if trimmedKey.isEmpty && !apiKeyLoaded {
            updated.apiKeyMasked = profile.apiKeyMasked.isEmpty ? "...****" : profile.apiKeyMasked
        } else {
            updated.apiKeyMasked = trimmedKey
        }
        switch await appState.saveProfile(updated) {
        case .success:
            dirty = false
            // The field now holds the authoritative key (just persisted).
            let saved = updated.apiKeyMasked
            if !saved.isEmpty && saved != "...****" {
                apiKeyLoaded = true
                lastFetchedKey = saved
            }
        case .failure(let error):
            errorAlert = ErrorAlert(
                titleKey: "Dialog_Error_Title",
                message: error.surfacedMessage)
        }
    }

    // MARK: - Clipboard

    private func copyProxyURL() {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString("http://localhost:\(portText)/", forType: .string)
        flashCopiedNotice()
    }

    private func flashCopiedNotice() {
        copiedNotice = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) { copiedNotice = false }
    }

    private enum PortVerdict: Equatable {
        case hidden
        case available
        case inUse
        case usedByProfile(String)
    }
}
