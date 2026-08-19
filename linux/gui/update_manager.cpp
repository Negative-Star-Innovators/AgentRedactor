#include "update_manager.h"

#include <QCoreApplication>
#include <QtDebug>

#ifdef AR_SELFRELEASE

#include <Velopack.hpp>

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <thread>

// The Cloudflare worker serves every file under this prefix from the R2
// releases bucket. The host is shared with Windows; the channel segment for
// Linux x64 builds is "linux" (matches vpk pack -c in build-release.sh).
namespace {
constexpr const char* kUpdateFeedUrl =
    "https://api.agentredactor.negativestarinnovators.com/updates/linux";

// Test hook (self-release builds only): AGENTREDACTOR_UPDATE_FEED overrides
// the update feed URL so the E2E tests can point at a local feed. Loopback
// only — honoring an arbitrary remote URL would let anyone who can launch
// the app with a custom environment steer updates to an untrusted server.
// Same contract as the Windows build.
std::string GetUpdateFeedUrl() {
    if (const char* overrideUrl = std::getenv("AGENTREDACTOR_UPDATE_FEED");
        overrideUrl && *overrideUrl) {
        std::string url = overrideUrl;
        for (auto& c : url) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (url.rfind("http://127.0.0.1", 0) == 0 || url.rfind("http://localhost", 0) == 0) {
            return overrideUrl;
        }
        qWarning("[UpdateManager] Ignoring AGENTREDACTOR_UPDATE_FEED (loopback URLs only): %s",
            overrideUrl);
    }
    return kUpdateFeedUrl;
}

// Test hook (self-release builds only): AGENTREDACTOR_UPDATE_AUTOAPPLY=1
// skips the restart prompt and applies immediately. Unset in normal use.
bool AutoApplyEnabled() {
    const char* value = std::getenv("AGENTREDACTOR_UPDATE_AUTOAPPLY");
    return value && std::string(value) == "1";
}
} // namespace

struct AppUpdateManager::Impl {
    std::thread worker;
    std::atomic<bool> busy{false};
    // Set on the worker after a successful download; consumed by
    // ApplyAndRestart on the GUI thread.
    std::optional<Velopack::UpdateInfo> pending;
};

AppUpdateManager::AppUpdateManager(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {}

AppUpdateManager::~AppUpdateManager() {
    if (impl_->worker.joinable()) impl_->worker.join();
}

bool AppUpdateManager::IsSelfRelease() { return true; }

void AppUpdateManager::CheckForUpdates(bool userInitiated) {
    bool expected = false;
    if (!impl_->busy.compare_exchange_strong(expected, true)) return;
    if (impl_->worker.joinable()) impl_->worker.join();

    impl_->worker = std::thread([this, userInitiated] {
        QString version, error;
        try {
            Velopack::UpdateManager manager(GetUpdateFeedUrl());
            auto update = manager.CheckForUpdates();
            if (update.has_value()) {
                manager.DownloadUpdates(update.value());
                version = QString::fromStdString(update->TargetFullRelease.Version);
                impl_->pending = std::move(update.value());
            }
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        }
        impl_->busy = false;
        QMetaObject::invokeMethod(this, "onWorkFinished", Qt::QueuedConnection,
            Q_ARG(QString, version), Q_ARG(QString, error),
            Q_ARG(bool, userInitiated));
    });
}

void AppUpdateManager::onWorkFinished(QString version, QString error, bool userInitiated) {
    if (!error.isEmpty()) {
        qWarning("[UpdateManager] update check failed: %s", qUtf8Printable(error));
        emit checkFailed(error, userInitiated);
    } else if (version.isEmpty()) {
        emit noUpdateFound(userInitiated);
    } else if (AutoApplyEnabled()) {
        qInfo("[UpdateManager] AGENTREDACTOR_UPDATE_AUTOAPPLY=1; applying without prompting");
        ApplyAndRestart();
    } else {
        emit updateDownloaded(version, userInitiated);
    }
}

void AppUpdateManager::ApplyAndRestart() {
    if (!impl_->pending.has_value()) return;
    try {
        Velopack::UpdateManager manager(GetUpdateFeedUrl());
        manager.WaitExitThenApplyUpdates(impl_->pending.value());
    } catch (const std::exception& e) {
        qWarning("[UpdateManager] apply failed: %s", e.what());
        emit checkFailed(QString::fromUtf8(e.what()), true);
        return;
    }
    emit restartRequested();
}

#else // !AR_SELFRELEASE — no-op stubs (mirrors the Windows Store/MSIX stubs)

struct AppUpdateManager::Impl {};

AppUpdateManager::AppUpdateManager(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {}
AppUpdateManager::~AppUpdateManager() = default;
bool AppUpdateManager::IsSelfRelease() { return false; }
void AppUpdateManager::CheckForUpdates(bool) {}
void AppUpdateManager::onWorkFinished(QString, QString, bool) {}
void AppUpdateManager::ApplyAndRestart() {}

#endif
