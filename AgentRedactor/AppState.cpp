#include "pch.h"
#include "AppState.h"
#include "utils.h"
#include "localization.h"
#include "constants.h"
#include "api_key_profile.h"
#include "logging.h"
#include "model_downloader.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <sstream>
#include <deque>
#include <thread>

using json = nlohmann::json;

using namespace AgentRedactor;

namespace {
    constexpr UINT WM_APP_NOTIFY_LOG = WM_APP + 1;
    constexpr UINT WM_APP_NOTIFY_STATS = WM_APP + 2;
    constexpr UINT WM_APP_NOTIFY_MODEL = WM_APP + 3;
    constexpr UINT WM_TRAYICON = WM_USER + 1;
    constexpr wchar_t MSG_WND_CLASS[] = L"AgentRedactorMsgWindow";

    std::wstring PathWithoutQuery(const std::wstring& path) {
        size_t q = path.find(L'?');
        return q == std::wstring::npos ? path : path.substr(0, q);
    }

    std::wstring CanonicalPathSuffix(const std::wstring& path) {
        std::wstring p = PathWithoutQuery(path);
        std::wstring lower = AgentRedactor::Utils::ToLower(p);
        static const std::wstring messagesSuffix = L"/v1/messages";
        static const std::wstring chatSuffix = L"/v1/chat/completions";
        static const std::wstring modelsSuffix = L"/v1/models";
        static const std::wstring bareMessages = L"/messages";
        static const std::wstring bareChat = L"/chat/completions";
        static const std::wstring bareModels = L"/models";
        if (AgentRedactor::Utils::EndsWith(lower, messagesSuffix)) return messagesSuffix;
        if (AgentRedactor::Utils::EndsWith(lower, chatSuffix)) return chatSuffix;
        if (AgentRedactor::Utils::EndsWith(lower, modelsSuffix)) return modelsSuffix;
        if (AgentRedactor::Utils::EndsWith(lower, bareMessages)) return bareMessages;
        if (AgentRedactor::Utils::EndsWith(lower, bareChat)) return bareChat;
        if (AgentRedactor::Utils::EndsWith(lower, bareModels)) return bareModels;
        return lower;
    }

    bool IsAnthropicMessagesRequest(const std::wstring& path, const std::string& method) {
        auto suffix = CanonicalPathSuffix(path);
        return (suffix == L"/v1/messages" || suffix == L"/messages") && method == "POST";
    }

    bool IsOpenAIChatCompletionsRequest(const std::wstring& path, const std::string& method) {
        auto suffix = CanonicalPathSuffix(path);
        return (suffix == L"/v1/chat/completions" || suffix == L"/chat/completions") && method == "POST";
    }

