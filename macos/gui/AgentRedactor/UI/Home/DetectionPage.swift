import SwiftUI

/// AI Detection section: the "AI Powered Detection Model" card for the
/// selected profile (per-profile toggle, confidence threshold, PII type
/// grid). Card sections of the unified page — no own scroll/width/padding.
struct DetectionPage: View {
    @EnvironmentObject var appState: AppState

    var body: some View {
        VStack(spacing: 16) {
            if appState.selectedProfile != nil {
                DetectionCard()
            } else {
                SelectProfileHint()
            }
        }
    }
}
