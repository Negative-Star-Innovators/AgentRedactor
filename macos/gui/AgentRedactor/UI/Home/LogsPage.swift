import SwiftUI

/// Logs page: enable logging, show-sensitive (verbose) toggle, open log
/// file/folder, and delete-all-logs. Logging is engine-wide (global on
/// Windows/Linux too), so this page is not bound to a profile.
struct LogsPage: View {
    var body: some View {
        VStack(spacing: 16) {
            LogsCard()
        }
    }
}
