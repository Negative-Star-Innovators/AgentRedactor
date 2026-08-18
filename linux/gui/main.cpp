// Linux GUI entry point. Thin shell: constructs the Qt app, ensures the
// engine is running (spawning it detached when not), and shows the main
// window / tray. All backend logic lives in the engine process.

#include <csignal>

#include <QApplication>
#include <QMessageBox>
#include <QSocketNotifier>

#include <sys/socket.h>
#include <unistd.h>

#include "app_state.h"
#include "main_window.h"
#include "tray_icon.h"
#include "translator_loader.h"
#include "utils.h"

namespace {

// Self-pipe SIGTERM/SIGINT bridge: the handler only writes a byte; the
// QSocketNotifier turns it into a graceful QApplication::quit on the GUI
// thread so AppState::Shutdown (engine stop/lock) still runs.
int g_signalFds[2] = {-1, -1};

void onSignal(int sig) {
    if (g_signalFds[1] >= 0) {
        const char b = static_cast<char>(sig);
        if (write(g_signalFds[1], &b, 1) < 0) { /* nothing sensible to do */ }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFds) == 0) {
        struct sigaction sa{};
        sa.sa_handler = onSignal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("agentredactor"));
    QApplication::setOrganizationName(QStringLiteral("NegativeStarInnovators"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app.png")));
    // Closing the last window must not quit the app when the tray keeps it
    // alive; MainWindow decides when a close is a real quit.
    QApplication::setQuitOnLastWindowClosed(false);

    QSocketNotifier notifier(g_signalFds[0], QSocketNotifier::Read);
    if (g_signalFds[0] >= 0) {
        notifier.setEnabled(true);
        QObject::connect(&notifier, &QSocketNotifier::activated, &app,
            [&app] { QApplication::quit(); });
    }

    const bool trayOnly = QApplication::arguments().contains(QLatin1String("--tray-only"));

    TranslatorLoader translator(app);

    AppState appState(AgentRedactor::Utils::GetAppDataPath());
    if (!appState.EnsureEngineRunning()) {
        QMessageBox::critical(nullptr, QStringLiteral("Agent Redactor"),
            QStringLiteral("The Agent Redactor engine could not be started."));
        return 1;
    }

    TrayIcon tray;
    MainWindow window(&appState, &tray, &translator, trayOnly);
    tray.showIcon();
    QObject::connect(&tray, &TrayIcon::openRequested, &window, &MainWindow::openWindow);
    QObject::connect(&tray, &TrayIcon::quitRequested, &window, &MainWindow::onQuitRequested);
    QObject::connect(&tray, &TrayIcon::startOnBootToggled, &window, &MainWindow::onStartOnBootToggled);

    appState.StartPolling();

    const int rc = QApplication::exec();
    if (g_signalFds[0] >= 0) { close(g_signalFds[0]); close(g_signalFds[1]); }
    return rc;
}
