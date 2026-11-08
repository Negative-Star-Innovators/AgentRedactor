import SwiftUI

/// "AI Powered Detection Model" card for the selected profile: the per-profile
/// AI toggle, the confidence threshold slider, and the 4-category/8-type PII
/// toggle grid (DEFAULT_PII_TYPES / PII_CATEGORIES from core constants).
/// Every control autosaves immediately (Linux autosave parity).
struct DetectionCard: View {
    @EnvironmentObject var appState: AppState
    @State private var errorAlert: ErrorAlert?
    @State private var confidence: Double = 0.9
    @State private var confidenceLoadedFor: String?

    private var profile: ApiKeyProfileModel? { appState.selectedProfile }

    var body: some View {
        Card("HomePage_Detection_Title.Text") {
            if let profile {
                VStack(alignment: .leading, spacing: 10) {
                    HintText(key: "HomePage_Detection_Description.Text")

                    Toggle(isOn: Binding(
                        get: { profile.useOpenAIModel },
                        set: { newValue in
                            var updated = profile
                            updated.useOpenAIModel = newValue
                            Task { await apply(updated) }
                        }
                    )) {
                        LocText("HomePage_Detection_Title.Text")
                    }
                    .toggleStyle(.switch)
                    .accessibilityIdentifier("detection.ai-enabled")

                    HintText(key: "HomePage_Detection_SlowWarning.Text")

                    VStack(alignment: .leading, spacing: 4) {
                        LocText("HomePage_Detection_ConfidenceLabel.Text")
                        HStack(spacing: 12) {
                            Slider(
                                value: Binding(
                                    get: { confidence },
                                    set: { confidence = $0 }),
                                in: 0.0...1.0,
                                onEditingChanged: { editing in
                                    // Commit once on release (Linux
                                    // editingFinished parity) — no save spam
                                    // while dragging, and instant local drag
                                    // feedback.
                                    if !editing {
                                        var updated = profile
                                        updated.piiConfidenceThreshold = confidence
                                        Task { await apply(updated) }
                                    }
                                })
                            Text(String(format: "%.2f", confidence))
                                .monospacedDigit()
                                .frame(width: 36)
                        }
                    }
                    .onAppear { syncConfidence(from: profile) }
                    .onChange(of: profile.id) { syncConfidence(from: profile) }
                    .onChange(of: profile.piiConfidenceThreshold) { syncConfidence(from: profile) }

                    ForEach(PII_CATEGORIES, id: \.id) { category in
                        VStack(alignment: .leading, spacing: 4) {
                            LocText(category.labelKey)
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                            LazyVGrid(columns: [GridItem(.adaptive(minimum: 140), alignment: .leading)], spacing: 4) {
                                ForEach(category.types, id: \.self) { type in
                                    Toggle(isOn: Binding(
                                        get: { profile.enabledPIITypes.contains(type) },
                                        set: { enabled in
                                            var updated = profile
                                            if enabled {
                                                if !updated.enabledPIITypes.contains(type) {
                                                    updated.enabledPIITypes.append(type)
                                                }
                                            } else {
                                                updated.enabledPIITypes.removeAll { $0 == type }
                                            }
                                            Task { await apply(updated) }
                                        }
                                    )) {
                                        LocText(piiTypeLabelKey(type))
                                    }
                                    .toggleStyle(.checkbox)
                                }
                            }
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

    private func syncConfidence(from profile: ApiKeyProfileModel) {
        guard confidenceLoadedFor != profile.id else { return }
        confidenceLoadedFor = profile.id
        confidence = profile.piiConfidenceThreshold
    }

    /// Optimistic apply: the local model already flipped (instant feedback);
    /// here we persist and surface failures (with revert, see AppState).
    private func apply(_ profile: ApiKeyProfileModel) async {
        switch await appState.applyProfileChange(profile) {
        case .success:
            break
        case .failure(let error):
            errorAlert = ErrorAlert(titleKey: "Dialog_Error_Title", message: error.surfacedMessage)
        }
    }
}
