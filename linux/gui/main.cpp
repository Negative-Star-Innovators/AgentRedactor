// Linux GUI entry point. Thin shell: constructs the Qt app, ensures the
// engine is running (spawning it detached when not), and shows the main
// window / tray. All backend logic lives in the engine process.

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <QApplication>
#include <QMessageBox>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <sys/socket.h>
#include <unistd.h>

#include "app_state.h"
#include "desktop_integration.h"
#include "main_window.h"
#include "tray_icon.h"
#include "translator_loader.h"
#include "utils.h"

#ifdef AR_SELFRELEASE
#include <Velopack.hpp>
#endif

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

// CLI pass-through: "<gui> --cli <args...>" re-execs the sibling dual-mode
// engine/CLI binary with the remaining args. This is how the
// ~/.local/bin/agentredactor wrapper reaches the CLI inside an AppImage: the
// wrapper re-launches the AppImage file with --cli, the AppImage runtime
// mounts and starts this binary, and we hand off to the real CLI. Runs before
// Velopack/Qt startup so CLI calls stay fast and never parse GUI flags.
int ForwardToCli(int argc, char* argv[]) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    const fs::path cliBin = ec ? fs::path() : self.parent_path() / "agentredactor";

    std::vector<std::string> args;
    args.push_back(cliBin.string());
    for (int i = 2; i < argc; ++i) args.emplace_back(argv[i]);
    std::vector<char*> cargv;
    for (auto& a : args) cargv.push_back(a.data());
    cargv.push_back(nullptr);

    execv(cliBin.c_str(), cargv.data());
    std::fprintf(stderr, "agentredactor: could not launch the bundled CLI (%s)\n",
        std::strerror(errno));
    return 1;
}

// Shell-quote a path for embedding in a double-quoted wrapper script.
std::string ShellQuoteDouble(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') out += '\\';
        out += c;
    }
    return out;
}

// Terminal discoverability: expose the CLI as ~/.local/bin/agentredactor.
// Best-effort and idempotent.
//  - Installed/dev layout (engine binary next to the GUI): plain symlink to
//    the dual-mode binary.
//  - AppImage: the CLI binary lives inside the ephemeral /tmp/.mount_* so a
//    symlink cannot reach it; instead drop a two-line wrapper that re-runs
//    the AppImage file ($APPIMAGE is the stable path) with --cli. Rewritten
//    on every launch so moving the AppImage self-heals on the next run.
void EnsureCliShim() {
    namespace fs = std::filesystem;
    const fs::path binDir =
        fs::path(QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdString())
        / ".local" / "bin";
    const fs::path link = binDir / "agentredactor";

    const char* appImageEnv = std::getenv("APPIMAGE");
    const bool appImageRun = (appImageEnv && *appImageEnv) ||
        QCoreApplication::applicationDirPath().startsWith(QLatin1String("/tmp/.mount_"));
    if (appImageRun) {
        // Clean up a dangling symlink left by an earlier AppImage run.
        std::error_code ec;
        if (fs::is_symlink(link, ec) &&
            fs::read_symlink(link, ec).string().rfind("/tmp/.mount_", 0) == 0) {
            fs::remove(link, ec);
        }
        if (!appImageEnv || !*appImageEnv) return; // extract-and-run: no stable path

        std::error_code ec2;
        fs::create_directories(binDir, ec2);
        const std::string script = "#!/bin/sh\nexec \"" +
            ShellQuoteDouble(appImageEnv) + "\" --cli \"$@\"\n";
        bool upToDate = false;
        {
            std::ifstream in(link, std::ios::binary);
            if (in) upToDate = std::string(std::istreambuf_iterator<char>(in),
                std::istreambuf_iterator<char>()) == script;
        }
        if (!upToDate) {
            // Not atomic, but a torn half-written wrapper just fails its next
            // exec and is rewritten on the following app launch.
            std::ofstream out(link, std::ios::binary | std::ios::trunc);
            out << script;
            out.close();
            fs::permissions(link, fs::perms::owner_all | fs::perms::group_read |
                fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec,
                fs::perm_options::replace, ec2);
            if (ec2) qWarning("[main] could not write CLI wrapper %s: %s",
                link.c_str(), ec2.message().c_str());
        }
        return;
    }

    const fs::path engine =
        fs::path(QCoreApplication::applicationDirPath().toStdString()) / "agentredactor";
    if (!fs::exists(engine)) return;
    std::error_code ec;
    if (fs::exists(link, ec) || fs::is_symlink(link, ec)) {
        if (fs::read_symlink(link, ec) == engine) return; // already correct
        fs::remove(link, ec);
    }
    fs::create_directories(binDir, ec);
    fs::create_symlink(engine, link, ec);
    if (ec) qWarning("[main] could not create CLI symlink %s: %s",
        link.c_str(), ec.message().c_str());
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc > 1 && std::strcmp(argv[1], "--cli") == 0) {
        return ForwardToCli(argc, argv);
    }
#ifdef AR_SELFRELEASE
    // Velopack startup logic: handles post-update restart/apply hooks and may
    // exit or restart the process. Must run before anything else.
    Velopack::VelopackApp::Build().Run();
#endif

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
    // Wayland ignores setWindowIcon: the dock icon comes from the desktop
    // entry whose name matches the app-id, so this must equal the
    // agentredactor.desktop installed by DesktopIntegration::EnsureInstalled.
    QGuiApplication::setDesktopFileName(QStringLiteral("agentredactor"));
    DesktopIntegration::EnsureInstalled();
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

    EnsureCliShim();

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
