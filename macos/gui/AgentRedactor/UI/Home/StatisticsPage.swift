import SwiftUI

/// Statistics section: request/redaction counters and the session redactions
/// (matches) list — both bound to the selected profile. Card sections of the
/// unified page — no own scroll/width/padding.
struct StatisticsPage: View {
    @EnvironmentObject var appState: AppState

    var body: some View {
        VStack(spacing: 16) {
            if appState.selectedProfile != nil {
                StatisticsCard()
                MatchesCard()
            } else {
                SelectProfileHint()
            }
        }
    }
}
