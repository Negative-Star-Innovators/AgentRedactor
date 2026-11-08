import SwiftUI

/// Blocking first-run model download sheet (Linux NonDismissibleDialog
/// parity): non-dismissible, progress bar driven by the /status fields, Retry
/// enabled on failure/waiting-to-retry. The sheet re-arms whenever
/// /status reports modelDownloadRequired; the download itself runs in the
/// engine, kicked off here via POST /engine/download-model.
struct ModelDownloadSheet: View {
    @EnvironmentObject var appState: AppState
    @Environment(\.dismiss) private var dismiss

    private var percent: Int { appState.status.modelDownloadPercent }
    private var failed: Bool { appState.status.modelDownloadFailed }
    private var waitingToRetry: Bool { appState.status.modelDownloadWaitingToRetry }
    private var inProgress: Bool { appState.status.modelDownloadInProgress }
    private var statusText: String { appState.status.modelDownloadStatus }

    var body: some View {
        VStack(spacing: 16) {
            LocText("ModelDownload_Title")
                .font(.title3)
                .fontWeight(.semibold)
            if failed {
                LocText("ModelDownload_Failed")
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
            } else if percent >= 0 {
                LocFormatText("MacModelDownload_Progress", String(percent))
                    .multilineTextAlignment(.center)
            } else {
                // Engine reports indeterminate progress (-1): no percentage
                // label, the ProgressView below spins undetermined.
                LocText("MacModelDownload_ProgressIndeterminate")
                    .multilineTextAlignment(.center)
            }
            if !statusText.isEmpty, !inProgress {
                Text(statusText)
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
            ProgressView(value: percent >= 0 ? Double(percent) : nil, total: 100)
                .frame(width: 320)
            HStack(spacing: 16) {
                Button {
                    Task { await appState.retryModelDownload() }
                } label: {
                    LocText("ModelDownload_RetryButton")
                }
                .disabled(!failed && !waitingToRetry)
                .keyboardShortcut(.defaultAction)
            }
        }
        .padding(28)
        .frame(width: 420)
        .onAppear {
            Task { await appState.startModelDownloadIfNeeded() }
        }
    }
}
