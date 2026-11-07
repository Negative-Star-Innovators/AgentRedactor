import SwiftUI

/// Placeholder shown on the per-profile pages when no profile exists or none
/// is selected in the sidebar.
struct SelectProfileHint: View {
    var body: some View {
        HintText(key: "MacNav_SelectProfileHint")
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(20)
    }
}
