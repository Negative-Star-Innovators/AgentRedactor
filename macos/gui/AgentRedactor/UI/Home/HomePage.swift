import SwiftUI

/// The unified main page (Windows HomePage / Linux MainWindow parity): every
/// card belongs to the profile selected in the sidebar, and the global cards
/// (logs, settings) follow below. Selection and content are always on screen
/// together, so the per-profile binding is self-evident.
struct HomePage: View {
    @EnvironmentObject var appState: AppState

    var body: some View {
        VStack(spacing: 16) {
            QuickStartCard()
            if appState.selectedProfile != nil {
                ProfileEditorCard(profile: appState.selectedProfile!)
                DetectionPage()
                RegexPage()
                KeywordsPage()
                StatisticsPage()
            } else if !appState.profiles.isEmpty {
                SelectProfileHint()
            }
            LogsPage()
            SettingsPage()
        }
    }
}

struct QuickStartCard: View {
    var body: some View {
        Card("HomePage_QuickStart_Title.Text") {
            VStack(alignment: .leading, spacing: 8) {
                HintText(key: "HomePage_QuickStart_Step1.Text")
                HintText(key: "HomePage_QuickStart_Step2.Text")
                HintText(key: "HomePage_QuickStart_Step3.Text")
            }
        }
    }
}
