import SwiftUI

/// Root shell, Windows/Linux unified-layout parity: a profile sidebar and a
/// single scrolling page whose cards all belong to the selected profile
/// (proxy settings, AI detection, regex, keywords, statistics, session
/// redactions), followed by the global cards (logs, settings). The per-tab
/// navigation experiment was replaced by this layout so the profile
/// selection and its content are always visible together — the binding can
/// never be hidden by a scrolled-out sidebar section.
struct RootView: View {
    @EnvironmentObject var appState: AppState
    @ObservedObject private var loc = LocalizationManager.shared
    @State private var updateApplyError: ErrorAlert?
    @State private var profilePendingRemoval: ApiKeyProfileModel?

    var body: some View {
        NavigationSplitView {
            List {
                Section {
                    SidebarProfileList(profilePendingRemoval: $profilePendingRemoval)
                } header: {
                    HStack {
                        LocText("HomePage_ProfilesHeader.Text")
                        Spacer()
                        // Visible remove control (the −/+ convention from
                        // Contacts-style sidebars); right-click on a profile
                        // offers the same action.
                        Button {
                            if let id = appState.selectedProfileID,
                               let p = appState.profiles.first(where: { $0.id == id }) {
                                profilePendingRemoval = p
                            }
                        } label: {
                            Image(systemName: "minus")
                                .font(.system(size: 12, weight: .semibold))
                                .frame(width: 18, height: 18)
                                .contentShape(Rectangle())
                        }
                        .buttonStyle(.borderless)
                        .disabled(appState.selectedProfileID == nil)
                        .help(LocString("Dialog_RemoveProfile_Title"))
                        Button {
                            Task { await appState.addProfile() }
                        } label: {
                            Image(systemName: "plus")
                                .font(.system(size: 12, weight: .semibold))
                                .frame(width: 18, height: 18)
                                .contentShape(Rectangle())
                        }
                        .buttonStyle(.borderless)
                        .help(LocString("MacProfiles_AddProfile"))
                    }
                }
            }
            .navigationSplitViewColumnWidth(min: 200, ideal: 220)
        } detail: {
            ScrollView {
                VStack(spacing: 16) {
                    if appState.connectionLost {
                        HStack(spacing: 6) {
                            Image(systemName: "exclamationmark.triangle")
                            LocText("MacEngine_NotRunning")
                        }
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.horizontal, 4)
                    }
                    HomePage()
                }
                .padding(20)
                .frame(maxWidth: 720, alignment: .top)
                .frame(maxWidth: .infinity)
            }
        }
        .overlay {
            if appState.isLocked {
                LockOverlayView()
                    .transition(.opacity)
            }
        }
        // Blocking first-run model download. Deliberately an inline overlay
        // (not a .sheet): a window with an attached sheet cannot be closed,
        // while Windows/Linux let the user close the app mid-download — the
        // engine keeps downloading in the background either way. Rendered as
        // a window-filling dim behind a proper card.
        .overlay {
            if appState.status.modelDownloadRequired {
                ZStack {
                    Color.black.opacity(0.5)
                        .ignoresSafeArea()
                    ModelDownloadSheet()
                        .padding(.horizontal, 32)
                        .padding(.vertical, 30)
                        .frame(width: 460)
                        .background(
                            RoundedRectangle(cornerRadius: 14)
                                .fill(Color(nsColor: .windowBackgroundColor))
                                .shadow(color: .black.opacity(0.4), radius: 20, y: 8)
                        )
                }
                .transition(.opacity)
            }
        }
        // Restart-to-update prompt (Linux updateDownloaded dialog parity):
        // shown for both the launch-time background check and the settings
        // button, with version + release notes from the feed.
        .alert(item: updatePromptBinding) { info in
            Alert(
                title: Text(LocString("UpdateAvailable_Title")),
                message: Text(updatePromptMessage(info)),
                primaryButton: .default(Text(LocString("Dialog_RestartNowButton"))) {
                    Task {
                        if let error = await appState.applyStagedUpdateAndRestart() {
                            updateApplyError = ErrorAlert(
                                titleKey: "Dialog_Error_Title",
                                message: error.surfacedMessage)
                        }
                    }
                },
                secondaryButton: .cancel(Text(LocString("Dialog_RestartLaterButton"))))
        }
        .alert(item: $updateApplyError) { item in
            Alert(
                title: Text(LocString(item.titleKey)),
                message: Text(item.message),
                dismissButton: .default(Text(LocString("Dialog_OKButton"))))
        }
        .alert(item: $profilePendingRemoval) { profile in
            Alert(
                title: Text(LocString("Dialog_RemoveProfile_Title")),
                message: Text(LocString("Dialog_RemoveProfile_Message")),
                primaryButton: .destructive(Text(LocString("Dialog_RemoveProfile_Title"))) {
                    Task { await appState.removeProfile(id: profile.id) }
                },
                secondaryButton: .cancel(Text(LocString("Dialog_CancelButton"))))
        }
        .id(loc.effectiveTag) // force re-resolution of every string on language switch
        .environment(\.layoutDirection, loc.layoutDirection)
    }

    private var updatePromptBinding: Binding<UpdateReleaseInfo?> {
        Binding(
            get: { appState.updatePrompt },
            set: { _ in appState.dismissUpdatePrompt() })
    }

    /// Localized prompt body: the version line plus the feed's release notes
    /// (markdown shown verbatim) when the release carries them.
    private func updatePromptMessage(_ info: UpdateReleaseInfo) -> String {
        var message = LocFormat("UpdateAvailable_Message", info.version)
        if let notes = info.notesMarkdown, !notes.isEmpty {
            message += "\n\n" + notes
        }
        return message
    }
}

/// Profile switcher in the sidebar; the selection is the working profile for
/// every card on the page. Right-click a profile to remove it (with
/// confirmation, Windows/Linux parity).
struct SidebarProfileList: View {
    @EnvironmentObject var appState: AppState
    @Binding var profilePendingRemoval: ApiKeyProfileModel?

    var body: some View {
        ForEach(appState.profiles) { profile in
            HStack(spacing: 6) {
                Image(systemName: profile.proxyRunning ? "circle.fill" : "circle")
                    .font(.system(size: 8))
                    .foregroundStyle(profile.proxyRunning ? .green : .secondary)
                Text(profile.alias.isEmpty ? LocString("Profile_Unnamed") : profile.alias)
                    .lineLimit(1)
                Spacer()
            }
            .contentShape(Rectangle())
            .onTapGesture {
                appState.selectProfile(id: profile.id)
            }
            .listRowBackground(
                Group {
                    if appState.selectedProfileID == profile.id {
                        Color.accentColor.opacity(0.15)
                    }
                }
            )
            .contextMenu {
                Button(LocString("Dialog_RemoveProfile_Title"), role: .destructive) {
                    profilePendingRemoval = profile
                }
            }
        }
    }
}
