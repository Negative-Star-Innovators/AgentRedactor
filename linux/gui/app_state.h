#pragma once

// Linux mirror of windows/AppState: owns the EngineClient, the engine
// lifecycle (spawn when unreachable, stop/lock on quit) and the 1-second
// /status + /settings poll loop that keeps the UI in sync with CLI changes.
//
// The poll loop runs on a worker thread (the curl client is blocking); all
// notifications arrive as queued signals on the GUI thread. User-initiated
// mutations go through client() directly on the UI thread — ControlApiClient
// is per-request stateless after Connect, and loopback calls return in
// milliseconds.

#include <atomic>
#include <filesystem>
#include <thread>

#include <QObject>
#include <QString>

#include "engine_client.h"

class AppState : public QObject {
    Q_OBJECT
public:
    AppState(std::filesystem::path configDir, QObject* parent = nullptr);
    ~AppState() override;

    // Connects to the engine, spawning it (detached, bare argv = engine mode)
    // when unreachable; waits up to ~30 s for control.json + /status.
    // False = engine could not be started (caller shows an error and exits).
    bool EnsureEngineRunning();

    void StartPolling();
    // protectionEnabled: current masterPasswordEnabled snapshot — when the
    // engine survives the GUI, it is locked behind the password again.
    void Shutdown(bool protectionEnabled);

    bool engineSpawned() const { return engineSpawned_; }
    AgentRedactor::EngineClient& client() { return client_; }
    const std::filesystem::path& configDir() const { return configDir_; }

    // Latest snapshots (GUI thread only; written from the slots below).
    const json& lastStatus() const { return lastStatus_; }
    const json& lastSettings() const { return lastSettings_; }

signals:
    // Every poll tick (stats/matches refresh).
    void statusUpdated();
    // Only when the settings JSON actually changed (full reload trigger).
    void settingsChanged();
    // Model-download fields changed; payload is the status JSON.
    void modelDownloadChanged();
    // The engine stopped answering (post-connect); UI should say so.
    void connectionLost();

private slots:
    void onPolled(QString statusDump, QString settingsDump, bool statusOk);

private:
    void PollThreadMain();

    std::filesystem::path configDir_;
    AgentRedactor::EngineClient client_;
    bool engineSpawned_ = false;

    std::thread pollThread_;
    std::atomic<bool> stopPolling_{false};
    bool shutdownDone_ = false;

    json lastStatus_ = json::object();
    json lastSettings_ = json::object();
    // Worker-thread copies used for diffing before emitting.
    std::string prevSettingsDump_;
    std::string prevModelFields_;
};
