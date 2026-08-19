#include "app_state.h"

#include <QCoreApplication>
#include <QProcess>
#include <QtDebug>

#include "autostart.h"
#include "utils.h"

using namespace AgentRedactor;

namespace {

// Locate the engine binary: next to the GUI in installed layouts, in the
// sibling engine/ dir in the dev build tree (linux/build/gui vs engine).
std::filesystem::path FindEngineBinary() {
    const auto appDir = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
    const auto direct = appDir / "agentredactor";
    if (std::filesystem::exists(direct)) return direct;
    const auto sibling = appDir.parent_path() / "engine" / "agentredactor";
    if (std::filesystem::exists(sibling)) return sibling;
    return direct; // let the spawn fail on the canonical name
}

} // namespace

AppState::AppState(std::filesystem::path configDir, QObject* parent)
    : QObject(parent), configDir_(std::move(configDir)) {}

AppState::~AppState() {
    stopPolling_ = true;
    if (pollThread_.joinable()) pollThread_.join();
}

bool AppState::EnsureEngineRunning() {
    if (client_.Connect(configDir_) && client_.Ping()) {
        // After a GUI self-update the still-running engine is the old build
        // (the engine binary lives next to the GUI inside the AppImage, so
        // the updated GUI must respawn it). Restart on version mismatch.
        json status;
        if (client_.GetStatus(status) &&
            status.value("engineVersion", std::string()) == std::string(AR_VERSION_STRING)) {
            return true;
        }
        qInfo("[AppState] engine version mismatch; restarting engine");
        client_.StopEngine();
        for (int i = 0; i < 50; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!client_.Connect(configDir_) || !client_.Ping()) break;
        }
    }

    const auto engine = FindEngineBinary();
    engineSpawned_ = QProcess::startDetached(
        QString::fromStdString(engine.string()), {});

    // The engine loads the ONNX model during startup; allow 30 s.
    for (int i = 0; i < 300 && !stopPolling_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (client_.Connect(configDir_) && client_.Ping()) return true;
    }
    return false;
}

void AppState::StartPolling() {
    stopPolling_ = false;
    pollThread_ = std::thread([this] { PollThreadMain(); });
}

void AppState::Shutdown(bool protectionEnabled) {
    if (shutdownDone_) return;
    shutdownDone_ = true;
    stopPolling_ = true;
    if (pollThread_.joinable()) pollThread_.join();
    if (engineSpawned_) {
        // This GUI started the engine, so it owns its lifetime.
        client_.StopEngine();
    } else if (protectionEnabled) {
        // The engine survives the GUI; lock it so the next open must
        // authenticate again.
        client_.Lock();
    }
}

void AppState::PollThreadMain() {
    while (!stopPolling_) {
        json status, settings;
        const bool statusOk = client_.GetStatus(status);
        bool settingsOk = false;
        if (statusOk) settingsOk = client_.GetSettings(settings);

        QString statusDump, settingsDump;
        if (statusOk) statusDump = QString::fromStdString(status.dump());
        if (settingsOk) settingsDump = QString::fromStdString(settings.dump());

        QMetaObject::invokeMethod(this, "onPolled", Qt::QueuedConnection,
            Q_ARG(QString, statusDump), Q_ARG(QString, settingsDump),
            Q_ARG(bool, statusOk));

        // 1 s cadence in 100 ms slices so Shutdown() is not kept waiting.
        for (int i = 0; i < 10 && !stopPolling_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void AppState::onPolled(QString statusDump, QString settingsDump, bool statusOk) {
    if (!statusOk) {
        emit connectionLost();
        return;
    }

    try {
        const json status = json::parse(statusDump.toStdString());

        // Model-download progress is a separate notification so the blocking
        // first-run dialog can track it.
        std::string modelFields;
        for (const char* k : {"modelDownloadRequired", "modelDownloadInProgress",
                              "modelDownloadFailed", "modelDownloadPercent", "modelDownloadStatus"}) {
            modelFields += status.value(k, json()).dump();
        }
        const bool modelChanged = modelFields != prevModelFields_;
        prevModelFields_ = modelFields;

        lastStatus_ = status;
        emit statusUpdated();
        if (modelChanged) emit modelDownloadChanged();
    } catch (...) {
        return;
    }

    if (!settingsDump.isEmpty() && settingsDump.toStdString() != prevSettingsDump_) {
        prevSettingsDump_ = settingsDump.toStdString();
        try {
            lastSettings_ = json::parse(prevSettingsDump_);
        } catch (...) {
            return;
        }
        // Keep the XDG autostart entry in agreement with the persisted
        // setting (e.g. changed via the CLI while the GUI was closed).
        const bool startOnBoot = lastSettings_.value("startOnBoot", false);
        if (Autostart::IsEnabled() != startOnBoot) {
            Autostart::SetEnabled(startOnBoot,
                QCoreApplication::applicationFilePath().toStdString());
        }
        emit settingsChanged();
    }
}
