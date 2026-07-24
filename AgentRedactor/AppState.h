#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_set>
#include <functional>
#include <mutex>
#include <windows.h>
#include "settings_manager.h"
#include "pii_detector.h"
#include "system_tray.h"
#include "log_manager.h"
#include "proxy_engine.h"
#include "http_server.h"

namespace AgentRedactor { class AppState; }
extern ::AgentRedactor::AppState* g_appState;

namespace AgentRedactor {

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

    // Called from background threads; posts to message window
    void NotifyLogAdded();
    void NotifyStatsUpdated();

    // Services
    SettingsManager* Settings() { return settings_.get(); }
    ProxyEngine* Proxy() { return proxyEngine_.get(); }
    LogManager* Logs() { return logManager_.get(); }
    SystemTray* Tray() { return systemTray_.get(); }

    // Proxy management
    void StartProxyServers();
    void StopProxyServers();
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

    HttpResponse HandleProxyRequest(int port, const std::string& method, const std::wstring& path,
        const std::unordered_map<std::wstring, std::wstring>& headers, const std::string& body);

    std::unique_ptr<SettingsManager> settings_;
    std::unique_ptr<PIIDetector> detector_;
    std::unique_ptr<LogManager> logManager_;
    std::unique_ptr<ProxyEngine> proxyEngine_;
    std::unique_ptr<SystemTray> systemTray_;
    std::vector<std::unique_ptr<HttpServer>> servers_;

    HWND messageHwnd_ = nullptr;
    HWND mainHwnd_ = nullptr;

    std::unordered_set<int> runningPorts_;

    std::function<void()> onLogAdded_;
    std::function<void()> onStatsUpdated_;
    std::function<void()> onMainWindowClose_;
    std::function<void()> localizationReloadCallback_;
    bool restarting_ = false;
    std::mutex callbackMutex_;
};

} // namespace AgentRedactor
