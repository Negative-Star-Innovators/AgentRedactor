import SwiftUI

// Card container used across all pages (the Windows/Linux GUIs are
// card-based: titled groups with separated content).
struct Card<Content: View>: View {
    let titleKey: String?
    @ViewBuilder let content: Content

    init(_ titleKey: String? = nil, @ViewBuilder content: () -> Content) {
        self.titleKey = titleKey
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if let titleKey {
                LocText(titleKey)
                    .font(.headline)
            }
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(16)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(Color(nsColor: .controlBackgroundColor))
                .shadow(color: .black.opacity(0.08), radius: 2, y: 1)
        )
    }
}

// Dimmed help text (Linux makeHint equivalent).
struct HintText: View {
    let key: String

    var body: some View {
        LocText(key)
            .font(.callout)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
    }
}

// Surfaces control-API/server errors in an alert; keeps the last message.
struct ErrorAlert: Identifiable {
    var id = UUID()
    var titleKey: String
    var message: String
}

extension Error {
    /// Extracts a user-presentable message from a facade error chain.
    var surfacedMessage: String {
        if let clientError = self as? EngineClientError {
            switch clientError {
            case .serverError(_, let message):
                return message ?? localizedDescription
            default:
                return localizedDescription
            }
        }
        return localizedDescription
    }
}
