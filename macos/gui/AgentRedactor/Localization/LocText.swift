import SwiftUI

// Text backed by the runtime manifest (Loc.swift) so live language switching
// re-renders without a relaunch. Falls back to the String Catalog only when
// the manifest has no entry for the key at all (missing key -> key itself).
struct LocText: View {
    let key: String
    @ObservedObject private var loc = LocalizationManager.shared

    init(_ key: String) {
        self.key = key
    }

    var body: some View {
        Text(loc.localized(key))
    }
}

/// LocText with {0}/{1} placeholder substitution.
struct LocFormatText: View {
    let key: String
    let args: [String]
    @ObservedObject private var loc = LocalizationManager.shared

    init(_ key: String, _ args: String...) {
        self.key = key
        self.args = args
    }

    var body: some View {
        Text(loc.localized(key, args))
    }
}
