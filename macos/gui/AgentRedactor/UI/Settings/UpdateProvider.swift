import Foundation

struct UpdateReleaseInfo: Equatable, Identifiable {
    var id: String { version }
    var version: String
    /// Release notes from the feed (markdown), when the release carries them.
    var notesMarkdown: String?
}

enum UpdateCheckResult: Equatable {
    case upToDate
    case updateReady(UpdateReleaseInfo)
}

enum UpdateProviderError: Error, LocalizedError {
    case feedUnreadable
    case applyUnavailable
    case updateCheckBusy

    var errorDescription: String? {
        switch self {
        case .feedUnreadable: return "Couldn't check for updates. Try again later."
        case .applyUnavailable: return "Restart-to-apply is not available in this build yet."
        case .updateCheckBusy: return "An update check is already running."
        }
    }
}

/// The Velopack-compatible feed and its test hook, shared by the real
/// (VelopackUpdateProvider) and fallback (VelopackFeedUpdateProvider) update
/// providers.
enum UpdateFeed {
    /// Cloudflare worker serving the R2 releases bucket, osx-arm64 channel
    /// (arch-aware, same split as the Windows win/win-arm64 channels).
    static let defaultBaseURL = "https://api.agentredactor.negativestarinnovators.com/updates/osx-arm64"
    static let channel = "osx-arm64"

    /// Test hook (self-release parity): AGENTREDACTOR_UPDATE_FEED overrides
    /// the update feed URL so E2E tests can point at a local feed. Loopback
    /// only — honoring an arbitrary remote URL would let anyone who can
    /// launch the app with a custom environment steer updates to an
    /// untrusted server. Same contract as the Windows/Linux builds.
    static var resolvedBaseURL: String {
        let env = ProcessInfo.processInfo.environment["AGENTREDACTOR_UPDATE_FEED"] ?? ""
        let lowered = env.lowercased()
        if lowered.hasPrefix("http://127.0.0.1") || lowered.hasPrefix("http://localhost") {
            return env
        }
        if !env.isEmpty {
            NSLog("[UpdateManager] Ignoring AGENTREDACTOR_UPDATE_FEED (loopback URLs only): %@", env)
        }
        return defaultBaseURL
    }

    /// Case-insensitive dictionary lookup: vpk writes the feed with
    /// PascalCase keys ("Assets", "Version") while the Rust deserializer is
    /// case-insensitive, so accept any casing here too.
    static func ciValue(_ dict: [String: Any], _ key: String) -> Any? {
        if let value = dict[key] { return value }
        let lowered = key.lowercased()
        return dict.first { $0.key.lowercased() == lowered }?.value
    }
}

/// Update-checking behind a protocol so self-release builds can swap in the
/// real Velopack provider (VelopackUpdateProvider, compiled in only with
/// SELFRELEASE) while store/sandbox/dev builds keep the feed-check stub.
protocol UpdateProvider {
    func checkForUpdates() async -> Result<UpdateCheckResult, Error>
    func applyStagedUpdate() async -> Result<Void, Error>
}

/// Fetches releases.<channel>.json from the Velopack feed and compares
/// SemVer versions with the running app. Fallback provider used when the
/// updater is compiled out (SELFRELEASE) or the Velopack dylib did not load;
/// downloading/applying is stubbed — self-release builds use
/// VelopackUpdateProvider instead.
final class VelopackFeedUpdateProvider: UpdateProvider {
    let feedBaseURL: String
    let channel: String
    private let session = URLSession(configuration: .ephemeral)

    init() {
        feedBaseURL = UpdateFeed.resolvedBaseURL
        channel = UpdateFeed.channel
    }

    var currentVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0.0.0"
    }

    func checkForUpdates() async -> Result<UpdateCheckResult, Error> {
        let urlString = "\(feedBaseURL)/releases.\(channel).json"
        guard let url = URL(string: urlString) else { return .failure(UpdateProviderError.feedUnreadable) }
        do {
            let (data, response) = try await session.data(from: url)
            guard let http = response as? HTTPURLResponse, http.statusCode == 200 else {
                return .failure(UpdateProviderError.feedUnreadable)
            }
            guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let assets = UpdateFeed.ciValue(json, "assets") as? [[String: Any]] else {
                return .failure(UpdateProviderError.feedUnreadable)
            }
            // Newest valid release wins; keep its release notes for the prompt.
            var latest: (version: String, notes: String?)?
            for entry in assets {
                guard let version = UpdateFeed.ciValue(entry, "version") as? String,
                      Self.isValidVersion(version) else { continue }
                if latest == nil || Self.compareVersions(version, latest!.version) == .orderedDescending {
                    let notes = UpdateFeed.ciValue(entry, "notesMarkdown") as? String
                    latest = (version, notes?.isEmpty == true ? nil : notes)
                }
            }
            guard let latest, Self.compareVersions(latest.version, currentVersion) == .orderedDescending else {
                return .success(.upToDate)
            }
            return .success(.updateReady(UpdateReleaseInfo(version: latest.version,
                                                           notesMarkdown: latest.notes)))
        } catch {
            return .failure(error)
        }
    }

    /// Stub: only VelopackUpdateProvider (self-release builds) can stage and
    /// apply updates; report unavailable so the UI says so honestly.
    func applyStagedUpdate() async -> Result<Void, Error> {
        .failure(UpdateProviderError.applyUnavailable)
    }

    // MARK: - Version helpers (Velopack uses strict SemVer: 1.2.3[-pre])

    static func isValidVersion(_ version: String) -> Bool {
        let parts = version.split(separator: "-")[0].split(separator: ".")
        return parts.count == 3 && parts.allSatisfy { $0.allSatisfy(\.isNumber) }
    }

    static func compareVersions(_ a: String, _ b: String) -> ComparisonResult {
        let aCore = String(a.split(separator: "-")[0])
        let bCore = String(b.split(separator: "-")[0])
        let aParts = aCore.split(separator: ".").compactMap { Int($0) }
        let bParts = bCore.split(separator: ".").compactMap { Int($0) }
        for i in 0..<max(aParts.count, bParts.count) {
            let av = i < aParts.count ? aParts[i] : 0
            let bv = i < bParts.count ? bParts[i] : 0
            if av != bv { return av < bv ? .orderedAscending : .orderedDescending }
        }
        // Pre-release versions sort before the release of the same core.
        let aPre = a.contains("-")
        let bPre = b.contains("-")
        if aPre != bPre { return aPre ? .orderedAscending : .orderedDescending }
        return .orderedSame
    }
}
