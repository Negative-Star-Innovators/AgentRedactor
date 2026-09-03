#pragma once

// Linux self-update manager — Velopack C/C++ equivalent of
// windows/src/update_manager.cpp. Compiled to real code only in self-release
// builds (AR_SELFRELEASE, mirroring AGENTREDACTOR_SELFRELEASE); otherwise all
// entry points are no-ops and IsSelfRelease() is false, so packaged-by-other-
// means builds carry no update code.
//
// Threading: CheckForUpdates runs the blocking Velopack calls on a worker
// thread and reports back on the GUI thread via signals. The restart prompt
// and ApplyAndRestart run on the GUI thread.

#include <memory>

#include <QObject>
#include <QString>

class AppUpdateManager : public QObject {
    Q_OBJECT
public:
    explicit AppUpdateManager(QObject* parent = nullptr);
    ~AppUpdateManager() override;

    // Compile-time: was this build produced with AR_SELFRELEASE=ON.
    static bool IsSelfRelease();

    // Async. Checks the feed, downloads the update when one exists, then
    // emits updateDownloaded (caller prompts / applies) or noUpdateFound /
    // checkFailed. No-op while a check is already running.
    // userInitiated=true means the user pressed the button: surface errors
    // and the "up to date" result; the startup check stays silent on those.
    void CheckForUpdates(bool userInitiated);

    // GUI thread. Hands the downloaded update to the Velopack updater (which
    // waits for this process to exit, applies, and restarts the app), then
    // emits restartRequested — the caller must gracefully quit immediately.
    void ApplyAndRestart();

signals:
    void updateDownloaded(QString version, bool userInitiated);
    void noUpdateFound(bool userInitiated);
    void checkFailed(QString message, bool userInitiated);
    void restartRequested();

private slots:
    void onWorkFinished(QString version, QString error, bool userInitiated);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
