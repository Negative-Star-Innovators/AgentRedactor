import Foundation

// Single-instance guard for the GUI (linux/gui/main.cpp QLockFile parity).
// The first instance holds an exclusive flock on <configDir>/gui.lock; a
// second launch posts a distributed notification and exits. The notification
// crosses process boundaries without a sandbox (this build is not sandboxed).

enum SingleInstance {
    static let activateNotification = NSNotification.Name("com.negativestarinnovators.agentredactor.activate")

    /// Returns nil when another instance already holds the lock.
    static func acquireLock(configDir: URL) -> FileHandle? {
        AppPaths.ensureConfigDir()
        let path = configDir.appendingPathComponent("gui.lock").path
        if !FileManager.default.fileExists(atPath: path) {
            FileManager.default.createFile(atPath: path, contents: nil)
        }
        guard let handle = FileHandle(forWritingAtPath: path) else { return nil }
        if flock(handle.fileDescriptor, LOCK_EX | LOCK_NB) != 0 {
            try? handle.close()
            return nil
        }
        return handle
    }

    /// Second-instance path: wake the running instance and exit.
    static func notifyRunningInstanceAndExit() -> Never {
        DistributedNotificationCenter.default().postNotificationName(
            activateNotification, object: nil, deliverImmediately: true)
        // Give the notification a moment to be delivered before we vanish.
        Thread.sleep(forTimeInterval: 0.3)
        exit(0)
    }
}
