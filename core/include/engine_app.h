#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <filesystem>
#include "platform_compat.h"
#include "settings_manager.h"
#include "pii_detector.h"
#include "log_manager.h"
#include "proxy_engine.h"
#include "http_server.h"
#include "control_server.h"

namespace AgentRedactor {

// Engine-side owner of everything the GUI process used to host in-process:
// settings, PII detection, the proxy data plane, and the localhost control
// API. Runs inside agentredactor.exe (console subsystem, hidden window).
class EngineApp {
public:
    EngineApp() = default;
    ~EngineApp();
    EngineApp(const EngineApp&) = delete;
    EngineApp& operator=(const EngineApp&) = delete;

    bool Initialize(const std::filesystem::path& dataDir = L"");
    void Shutdown();

    // Blocks until RequestStop() (via POST /engine/stop).
    void Run();
    void RequestStop();

private:
    // Proxy data plane (moved from the old in-process AppState).
    void StartProxyServers();
    void StopProxyServers();
    void RestartProxyServers();
    bool IsProxyRunning(int port) const;

    HttpResponse HandleProxyRequest(int port, const std::string& method, const std::wstring& path,
        const std::unordered_map<std::wstring, std::wstring>& headers, const std::string& body);

    // Blocking first-run model download (moved from AppState; the GUI drives
    // it through the control API and polls /status for progress).
    void StartModelDownloadIfNeeded();
    bool IsModelDownloadRequired() const;

    // Control API router + endpoints.
    HttpResponse HandleControlRequest(const HttpRequest& request);
    HttpResponse ApiGetStatus();
    HttpResponse ApiGetSettings();
    HttpResponse ApiPutSetting(const std::wstring& key, const std::wstring& query, const std::string& body);
    HttpResponse ApiGetProfiles();
    HttpResponse ApiGetProfileApiKey(const std::wstring& id);
    HttpResponse ApiPostProfile(const std::string& body);
    HttpResponse ApiPutProfile(const std::wstring& id, const std::string& body);
    HttpResponse ApiDeleteProfile(const std::wstring& id);
    HttpResponse ApiGetMatches(const std::wstring& id);
    HttpResponse ApiDeleteMatches(const std::wstring& id);
    HttpResponse ApiUnlockHello(const std::wstring& query);
    HttpResponse ApiUnlock(const std::string& body);
    HttpResponse ApiHelloVerify(const std::wstring& query);
    HttpResponse ApiGetLogs(const std::wstring& profileParam);

    static HttpResponse JsonResponse(int statusCode, const std::string& body);

    std::unique_ptr<SettingsManager> settings_;
    std::unique_ptr<PIIDetector> detector_;
    std::unique_ptr<LogManager> logManager_;
    std::unique_ptr<ProxyEngine> proxyEngine_;
    std::vector<std::unique_ptr<HttpServer>> servers_;
    std::unordered_set<int> runningPorts_;
    ControlServer controlServer_;

    // Stop signal for Run() (a Win32 event handle was used before the core
    // move; a condition variable is portable and behaves identically).
    std::mutex stopMutex_;
    std::condition_variable stopCv_;
    bool stopRequested_ = false;

    // First-run model download state (guarded by stateMutex_)
    mutable std::mutex stateMutex_;
    std::wstring modelDownloadStatus_;
    int modelDownloadPercent_ = -1;
    bool modelDownloadRequired_ = false;
    bool modelDownloadInProgress_ = false;
    bool modelDownloadFailed_ = false;
    bool modelDownloadWaitingToRetry_ = false;

    // Retry signalling: StartModelDownloadIfNeeded() can wake a sleeping
    // download thread so a manual retry happens immediately.
    std::mutex retryMutex_;
    std::condition_variable retryCv_;
    bool retryNowRequested_ = false;

    // Monotonic counter bumped on every profile mutation; exposed via
    // /settings so the GUI poll can detect CLI-side profile changes
    // (aliases, api keys) and refresh without a restart.
    unsigned long long profilesRevision_ = 0;
};

} // namespace AgentRedactor
