import Foundation

/// Installs the bundled engine/CLI binary as the `agentredactor` command.
/// On every launch the app silently installs a user-space command (no
/// prompt): a symlink in ~/.agentredactor/bin plus a PATH export appended to
/// the user's shell rc files (see installUserSpace). That is the mechanism
/// CLI-first tools use (`openclaw` via Homebrew's user-owned bin, `kimi` via
/// ~/.kimi-code/bin + .zshrc) — no root anywhere in the path.
///
/// A Mac App Store (sandboxed) build must exclude this — sandboxed apps
/// cannot write outside their container.
enum CliInstaller {
    /// User-space install location: user-owned, no authorization needed.
    static let userSpaceBinDir = NSHomeDirectory() + "/.agentredactor/bin"
    static let userSpaceLinkPath = userSpaceBinDir + "/agentredactor"
    static let pathLine = "export PATH=\"$HOME/.agentredactor/bin:$PATH\""

    /// Path of the engine binary inside this bundle (Contents/Resources).
    static var bundledEnginePath: String? {
        Bundle.main.resourceURL?.appendingPathComponent("agentredactor").path
    }

    /// True when the user-space command exists and points at THIS build's
    /// engine. Resolves both sides so a rebuild of the same bundle counts.
    static var isInstalled: Bool {
        guard let target = bundledEnginePath,
              let link = try? FileManager.default.destinationOfSymbolicLink(atPath: userSpaceLinkPath) else {
            return false
        }
        return URL(fileURLWithPath: link).standardizedFileURL.path
            == URL(fileURLWithPath: target).standardizedFileURL.path
    }
}

// MARK: - User-space install (silent, no authorization)

extension CliInstaller {
    /// Installs the `agentredactor` command into the user's own bin dir and
    /// makes it visible on PATH by appending a PATH export to the user's
    /// shell rc files — the mechanism CLI-first tools use (Kimi Code's
    /// ~/.kimi-code/bin, version managers, etc.). No password prompt.
    ///
    /// Idempotent and safe to run on EVERY launch: the symlink is refreshed
    /// (self-heals across updates/moves), and the PATH line is appended only
    /// to rc files that don't already mention it. Sandboxed (App Store)
    /// builds cannot write outside their container and must not call this.
    @discardableResult
    static func installUserSpace() -> String? {
        guard let target = bundledEnginePath,
              FileManager.default.isExecutableFile(atPath: target) else {
            return "The engine binary is missing from the app bundle."
        }

        do {
            try FileManager.default.createDirectory(atPath: userSpaceBinDir,
                                                    withIntermediateDirectories: true)
            if FileManager.default.fileExists(atPath: userSpaceLinkPath) {
                try FileManager.default.removeItem(atPath: userSpaceLinkPath)
            }
            try FileManager.default.createSymbolicLink(atPath: userSpaceLinkPath,
                                                       withDestinationPath: target)
        } catch {
            return "Could not create \(userSpaceLinkPath) (\(error.localizedDescription))."
        }

        // PATH export: append to every existing rc file that lacks it; if
        // none exist, create ~/.zshrc (macOS default shell).
        let home = NSHomeDirectory()
        let rcFiles = ["\(home)/.zshrc", "\(home)/.zprofile", "\(home)/.bash_profile"]
        var wrote = false
        for rc in rcFiles where FileManager.default.fileExists(atPath: rc) {
            if ensurePathLine(in: rc) { wrote = true }
        }
        if !wrote {
            // No rc file existed (or all updates failed): create ~/.zshrc.
            let zshrc = rcFiles[0]
            let content = "# Added by Agent Redactor\n\(pathLine)\n"
            if !FileManager.default.fileExists(atPath: zshrc) {
                try? content.write(toFile: zshrc, atomically: true, encoding: .utf8)
            } else if !ensurePathLine(in: zshrc) {
                return "Installed the command but could not update \(zshrc); add '\(pathLine)' manually."
            }
        }
        return nil
    }

    /// Appends the PATH export to an rc file unless it already mentions the
    /// user-space bin dir. Returns true when the file is guaranteed to
    /// contain the line afterwards.
    private static func ensurePathLine(in path: String) -> Bool {
        guard let existing = try? String(contentsOfFile: path, encoding: .utf8) else { return false }
        if existing.contains(".agentredactor/bin") { return true }
        let updated = existing.hasSuffix("\n")
            ? existing + "\n# Added by Agent Redactor\n\(pathLine)\n"
            : existing + "\n\n# Added by Agent Redactor\n\(pathLine)\n"
        return (try? updated.write(toFile: path, atomically: true, encoding: .utf8)) != nil
    }
}
