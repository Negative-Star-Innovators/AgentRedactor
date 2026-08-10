#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <windows.h>
#include "system_tray.h"
#include "log_manager.h"
#include "EngineClient.h"

namespace AgentRedactor { class AppState; }
extern ::AgentRedactor::AppState* g_appState;

namespace AgentRedactor {

// Thin GUI-side state holder. The heavy services (settings, PII detection,
// proxy data plane) live in the engine process (agentredactor.exe); AppState
// owns the EngineClient used to reach them, the system tray, the hidden
// message window, and the engine lifecycle (spawn on startup, stop on quit
// when this GUI started it). Pages keep calling the same accessors — the
// facades returned by Settings()/Logs()/Proxy() mirror the old in-process
// interfaces and forward to the engine over the localhost control API.
class AppState {
public:
    static AppState* Instance();

    AppState();
    ~AppState();

    bool Initialize(const std::filesystem::path& dataDir = L"");
    void Shutdown();

    void SetMainWindow(HWND hwnd) { mainHwnd_ = hwnd; }
    HWND MainWindow() const { return mainHwnd_; }

    // UI callbacks (called on UI thread)
    void SetOnLogAdded(std::function<void()> cb);
    void SetOnStatsUpdated(std::function<void()> cb);

    // Blocking first-run model download. The state machine lives in the
    // engine; these accessors read the /status snapshot refreshed by the
    // 1-second poll thread, and Start/Retry forward to the engine.
    void StartModelDownloadIfNeeded();
    void RetryModelDownload();
    bool IsModelDownloadRequired() const;
    bool IsModelDownloadInProgress() const;
    bool HasModelDownloadFailed() const;
    std::wstring ModelDownloadStatus() const;
    int ModelDownloadPercent() const; // -1 = indeterminate
    void SetOnModelDownloadStatus(std::function<void()> cb);
    void NotifyModelDownloadStatus();

    // Called from the poll thread; posts to message window
    void NotifyLogAdded();
    void NotifyStatsUpdated();

    // Services (engine-backed facades; tray remains in-process)
    SettingsFacade* Settings() { return &settingsFacade_; }
    LogsFacade* Logs() { return &logsFacade_; }
    ProxyFacade* Proxy() { return &proxyFacade_; }
    SystemTray* Tray() { return systemTray_.get(); }

    // Proxy management (engine-side; forwarded over the control API)
    void RestartProxyServers();
    bool IsProxyRunning(int port) const;
    std::vector<int> GetRunningPorts() const;

    // Tray actions
    void ShowTrayMenu();
    void OpenWindow();
    void SetLanguage(const std::wstring& language);
    void ToggleStartOnBoot();
    void Quit();
    void Restart();
    void SetMainWindowCloseCallback(std::function<void()> cb) { onMainWindowClose_ = std::move(cb); }
    void SetLocalizationReloadCallback(std::function<void()> cb) { localizationReloadCallback_ = std::move(cb); }
    bool IsRestarting() const { return restarting_; }

private:
    HWND CreateMessageWindow();
    static LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Engine lifecycle: connect to a running engine via control.json, spawn
    // agentredactor.exe hidden when unreachable, and wait for /status.
    bool EnsureEngineRunning(const std::filesystem::path& configDir);
    bool SpawnEngine();
    void StatusPollLoop();

    EngineClient engineClient_;
    LogManager logManager_; // GUI-local file-logging mirror for LogsFacade
    SettingsFacade settingsFacade_{ &engineClient_ };
    LogsFacade logsFacade_{ &engineClient_, &logManager_ };
    ProxyFacade proxyFacade_{ &engineClient_ };
    std::unique_ptr<SystemTray> systemTray_;

    HWND messageHwnd_ = nullptr;
    HWND mainHwnd_ = nullptr;

    std::function<void()> onLogAdded_;
    std::function<void()> onStatsUpdated_;
    std::function<void()> onMainWindowClose_;
    std::function<void()> localizationReloadCallback_;
    std::function<void()> onModelDownloadStatus_;
    bool restarting_ = false;
    mutable std::mutex callbackMutex_;

    // Last /status snapshot from the engine (guarded by statusMutex_)
    mutable std::mutex statusMutex_;
    json statusCache_;
    bool statusValid_ = false;

    std::thread pollThread_;
    std::atomic<bool> pollStop_{ false };
    bool engineSpawned_ = false;
};

} // namespace AgentRedactor
