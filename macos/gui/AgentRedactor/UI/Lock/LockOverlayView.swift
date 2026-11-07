import SwiftUI
import AppKit
import LocalAuthentication

/// Full-window lock overlay (Windows padlock overlay / Linux lock page
/// parity): opaque scrim, padlock, and an Unlock button that runs
/// LocalAuthentication and then the same control-API unlock flow as the Linux
/// typed-password path. A failed/cancelled attempt simply stays on the overlay
/// with Try again — exactly like the other platforms.
struct LockOverlayView: View {
    @EnvironmentObject var appState: AppState
    @State private var authenticating = false
    @State private var showFailure = false

    var body: some View {
        ZStack {
            Color(nsColor: .underPageBackgroundColor)
                .ignoresSafeArea()
            VStack(spacing: 16) {
                Image(systemName: "lock.fill")
                    .font(.system(size: 44))
                    .foregroundStyle(.secondary)
                LocText("MacLock_Title")
                    .font(.title2)
                    .fontWeight(.semibold)
                LocText("MacLock_Subtitle")
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                Button {
                    Task { await unlock() }
                } label: {
                    if authenticating {
                        ProgressView()
                            .controlSize(.small)
                    } else {
                        LocText("Dialog_UnlockButton")
                    }
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .disabled(authenticating)
                if showFailure {
                    LocText("MacLock_AuthenticateFailed")
                        .foregroundStyle(.red)
                        .font(.callout)
                }
            }
            .padding(32)
            .frame(maxWidth: 420)
        }
    }

    private func unlock() async {
        guard !authenticating else { return }
        authenticating = true
        showFailure = false
        let result = await appState.unlockWithLocalAuth()
        authenticating = false
        switch result {
        case .success(let unlock):
            if !unlock.ok {
                showFailure = true
            }
        case .failure(let error):
            // userCancel/systemCancel mean "stay locked, no error message" —
            // the padlock screen itself is the retry UI.
            var gateCanceled = false
            if let gateError = error as? AuthGateError, case .canceled = gateError {
                gateCanceled = true
            }
            let nsError = error as NSError
            let laCanceled = nsError.domain == LAError.errorDomain &&
                (nsError.code == LAError.userCancel.rawValue || nsError.code == LAError.systemCancel.rawValue)
            if !gateCanceled && !laCanceled {
                showFailure = true
            }
        }
    }
}