    // Streaming SSE unredaction helpers. We accumulate complete SSE events and
    // emit them once the buffered redactable text contains no incomplete
    // redaction labels. This prevents labels that are split across upstream
    // chunks from leaking to the client.
    struct StreamingSSEUnredactor {
        StreamingSSEUnredactor(AgentRedactor::ProxyEngine* engine,
            const AgentRedactor::RedactionState& state,
            std::function<bool(const std::string&)> emitChunk)
            : engine_(engine), state_(state), emitChunk_(std::move(emitChunk)) {
            auto consider = [&](const std::map<std::wstring, std::wstring>& m) {
                for (const auto& [label, _] : m) {
                    std::string utf8 = AgentRedactor::Utils::WideToUtf8(label);
                    maxLabelLen_ = std::max(maxLabelLen_, utf8.size());
                    for (size_t i = 1; i <= utf8.size(); ++i) {
                        labelPrefixes_.push_back(utf8.substr(0, i));
                    }
                }
            };
            consider(state.piiMap);
            consider(state.regexMap);
            consider(state.keywordMap);
            // Sort longest first so a long prefix matches before its own shorter prefix.
            std::sort(labelPrefixes_.begin(), labelPrefixes_.end(),
                [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
            LOGF(L"[StreamingSSEUnredactor] created, maxLabelLen=%zu, prefixes=%zu, pii=%zu, regex=%zu, keyword=%zu",
                maxLabelLen_, labelPrefixes_.size(), state.piiMap.size(), state.regexMap.size(), state.keywordMap.size());
            for (const auto& [label, original] : state.keywordMap) {
                LOGF(L"[StreamingSSEUnredactor] keyword map: [%s] -> [%s]",
                    label.c_str(), original.c_str());
            }
        }

        bool OnChunk(const char* data, size_t len) {
            rawBuffer_.append(data, len);
            std::string leftover;
            if (!ParseCompleteEvents(rawBuffer_, events_, leftover)) return false;
            rawBuffer_ = std::move(leftover);
            LOGF(L"[StreamingSSEUnredactor] OnChunk: parsed %zu events, %zu leftover bytes", events_.size(), rawBuffer_.size());
            return MaybeProcess();
        }

        bool Flush() {
            LOGF(L"[StreamingSSEUnredactor] Flush: %zu events, %zu leftover bytes", events_.size(), rawBuffer_.size());
            // Any incomplete raw bytes are treated as a final event line, but
            // whitespace-only leftovers (e.g. a stray newline at the end of the
            // stream) must not become an empty ``data:  `` event that breaks
            // strict SSE clients such as OpenCode.
            if (!rawBuffer_.empty()) {
                size_t start = rawBuffer_.find_first_not_of(" \t\r\n");
                if (start != std::string::npos) {
                    events_.push_back(rawBuffer_.substr(start));
                }
                rawBuffer_.clear();
            }
            return ProcessAll();
        }

    private:
        AgentRedactor::ProxyEngine* engine_ = nullptr;
        AgentRedactor::RedactionState state_;
        std::function<bool(const std::string&)> emitChunk_;
        std::vector<std::string> labelPrefixes_;
        size_t maxLabelLen_ = 0;
        std::string rawBuffer_;
        std::deque<std::string> events_;

        static bool ParseCompleteEvents(const std::string& raw,
            std::deque<std::string>& outEvents, std::string& leftover) {
            size_t pos = 0;
            while (pos < raw.size()) {
                size_t end = raw.find("\n\n", pos);
                if (end == std::string::npos) break;
                std::string eventBlock = raw.substr(pos, end - pos);
                pos = end + 2;
                if (eventBlock.empty()) continue;
                std::string eventData;
                std::istringstream blockStream(eventBlock);
                std::string line;
                while (std::getline(blockStream, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.substr(0, 5) == "data:") {
                        size_t start = line.find_first_not_of(" \t", 5);
                        if (start != std::string::npos) {
                            eventData += line.substr(start);
                        }
                    }
                }
                if (!eventData.empty()) {
                    outEvents.push_back(std::move(eventData));
                }
            }
            leftover = raw.substr(pos);
            return true;
        }

        static std::string SerializeEventsVec(const std::vector<std::string>& events) {
            std::string out;
            for (const auto& ev : events) {
                // Skip whitespace-only events; they would serialize as empty
                // ``data:`` lines that strict SSE clients reject.
                if (ev.empty() || ev.find_first_not_of(" \t\r\n") == std::string::npos) {
                    continue;
                }
                if (ev == "[DONE]") {
                    out += "data: [DONE]\n\n";
                } else {
                    out += "data: " + ev + "\n\n";
                }
            }
            return out;
        }

        // Extract the text that may contain redaction labels from an SSE data
        // payload. This is the concatenation of content/reasoning/reasoning_details
        // fields for OpenAI, and text/partial_json fields for Anthropic; labels
        // never appear in the JSON wrapper itself.
        static std::string ExtractRedactableText(const std::string& eventData) {
            try {
                json j = json::parse(eventData);
                std::string text;
                if (j.contains("choices") && j["choices"].is_array()) {
                    for (const auto& choice : j["choices"]) {
                        if (!choice.contains("delta") || !choice["delta"].is_object()) continue;
                        const auto& delta = choice["delta"];
                        if (delta.contains("content") && delta["content"].is_string()) {
                            text += delta["content"].get<std::string>();
                        }
                        if (delta.contains("reasoning") && delta["reasoning"].is_string()) {
                            text += delta["reasoning"].get<std::string>();
                        }
                        if (delta.contains("reasoning_details") && delta["reasoning_details"].is_array()) {
                            for (const auto& rd : delta["reasoning_details"]) {
                                if (rd.contains("text") && rd["text"].is_string()) {
                                    text += rd["text"].get<std::string>();
                                }
                            }
                        }
                    }
                }
                // Anthropic Messages SSE carries deltas in delta.text and
                // delta.partial_json (and occasionally content_block.text).
                std::function<void(const json&)> collectAnthropic = [&](const json& node) {
                    if (node.is_object()) {
                        for (const auto& item : node.items()) {
                            const std::string& key = item.key();
                            const json& value = item.value();
                            if ((key == "text" || key == "partial_json") && value.is_string()) {
                                text += value.get<std::string>();
                            } else {
                                collectAnthropic(value);
                            }
                        }
                    } else if (node.is_array()) {
                        for (const auto& item : node) {
                            collectAnthropic(item);
                        }
                    }
                };
                collectAnthropic(j);
                return text;
            } catch (...) {
                return eventData;
            }
        }

        // Returns true if text ends with any known redaction label prefix.
        bool EndsWithLabelPrefix(const std::string& text) const {
            for (const auto& prefix : labelPrefixes_) {
                if (prefix.size() <= text.size() &&
                    std::memcmp(text.data() + text.size() - prefix.size(), prefix.data(), prefix.size()) == 0) {
                    return true;
                }
            }
            return false;
        }

        // Build the concatenated redactable text for all buffered events.
        std::string BuildFullText() const {
            std::string full;
            for (const auto& ev : events_) {
                full += ExtractRedactableText(ev);
            }
            return full;
        }

        // Returns true if the concatenated text contains a redaction label that
        // has not been completed yet (no closing >> within the maximum label
        // length), or if it ends with a label prefix.
        bool HasIncompleteLabel(const std::string& fullText) const {
            if (fullText.empty()) return false;
            const std::string marker = "<<REDACTED_";
            size_t pos = 0;
            while ((pos = fullText.find(marker, pos)) != std::string::npos) {
                size_t endSearch = pos + marker.size() + maxLabelLen_;
                if (endSearch > fullText.size()) endSearch = fullText.size();
                size_t closePos = fullText.find(">>", pos + marker.size());
                bool hasClose = closePos != std::string::npos && closePos < endSearch;
                if (!hasClose) return true;
                ++pos;
            }
            if (!fullText.ends_with(">>") && EndsWithLabelPrefix(fullText)) {
                return true;
            }
            return false;
        }

        bool MaybeProcess() {
            if (events_.empty() || maxLabelLen_ == 0) return true;

            // If the concatenated redactable text still contains an incomplete
            // redaction label, buffer more before emitting so RebuildSSE can
            // replace the full label when the remainder arrives.
            std::string fullText = BuildFullText();
            if (HasIncompleteLabel(fullText)) {
                LOGF(L"[StreamingSSEUnredactor] MaybeProcess: buffering %zu events, incomplete label present", events_.size());
                return true;
            }

            // All labels in the buffered window are complete; safe to emit.
            std::string sse = SerializeEventsVec({events_.begin(), events_.end()});
            std::string rebuilt = engine_->RebuildSSE(sse, state_);
            LOGF(L"[StreamingSSEUnredactor] MaybeProcess: emitting %zu events (%zu bytes)", events_.size(), rebuilt.size());
            events_.clear();
            return emitChunk_(rebuilt);
        }

        bool ProcessAll() {
            std::string sse = SerializeEventsVec({events_.begin(), events_.end()});
            LOGF(L"[StreamingSSEUnredactor] ProcessAll: rebuilding %zu events (%zu bytes)", events_.size(), sse.size());
            events_.clear();
            std::string rebuilt = engine_->RebuildSSE(sse, state_);
            LOGF(L"[StreamingSSEUnredactor] ProcessAll: rebuilt %zu bytes", rebuilt.size());
            return emitChunk_(rebuilt);
        }
    };
}

::AgentRedactor::AppState* g_appState = nullptr;

AppState* AppState::Instance() {
    return g_appState;
}

AppState::AppState() {
    g_appState = this;
}

AppState::~AppState() {
    Shutdown();
    g_appState = nullptr;
}

bool AppState::Initialize(const std::filesystem::path& dataDir) {
    settings_ = std::make_unique<SettingsManager>(dataDir);
    logManager_ = std::make_unique<LogManager>();

    // Apply any saved language override before any UI resources are loaded.
    ::AgentRedactor::InitializeLocalization();

    // MSIX/Store builds ship the weights next to the exe (identical behavior
    // to before); self-release installs download them on first run into
    // %LOCALAPPDATA%\AgentRedactor\models.
    auto modelDir = ModelDownloader::ResolveModelDir();
    detector_ = std::make_unique<PIIDetector>(modelDir);
    // Set the provider BEFORE initializing so the model loads with the user's
    // chosen execution provider (CPU, GPU/Auto/DirectML/CUDA).
    detector_->SetProvider(settings_->GetOnnxProvider());
    if (!ModelDownloader::HasModelWeights(modelDir)) {
        // Blocking first-run download: the app must not run degraded
        // (regex/keyword-only), so the detector stays uninitialized and the
        // proxy servers are NOT started below. MainWindow shows a modal
        // download dialog; StartModelDownloadIfNeeded() initializes the
        // detector and starts the proxies once the download succeeds.
        {
            std::lock_guard lock(callbackMutex_);
            modelDownloadRequired_ = true;
        }
        LOG_LIFECYCLE(L"[AppState] Model weights missing; startup blocked until the first-run download completes");
    } else if (!detector_->Initialize()) {
        // Weights are present but the model failed to load. This is the same
        // degraded fallback the app has always had (regex/keyword-only); the
        // blocking flow above only covers missing weights.
        LOG_LIFECYCLE(L"[AppState] WARNING: PII Detector failed to initialize although model weights are present.");
    }

    logManager_->SetLoggingEnabled(settings_->IsLoggingEnabled());
    // Sensitive logging is session-only and always starts off.
    logManager_->SetShowSensitive(false);

    proxyEngine_ = std::make_unique<ProxyEngine>(detector_.get(), logManager_.get(), [this]() {
        NotifyStatsUpdated();
    });

    messageHwnd_ = CreateMessageWindow();
    if (!messageHwnd_) {
        LOG_LIFECYCLE(L"[AppState] Failed to create message window");
    }

    systemTray_ = std::make_unique<SystemTray>(messageHwnd_);
    HICON trayIcon = SystemTray::LoadIconFromFile(L"app.ico", 32);
    if (!trayIcon) trayIcon = SystemTray::CreateGradientIcon(32);
    systemTray_->Create(trayIcon, ::AgentRedactor::LocString(L"AppDisplayName").c_str());
    systemTray_->SetOnLeftClick([this]() { OpenWindow(); });
    systemTray_->SetOnRightClick([this]() { ShowTrayMenu(); });

    if (settings_->IsStartOnBoot()) {
        RegisterStartupTask();
    } else {
        UnregisterStartupTask();
    }

    // Never serve traffic while the blocking first-run download is pending:
    // the proxies start only after the download + detector init succeed.
    if (!IsModelDownloadRequired()) {
        StartProxyServers();
    }
    return true;
}

void AppState::Shutdown() {
    LOG(L"=== Agent Redactor Shutdown ===");
    StopProxyServers();
    if (systemTray_) systemTray_->Destroy();
    if (messageHwnd_) {
        DestroyWindow(messageHwnd_);
        messageHwnd_ = nullptr;
    }
}

void AppState::SetOnLogAdded(std::function<void()> cb) {
    std::lock_guard lock(callbackMutex_);
    onLogAdded_ = std::move(cb);
}

void AppState::SetOnStatsUpdated(std::function<void()> cb) {
    std::lock_guard lock(callbackMutex_);
    onStatsUpdated_ = std::move(cb);
}

void AppState::NotifyLogAdded() {
    if (messageHwnd_) {
        PostMessage(messageHwnd_, WM_APP_NOTIFY_LOG, 0, 0);
    }
}

void AppState::NotifyStatsUpdated() {
    if (messageHwnd_) {
        PostMessage(messageHwnd_, WM_APP_NOTIFY_STATS, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// Blocking first-run model download (fires only when the weights are missing,
// e.g. a fresh self-release install; never in current MSIX builds, which ship
// the weights next to the exe). While the download is pending the app is
// blocked: no detector, no proxy servers.
// ---------------------------------------------------------------------------

void AppState::SetOnModelDownloadStatus(std::function<void()> cb) {
    std::lock_guard lock(callbackMutex_);
    onModelDownloadStatus_ = std::move(cb);
}

void AppState::NotifyModelDownloadStatus() {
    if (messageHwnd_) {
        PostMessage(messageHwnd_, WM_APP_NOTIFY_MODEL, 0, 0);
    }
}

bool AppState::IsModelDownloadRequired() const {
    std::lock_guard lock(callbackMutex_);
    return modelDownloadRequired_;
}

bool AppState::IsModelDownloadInProgress() const {
    std::lock_guard lock(callbackMutex_);
    return modelDownloadInProgress_;
}

bool AppState::HasModelDownloadFailed() const {
    std::lock_guard lock(callbackMutex_);
    return modelDownloadFailed_;
}

std::wstring AppState::ModelDownloadStatus() const {
    std::lock_guard lock(callbackMutex_);
    return modelDownloadStatus_;
}

int AppState::ModelDownloadPercent() const {
    std::lock_guard lock(callbackMutex_);
    return modelDownloadPercent_;
}

void AppState::RetryModelDownload() {
    StartModelDownloadIfNeeded();
}

void AppState::StartModelDownloadIfNeeded() {
    {
        std::lock_guard lock(callbackMutex_);
        if (modelDownloadInProgress_) return;
        modelDownloadInProgress_ = true;
        modelDownloadFailed_ = false;
        modelDownloadPercent_ = -1;
        modelDownloadStatus_.clear();
    }
    NotifyModelDownloadStatus();

    const auto fallbackDir = ModelDownloader::GetFallbackModelDir();
    std::thread([this, fallbackDir]() {
        auto progress = [this](int percent, const std::wstring& message) {
            {
                std::lock_guard lock(callbackMutex_);
                modelDownloadPercent_ = percent;
                if (!message.empty()) modelDownloadStatus_ = message;
            }
            NotifyModelDownloadStatus();
        };

        bool ok = false;
        try {
            ok = ModelDownloader::EnsureModelFiles(fallbackDir, progress);
        } catch (...) {
            ok = false;
        }

        if (ok) {
            // Load the model now that the files exist. Stop the proxy first so
            // no in-flight request can touch the detector while it initializes
            // (on the blocking first-run path the proxies were never started,
            // so Stop is a no-op and Start below unblocks the app).
            StopProxyServers();
            bool initOk = false;
            try {
                if (detector_) initOk = detector_->Initialize();
            } catch (...) {
                initOk = false;
            }
            if (initOk) {
                StartProxyServers();
            }
            LOG_LIFECYCLE(initOk
                ? L"[AppState] Model downloaded and initialized"
                : L"[AppState] Model downloaded but detector initialization failed");
            ok = initOk;
        } else {
            LOG_LIFECYCLE(L"[AppState] Model download failed");
        }

        {
            std::lock_guard lock(callbackMutex_);
            modelDownloadInProgress_ = false;
            modelDownloadFailed_ = !ok;
            modelDownloadPercent_ = ok ? 100 : -1;
            if (ok) modelDownloadRequired_ = false;
        }
        NotifyModelDownloadStatus();
    }).detach();
}

HWND AppState::CreateMessageWindow() {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = MessageWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = MSG_WND_CLASS;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, MSG_WND_CLASS, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, this);
    return hwnd;
}

LRESULT CALLBACK AppState::MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* app = reinterpret_cast<AppState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!app) return DefWindowProc(hwnd, msg, wParam, lParam);

    if (msg == WM_APP_NOTIFY_LOG) {
        std::lock_guard lock(app->callbackMutex_);
        if (app->onLogAdded_) app->onLogAdded_();
        return 0;
    }
    if (msg == WM_APP_NOTIFY_STATS) {
        std::lock_guard lock(app->callbackMutex_);
        if (app->onStatsUpdated_) app->onStatsUpdated_();
        return 0;
    }
    if (msg == WM_APP_NOTIFY_MODEL) {
        std::lock_guard lock(app->callbackMutex_);
        if (app->onModelDownloadStatus_) app->onModelDownloadStatus_();
        return 0;
    }
    if (msg == WM_TRAYICON) {
        if (app->systemTray_) {
            app->systemTray_->HandleTrayMessage(lParam);
        }
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void AppState::StartProxyServers() {
    StopProxyServers();
    auto profiles = settings_->GetProfiles();
    for (const auto& profile : profiles) {
        if (!profile.enabled) continue;
        int port = profile.port;
        auto server = std::make_unique<HttpServer>();
        server->SetLogManager(logManager_.get());
        auto handler = [this, port](const HttpRequest& req) -> HttpResponse {
            return this->HandleProxyRequest(port, Utils::WideToUtf8(req.method), req.path, req.headers, req.body);
        };
        if (server->Start(port, handler)) {
            LOGF_LIFECYCLE(L"[AppState] Started proxy on port %d for profile '%s'", port, profile.alias.c_str());
            runningPorts_.insert(port);
            servers_.push_back(std::move(server));
        } else {
            LOGF_LIFECYCLE(L"[AppState] FAILED to start proxy on port %d for profile '%s'", port, profile.alias.c_str());
        }
    }
}

void AppState::StopProxyServers() {
    for (auto& server : servers_) {
        if (server) server->Stop();
    }
    servers_.clear();
    runningPorts_.clear();
    LOG_LIFECYCLE(L"[AppState] All proxy servers stopped");
}

bool AppState::IsProxyRunning(int port) const {
    return runningPorts_.find(port) != runningPorts_.end();
}

std::vector<int> AppState::GetRunningPorts() const {
    return std::vector<int>(runningPorts_.begin(), runningPorts_.end());
}

void AppState::RestartProxyServers() {
    StopProxyServers();
    StartProxyServers();
}

HttpResponse AppState::HandleProxyRequest(int port, const std::string& method, const std::wstring& path,
    const std::unordered_map<std::wstring, std::wstring>& headers, const std::string& body) {

    auto requestStart = std::chrono::high_resolution_clock::now();

    HttpResponse clientResp;
    clientResp.statusCode = 502;
    clientResp.headers[L"Content-Type"] = L"application/json";
    clientResp.body = "{\"error\": \"Proxy error\"}";

    auto opt = settings_->GetProfileByPort(port);
    if (!opt) {
        clientResp.body = "{\"error\": \"Unknown proxy port\"}";
        return clientResp;
    }

    auto profile = *opt;
    profile.stats.totalRequests++;

    std::vector<std::pair<std::wstring, std::wstring>> headerVec;
    for (const auto& [name, value] : headers) {
        headerVec.push_back({name, value});
    }

    // No protocol translation; the proxy forwards requests unchanged.
    std::wstring upstreamPath = path;
    std::string requestBody = body;

    RedactionState state;
    requestBody = proxyEngine_->ProcessRequest(profile, method, path, headerVec, requestBody, state);

    bool isChatCompletions = Utils::ToLower(upstreamPath).find(L"/chat/completions") != std::wstring::npos;
    bool isAnthropicMessagesPath = IsAnthropicMessagesRequest(upstreamPath, method);
    bool stream = false;
    try {
        json bodyJson = json::parse(body);
        if (bodyJson.contains("stream") && bodyJson["stream"].is_boolean()) {
            stream = bodyJson["stream"].get<bool>();
        }
    } catch (...) {
        stream = false;
    }
    bool canStream = (isChatCompletions || isAnthropicMessagesPath) && stream;
    LOGF(L"[AppState] Stream eligibility: isChatCompletions=%s, isAnthropicMessages=%s, canStream=%s",
        isChatCompletions ? L"true" : L"false",
        isAnthropicMessagesPath ? L"true" : L"false",
        canStream ? L"true" : L"false");

    int statusCode = 200;
    std::vector<std::pair<std::wstring, std::wstring>> responseHeaders;
    std::string responseBody;
    std::string rawResponseBody;
    std::string finalBody;
    std::wstring headerLog;
    bool isSSE = false;

    if (canStream) {
        LOGF(L"[AppState] Using chunked streaming proxy on port %d for %s", port, upstreamPath.c_str());
        clientResp.isStreaming = true;
        clientResp.streamWriterOwnsHeaders = true;
        clientResp.streamWriter = [this, profile, method, path, upstreamPath, headerVec, requestBody, state](SOCKET clientSocket) {
            int upstreamStatus = 0;
            std::vector<std::pair<std::wstring, std::wstring>> upstreamHeaders;
            bool headersSent = false;
            bool isError = false;
            std::string errorBody;

            // If no labels were inserted into the request, the upstream response
            // cannot contain any labels to unredact, so pass chunks straight through.
            bool needsUnredaction = requestBody.find("<<REDACTED_") != std::string::npos;

            auto sendChunk = [&](const std::string& data) -> bool {
                return HttpServer::SendChunk(clientSocket, data.data(), data.size());
            };

            // OpenRouter's Anthropic-compatible endpoint sometimes emits SSE events
            // as bare `data:` lines without the leading `event:` line that the
            // official Anthropic SDK requires. When we are proxying an Anthropic
            // Messages request in pass-through mode, inspect each complete SSE block
            // and synthesize the missing `event:` prefix from the JSON `type` field.
            bool isAnthropicMessages = IsAnthropicMessagesRequest(path, method);
            std::string anthropicNormalizeBuffer;
            auto sendNormalized = [&](const std::string& chunk) -> bool {
                if (!isAnthropicMessages) {
                    return sendChunk(chunk);
                }
                anthropicNormalizeBuffer += chunk;
                std::string out;
                size_t pos = 0;
                while (pos < anthropicNormalizeBuffer.size()) {
                    size_t end = anthropicNormalizeBuffer.find("\n\n", pos);
                    if (end == std::string::npos) break;
                    std::string block = anthropicNormalizeBuffer.substr(pos, end - pos);
                    pos = end + 2;
                    if (block.empty()) {
                        out += "\n\n";
                        continue;
                    }
                    bool hasEvent = false;
                    std::string dataLine;
                    std::istringstream blockStream(block);
                    std::string line;
                    while (std::getline(blockStream, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (line.substr(0, 6) == "event:") {
                            hasEvent = true;
                            continue;
                        }
                        if (line.substr(0, 5) == "data:" && dataLine.empty()) {
                            size_t start = line.find_first_not_of(" \t", 5);
                            if (start != std::string::npos) dataLine = line.substr(start);
                        }
                    }
                    if (!dataLine.empty()) {
                        // OpenAI-style stream terminators are not valid Anthropic SSE.
                        std::string trimmed = dataLine;
                        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n')) {
                            trimmed.pop_back();
                        }
                        if (trimmed == "[DONE]") {
                            continue;
                        }
                    }
                    if (!hasEvent && !dataLine.empty()) {
                        try {
                            json j = json::parse(dataLine);
                            if (j.contains("type") && j["type"].is_string()) {
                                out += "event: " + j["type"].get<std::string>() + "\n";
                            }
                        } catch (...) {
                            // Not JSON or no type; fall through and emit the block as-is.
                        }
                    }
                    out += block + "\n\n";
                }
                anthropicNormalizeBuffer = anthropicNormalizeBuffer.substr(pos);
                if (!out.empty()) {
                    return sendChunk(out);
                }
                return true;
            };

            std::unique_ptr<StreamingSSEUnredactor> unredactor;
            if (needsUnredaction) {
                unredactor = std::make_unique<StreamingSSEUnredactor>(
                    proxyEngine_.get(), state, sendNormalized);
            }

            auto sendStreamingHeaders = [&](int code, const std::vector<std::pair<std::wstring, std::wstring>>& hdrs) {
                upstreamStatus = code;
                std::string statusText = "OK";
                switch (code) {
                    case 200: statusText = "OK"; break;
                    case 201: statusText = "Created"; break;
                    case 204: statusText = "No Content"; break;
                    case 400: statusText = "Bad Request"; break;
                    case 401: statusText = "Unauthorized"; break;
                    case 403: statusText = "Forbidden"; break;
                    case 404: statusText = "Not Found"; break;
                    case 500: statusText = "Internal Server Error"; break;
                    case 502: statusText = "Bad Gateway"; break;
                    case 503: statusText = "Service Unavailable"; break;
                }
                std::ostringstream responseStream;
                responseStream << "HTTP/1.1 " << code << " " << statusText << "\r\n";
                for (const auto& [name, value] : hdrs) {
                    std::string lower = Utils::WideToUtf8(Utils::ToLower(name));
                    if (lower == "content-length" || lower == "transfer-encoding" || lower == "content-encoding"
                        || lower == "keep-alive" || lower == "proxy-connection" || lower == "connection") {
                        continue;
                    }
                    std::string nameUtf8 = Utils::WideToUtf8(name);
                    std::string valueUtf8 = Utils::WideToUtf8(value);
                    while (!valueUtf8.empty() && (valueUtf8.back() == ' ' || valueUtf8.back() == '\t' || valueUtf8.back() == '\r' || valueUtf8.back() == '\n')) {
                        valueUtf8.pop_back();
                    }
                    responseStream << nameUtf8 << ": " << valueUtf8 << "\r\n";
                }
                responseStream << "Transfer-Encoding: chunked\r\n";
                responseStream << "Connection: close\r\n\r\n";
                std::string headerData = responseStream.str();
                int totalSent = 0;
                while (totalSent < (int)headerData.size()) {
                    int sent = send(clientSocket, headerData.c_str() + totalSent, (int)headerData.size() - totalSent, 0);
                    if (sent <= 0) return;
                    totalSent += sent;
                }
                headersSent = true;
                LOGF(L"[AppState] Sent streaming headers: status=%d, %d bytes", code, totalSent);
            };

            bool ok = proxyEngine_->ForwardToUpstreamStreaming(
                profile.upstreamUrl, profile.apiKey, profile.alias,
                method, upstreamPath, headerVec, requestBody,
                upstreamStatus, upstreamHeaders,
                [&](const char* data, size_t len) -> bool {
                    if (isError) {
                        errorBody.append(data, len);
                        return true;
                    }
                    if (!headersSent) {
                        LOG(L"[AppState] Upstream body chunk arrived before headers; aborting stream");
                        return false;
                    }

                    if (unredactor) {
                        return unredactor->OnChunk(data, len);
                    }
                    return sendNormalized(std::string(data, len));
                },
                [&](int code, const std::vector<std::pair<std::wstring, std::wstring>>& hdrs) {
                    if (code >= 200 && code < 300) {
                        sendStreamingHeaders(code, hdrs);
                    } else {
                        upstreamStatus = code;
                        isError = true;
                    }
                });

            if (isError) {
                // For error responses, send the raw upstream error body with the
                // upstream status. Translating error payloads is left for a future
                // improvement because error bodies are typically small JSON and
                // rarely cause timeouts.
                std::string statusText = "OK";
                switch (upstreamStatus) {
                    case 400: statusText = "Bad Request"; break;
                    case 401: statusText = "Unauthorized"; break;
                    case 403: statusText = "Forbidden"; break;
                    case 404: statusText = "Not Found"; break;
                    case 500: statusText = "Internal Server Error"; break;
                    case 502: statusText = "Bad Gateway"; break;
                    case 503: statusText = "Service Unavailable"; break;
                }
                std::ostringstream responseStream;
                responseStream << "HTTP/1.1 " << upstreamStatus << " " << statusText << "\r\n";
                responseStream << "Content-Type: application/json\r\n";
                responseStream << "Content-Length: " << errorBody.size() << "\r\n";
                responseStream << "Connection: close\r\n\r\n";
                responseStream << errorBody;
                std::string responseData = responseStream.str();
                send(clientSocket, responseData.data(), (int)responseData.size(), 0);
                LOGF(L"[AppState] Streaming error response: status=%d, body=%zu bytes", upstreamStatus, errorBody.size());
            } else if (headersSent) {
                if (ok) {
                    bool flushed = unredactor ? unredactor->Flush() : true;
                    if (flushed) {
                        HttpServer::SendChunkedEnd(clientSocket);
                        LOG(L"[AppState] Streaming response completed");
                    } else {
                        LOG(L"[AppState] Streaming response failed while flushing unredaction buffer");
                    }
                } else {
                    LOG(L"[AppState] Streaming response failed after headers were sent");
                }
            } else {
                // Upstream failed before we could send headers; send a 502 to the client.
                std::string err = "HTTP/1.1 502 Bad Gateway\r\nContent-Type: application/json\r\nContent-Length: 47\r\nConnection: close\r\n\r\n{\"error\": \"Failed to forward request to upstream\"}";
                send(clientSocket, err.c_str(), (int)err.size(), 0);
                LOG(L"[AppState] Streaming response failed before headers; sent 502");
            }
        };
        clientResp.statusCode = 200;
        // The real status code and headers are emitted by streamWriter; use placeholders for logging.
        statusCode = 200;
        headerLog = L"(chunked streaming response)";
    } else {
        bool forwarded = proxyEngine_->ForwardToUpstream(profile.upstreamUrl, profile.apiKey, profile.alias,
            method, upstreamPath, headerVec, requestBody, statusCode, responseHeaders, responseBody);

        if (!forwarded) {
            clientResp.body = "{\"error\": \"Failed to forward request to upstream\"}";
            return clientResp;
        }

        rawResponseBody = responseBody;
        finalBody = proxyEngine_->ProcessResponse(profile, responseBody, responseHeaders, state);

        clientResp.statusCode = statusCode;
        for (const auto& [name, value] : responseHeaders) {
            std::wstring lowerName = Utils::ToLower(name);
            if (lowerName == L"content-type" && Utils::ToLower(value).find(L"text/event-stream") != std::wstring::npos) {
                isSSE = true;
            }
            if (lowerName != L"content-length" && lowerName != L"transfer-encoding" && lowerName != L"content-encoding"
                && lowerName != L"keep-alive" && lowerName != L"proxy-connection") {
                clientResp.headers[name] = value;
                headerLog += name + L": " + value + L"; ";
            }
        }
        if (!isSSE) {
            // Some upstreams (e.g., OpenRouter/NVIDIA) prepend whitespace to the
            // JSON body. Strip it so JSON clients don't see an unexpected leading
            // blank sequence. Only do this for JSON payloads to avoid corrupting
            // other response types.
            auto ctIt = clientResp.headers.find(L"Content-Type");
            if (ctIt != clientResp.headers.end() &&
                Utils::ToLower(ctIt->second).find(L"application/json") != std::wstring::npos) {
                size_t firstNonWs = finalBody.find_first_not_of(" \t\r\n");
                if (firstNonWs != std::string::npos && firstNonWs > 0) {
                    finalBody = finalBody.substr(firstNonWs);
                }
            }
            clientResp.headers[L"Content-Length"] = std::to_wstring(finalBody.size());
        }
        clientResp.body = finalBody;
    }

    profile.stats.totalPIIDetected += state.piiMap.size();
    profile.stats.totalRegexMatches += state.regexMap.size();
    profile.stats.totalKeywordMatches += state.keywordMap.size();
    settings_->UpdateProfile(profile);

    NotifyStatsUpdated();
    NotifyLogAdded();

    if (canStream) {
        logManager_->AddLog(profile.alias, LogDirection::ProxyToUser,
            L"HTTP Streaming Response to client: 200 | chunked",
            L"=== RESPONSE HEADERS ===\n" + headerLog);
    } else if (logManager_->IsShowSensitive()) {
        std::wstring responseBodyPreview = Utils::Utf8ToWide(finalBody);
        if (responseBodyPreview.length() > 50000) responseBodyPreview = responseBodyPreview.substr(0, 50000) + L"...[truncated]";
        logManager_->AddLog(profile.alias, LogDirection::ProxyToUser,
            L"HTTP Response to client: " + std::to_wstring(statusCode) + L" | " + std::to_wstring(finalBody.size()) + L" bytes",
            L"=== RESPONSE HEADERS ===\n" + headerLog +
            L"\n=== FINAL RESPONSE BODY (to client) ===\n" + responseBodyPreview);
    } else {
        std::wstring responseBodyPreview = Utils::Utf8ToWide(rawResponseBody);
        if (responseBodyPreview.length() > 50000) responseBodyPreview = responseBodyPreview.substr(0, 50000) + L"...[truncated]";
        logManager_->AddLog(profile.alias, LogDirection::ProxyToUser,
            L"HTTP Response to client: " + std::to_wstring(statusCode) + L" | " + std::to_wstring(rawResponseBody.size()) + L" bytes",
            L"=== RESPONSE HEADERS ===\n" + headerLog +
            L"\n=== REDACTED RESPONSE BODY (from upstream) ===\n" + responseBodyPreview);
    }

    auto requestEnd = std::chrono::high_resolution_clock::now();
    auto requestElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(requestEnd - requestStart).count();
    LOGF(L"[AppState] Request timing: total=%lld ms, port=%d, status=%d, bodyIn=%zu, bodyOut=%zu",
        requestElapsedMs, port, statusCode, body.size(), finalBody.size());

    return clientResp;
}

void AppState::ShowTrayMenu() {
    if (!systemTray_) return;

    std::wstring currentLang = ::AgentRedactor::GetCurrentLanguage();

    std::vector<MenuItem> languageItems;
    for (size_t i = 0; i < SUPPORTED_LANGUAGES.size(); ++i) {
        const auto& lang = SUPPORTED_LANGUAGES[i];
        UINT menuId = ID_TRAY_LANGUAGE_FIRST + static_cast<UINT>(i);
        bool active = LanguageMatches(currentLang, lang.tag);
        languageItems.push_back(MenuItem::Item(lang.nativeName.c_str(), menuId,
            [this, tag = lang.tag]() {
                std::wstring currentLang = ::AgentRedactor::GetCurrentLanguage();
                if (tag == currentLang) {
                    if (systemTray_) {
                        systemTray_->ShowNotification(
                            ::AgentRedactor::LocString(L"TrayMenu_LanguageChanged_Title").c_str(),
                            ::AgentRedactor::LocString(L"TrayMenu_LanguageSame_Message").c_str(),
                            NIIF_INFO);
                    }
                } else {
                    SetLanguage(tag);
                }
            }, true, active));
    }

    std::vector<MenuItem> items;
    items.push_back(MenuItem::Item(::AgentRedactor::LocString(L"TrayMenu_Open").c_str(), ID_TRAY_OPEN, [this]() { OpenWindow(); }));
    items.push_back(MenuItem::Submenu(::AgentRedactor::LocString(L"TrayMenu_Language").c_str(), std::move(languageItems)));
    items.push_back(MenuItem::Separator());
    items.push_back(MenuItem::Item(::AgentRedactor::LocString(L"TrayMenu_StartOnBoot").c_str(), ID_TRAY_START_ON_BOOT,
        [this]() { ToggleStartOnBoot(); }, true, settings_->IsStartOnBoot()));
    items.push_back(MenuItem::Separator());
    items.push_back(MenuItem::Item(::AgentRedactor::LocString(L"TrayMenu_Quit").c_str(), ID_TRAY_QUIT, [this]() { Quit(); }));
    systemTray_->ShowMenu(items);
}

void AppState::SetLanguage(const std::wstring& language) {
    LOG(L"[AppState] SetLanguage called: " + language);
    ::AgentRedactor::SetLanguageOverride(language);

    // Refresh the tray tooltip so it matches the new language.
    if (systemTray_) {
        systemTray_->UpdateTooltip(::AgentRedactor::LocString(L"AppDisplayName").c_str());
    }

    // Notify the main window to re-localize all visible UI in-session.
    if (localizationReloadCallback_) {
        LOG(L"[AppState] Invoking localization reload callback");
        localizationReloadCallback_();
    } else {
        LOG(L"[AppState] No localization reload callback registered");
    }
}

void AppState::OpenWindow() {
    if (mainHwnd_) {
        ShowWindow(mainHwnd_, SW_RESTORE);
        SetWindowPos(mainHwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetWindowPos(mainHwnd_, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(mainHwnd_);
    }
}

void AppState::ToggleStartOnBoot() {
    bool current = settings_->IsStartOnBoot();
    settings_->SetStartOnBoot(!current);
    if (!current) {
        RegisterStartupTask();
    } else {
        UnregisterStartupTask();
    }
}

void AppState::Quit() {
    if (onMainWindowClose_) {
        onMainWindowClose_();
    }
}

void AppState::Restart() {
    std::wstring exePath = Utils::GetExecutablePath().wstring();
    std::wstring exeDir = Utils::GetExecutablePath().parent_path().wstring();
    DWORD currentPid = GetCurrentProcessId();

    // Write a small temporary batch helper that polls until this process exits
    // (so the single-instance mutex is released) and then starts a new instance.
    wchar_t tempDir[MAX_PATH];
    DWORD tempLen = GetTempPathW(MAX_PATH, tempDir);
    if (tempLen == 0) {
        LOG_LIFECYCLE(L"[AppState] Restart failed: could not get temp path. Error: " + std::to_wstring(GetLastError()));
        return;
    }
    std::wstring batchPath = std::wstring(tempDir) + L"AgentRedactor_restart_" + std::to_wstring(currentPid) + L".cmd";

    std::wstring batchContent =
        L"@echo off\r\n"
        L":wait\r\n"
        L"tasklist /FI \"PID eq " + std::to_wstring(currentPid) + L"\" 2>nul | find \"" + std::to_wstring(currentPid) + L"\" >nul\r\n"
        L"if errorlevel 1 goto start\r\n"
        L"timeout /t 1 /nobreak >nul\r\n"
        L"goto wait\r\n"
        L":start\r\n"
        L"start \"\" /D \"" + exeDir + L"\" \"" + exePath + L"\"\r\n"
        L"del /F /Q \"" + batchPath + L"\"\r\n";

    {
        std::wofstream f(batchPath, std::ios::out | std::ios::trunc);
        if (!f) {
            LOG_LIFECYCLE(L"[AppState] Restart failed: could not write helper batch file: " + batchPath);
            return;
        }
        f << batchContent;
    }

    std::wstring cmdLine = L"cmd.exe /c \"" + batchPath + L"\"";

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    DWORD creationFlags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;

    // Try to break away from any job object so the helper survives our exit.
    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                        creationFlags | CREATE_BREAKAWAY_FROM_JOB,
                        nullptr, exeDir.c_str(), &si, &pi)) {
        LOG_LIFECYCLE(L"[AppState] Restart breakaway attempt failed, retrying without breakaway. Error: " + std::to_wstring(GetLastError()));
        if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                            creationFlags,
                            nullptr, exeDir.c_str(), &si, &pi)) {
            LOG_LIFECYCLE(L"[AppState] Restart failed to launch helper process. Error: " + std::to_wstring(GetLastError()));
            return;
        }
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    restarting_ = true;
    Quit();
}
