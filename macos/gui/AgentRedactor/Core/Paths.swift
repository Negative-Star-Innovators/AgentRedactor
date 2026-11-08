import Foundation

enum AppPaths {
    /// Per-user config dir, identical to the engine's Utils::GetAppDataPath:
    /// ~/Library/Application Support/AgentRedactor, overridable with
    /// AGENTREDACTOR_CONFIG_DIR (test hook used by the test suites).
    static var configDir: URL {
        if let override = ProcessInfo.processInfo.environment["AGENTREDACTOR_CONFIG_DIR"],
           !override.isEmpty {
            return URL(fileURLWithPath: override, isDirectory: true)
        }
        let home = FileManager.default.homeDirectoryForCurrentUser
        return home.appendingPathComponent("Library/Application Support/AgentRedactor", isDirectory: true)
    }

    static var controlFile: URL { configDir.appendingPathComponent("control.json") }
    static var settingsFile: URL { configDir.appendingPathComponent("settings.json") }
    static var logFile: URL { configDir.appendingPathComponent("agent_redactor.log") }
    static var sessionsDir: URL { configDir.appendingPathComponent("sessions", isDirectory: true) }
    static var guiLockFile: URL { configDir.appendingPathComponent("gui.lock") }

    /// The engine binary bundled inside the app by build-app.sh.
    static var bundledEngine: URL? {
        Bundle.main.resourceURL?.appendingPathComponent("agentredactor")
    }

    static func ensureConfigDir() {
        try? FileManager.default.createDirectory(at: configDir, withIntermediateDirectories: true)
    }
}
