#include "EngineApp.h"
#include "utils.h"
#include "api_key_profile.h"
#include "logging.h"
#include "model_downloader.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <deque>
#include <thread>

using json = nlohmann::json;

using namespace AgentRedactor;

namespace {
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

EngineApp::~EngineApp() {
    Shutdown();
}

bool EngineApp::Initialize(const std::filesystem::path& dataDir) {
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) {
        LOG_LIFECYCLE(L"[EngineApp] Failed to create stop event");
        return false;
    }

    settings_ = std::make_unique<SettingsManager>(dataDir);
    logManager_ = std::make_unique<LogManager>();

    // MSIX/Store builds ship the weights next to the exe (identical behavior
    // to before); self-release installs download them on first run into
    // %LOCALAPPDATA%\AgentRedactor\models.
    auto modelDir = ModelDownloader::ResolveModelDir();
    detector_ = std::make_unique<PIIDetector>(modelDir);
    // Set the provider BEFORE initializing so the model loads with the user's
    // chosen execution provider (CPU, GPU/Auto/DirectML/CUDA).
    detector_->SetProvider(settings_->GetOnnxProvider());
    if (!ModelDownloader::HasModelWeights(modelDir)) {
        // Blocking first-run download: the engine must not run degraded
        // (regex/keyword-only), so the detector stays uninitialized and the
        // proxy servers are NOT started below. The GUI shows a modal download
        // dialog and drives StartModelDownloadIfNeeded() via the control API.
        {
            std::lock_guard lock(stateMutex_);
            modelDownloadRequired_ = true;
        }
        LOG_LIFECYCLE(L"[EngineApp] Model weights missing; proxy startup blocked until the first-run download completes");
    } else if (!detector_->Initialize()) {
        // Weights are present but the model failed to load. This is the same
        // degraded fallback the app has always had (regex/keyword-only); the
        // blocking flow above only covers missing weights.
        LOG_LIFECYCLE(L"[EngineApp] WARNING: PII Detector failed to initialize although model weights are present.");
    }

    logManager_->SetLoggingEnabled(settings_->IsLoggingEnabled());
    // Sensitive logging is session-only and always starts off.
    logManager_->SetShowSensitive(false);

    proxyEngine_ = std::make_unique<ProxyEngine>(detector_.get(), logManager_.get(), []() {
        // Stats are persisted per-request into settings.json; the GUI polls
        // the control API for updates, so no push notification is needed.
    });

    if (!controlServer_.Start(ControlServer::kDefaultPort, settings_->GetConfigDir(),
            [this](const HttpRequest& req) { return this->HandleControlRequest(req); })) {
        LOG_LIFECYCLE(L"[EngineApp] Failed to start the control API");
        return false;
    }

    // Never serve traffic while the blocking first-run download is pending:
    // the proxies start only after the download + detector init succeed.
    if (!IsModelDownloadRequired()) {
        StartProxyServers();
    }
    return true;
}

void EngineApp::Shutdown() {
    LOG(L"=== Agent Redactor Engine Shutdown ===");
    StopProxyServers();
    controlServer_.Stop();
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
}

void EngineApp::Run() {
    LOG_LIFECYCLE(L"[EngineApp] Engine running");
    WaitForSingleObject(stopEvent_, INFINITE);
}

void EngineApp::RequestStop() {
    if (stopEvent_) SetEvent(stopEvent_);
}

// ---------------------------------------------------------------------------
// Blocking first-run model download. While the download is pending the engine
// is blocked: no detector, no proxy servers. The GUI polls /status.
// ---------------------------------------------------------------------------

bool EngineApp::IsModelDownloadRequired() const {
    std::lock_guard lock(stateMutex_);
    return modelDownloadRequired_;
}

void EngineApp::StartModelDownloadIfNeeded() {
    {
        std::lock_guard lock(stateMutex_);
        if (modelDownloadInProgress_) return;
        modelDownloadInProgress_ = true;
        modelDownloadFailed_ = false;
        modelDownloadPercent_ = -1;
        modelDownloadStatus_.clear();
    }

    const auto fallbackDir = ModelDownloader::GetFallbackModelDir();
    std::thread([this, fallbackDir]() {
        auto progress = [this](int percent, const std::wstring& message) {
            {
                std::lock_guard lock(stateMutex_);
                modelDownloadPercent_ = percent;
                if (!message.empty()) modelDownloadStatus_ = message;
            }
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
            } else if (ModelDownloader::ResolveModelDir() == fallbackDir) {
                // The weights passed the size check but failed to load (e.g.
                // a corrupt download from before size verification existed).
                // Delete them so the next Retry re-downloads instead of
                // looping on the same bad file — but ONLY in the fallback
                // dir; exe-dir weights are MSIX package content and are never
                // touched.
                std::error_code ec;
                std::filesystem::remove(ModelDownloader::WeightsFilePath(fallbackDir), ec);
                LOG_LIFECYCLE(L"[EngineApp] Deleted corrupt downloaded weights; the next retry will re-download");
            }
            LOG_LIFECYCLE(initOk
                ? L"[EngineApp] Model downloaded and initialized"
                : L"[EngineApp] Model downloaded but detector initialization failed");
            ok = initOk;
        } else {
            LOG_LIFECYCLE(L"[EngineApp] Model download failed");
        }

        {
            std::lock_guard lock(stateMutex_);
            modelDownloadInProgress_ = false;
            modelDownloadFailed_ = !ok;
            modelDownloadPercent_ = ok ? 100 : -1;
            if (ok) modelDownloadRequired_ = false;
        }
    }).detach();
}

void EngineApp::StartProxyServers() {
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
            LOGF_LIFECYCLE(L"[EngineApp] Started proxy on port %d for profile '%s'", port, profile.alias.c_str());
            runningPorts_.insert(port);
            servers_.push_back(std::move(server));
        } else {
            LOGF_LIFECYCLE(L"[EngineApp] FAILED to start proxy on port %d for profile '%s'", port, profile.alias.c_str());
        }
    }
}

void EngineApp::StopProxyServers() {
    for (auto& server : servers_) {
        if (server) server->Stop();
    }
    servers_.clear();
    runningPorts_.clear();
    LOG_LIFECYCLE(L"[EngineApp] All proxy servers stopped");
}

bool EngineApp::IsProxyRunning(int port) const {
    return runningPorts_.find(port) != runningPorts_.end();
}

void EngineApp::RestartProxyServers() {
    StopProxyServers();
    StartProxyServers();
}

HttpResponse EngineApp::HandleProxyRequest(int port, const std::string& method, const std::wstring& path,
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
    LOGF(L"[EngineApp] Stream eligibility: isChatCompletions=%s, isAnthropicMessages=%s, canStream=%s",
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
        LOGF(L"[EngineApp] Using chunked streaming proxy on port %d for %s", port, upstreamPath.c_str());
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
                LOGF(L"[EngineApp] Sent streaming headers: status=%d, %d bytes", code, totalSent);
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
                        LOG(L"[EngineApp] Upstream body chunk arrived before headers; aborting stream");
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
                LOGF(L"[EngineApp] Streaming error response: status=%d, body=%zu bytes", upstreamStatus, errorBody.size());
            } else if (headersSent) {
                if (ok) {
                    bool flushed = unredactor ? unredactor->Flush() : true;
                    if (flushed) {
                        HttpServer::SendChunkedEnd(clientSocket);
                        LOG(L"[EngineApp] Streaming response completed");
                    } else {
                        LOG(L"[EngineApp] Streaming response failed while flushing unredaction buffer");
                    }
                } else {
                    LOG(L"[EngineApp] Streaming response failed after headers were sent");
                }
            } else {
                // Upstream failed before we could send headers; send a 502 to the client.
                std::string err = "HTTP/1.1 502 Bad Gateway\r\nContent-Type: application/json\r\nContent-Length: 47\r\nConnection: close\r\n\r\n{\"error\": \"Failed to forward request to upstream\"}";
                send(clientSocket, err.c_str(), (int)err.size(), 0);
                LOG(L"[EngineApp] Streaming response failed before headers; sent 502");
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
    LOGF(L"[EngineApp] Request timing: total=%lld ms, port=%d, status=%d, bodyIn=%zu, bodyOut=%zu",
        requestElapsedMs, port, statusCode, body.size(), finalBody.size());

    return clientResp;
}

// ---------------------------------------------------------------------------
// Control API
// ---------------------------------------------------------------------------

HttpResponse EngineApp::JsonResponse(int statusCode, const std::string& body) {
    HttpResponse resp;
    resp.statusCode = statusCode;
    resp.headers[L"Content-Type"] = L"application/json";
    resp.body = body;
    return resp;
}

HttpResponse EngineApp::HandleControlRequest(const HttpRequest& request) {
    std::wstring path = PathWithoutQuery(request.path);
    std::wstring query;
    size_t q = request.path.find(L'?');
    if (q != std::wstring::npos) query = request.path.substr(q + 1);
    std::string method = Utils::WideToUtf8(request.method);

    try {
        if (path == L"/status" && method == "GET") return ApiGetStatus();
        if (path == L"/settings" && method == "GET") return ApiGetSettings();
        if (path == L"/profiles" && method == "GET") return ApiGetProfiles();
        if (path == L"/profiles" && method == "POST") return ApiPostProfile(request.body);
        if (path == L"/unlock" && method == "POST") return ApiUnlock(request.body);
        if (path == L"/logs" && method == "GET") {
            std::wstring profileParam;
            const std::wstring prefix = L"profile=";
            if (Utils::StartsWith(query, prefix)) profileParam = query.substr(prefix.size());
            return ApiGetLogs(profileParam);
        }
        if (path == L"/engine/restart-listeners" && method == "POST") {
            RestartProxyServers();
            return JsonResponse(200, "{\"ok\": true}");
        }
        if (path == L"/engine/download-model" && method == "POST") {
            StartModelDownloadIfNeeded();
            return JsonResponse(200, "{\"ok\": true}");
        }
        if (path == L"/engine/stop" && method == "POST") {
            // Respond first, then stop: Shutdown() joins the listener threads,
            // which would deadlock if Stop ran inside this request handler.
            std::thread([this]() {
                Sleep(200);
                RequestStop();
            }).detach();
            return JsonResponse(200, "{\"ok\": true}");
        }

        static const std::wstring settingsPrefix = L"/settings/";
        if (Utils::StartsWith(path, settingsPrefix) && method == "PUT") {
            return ApiPutSetting(path.substr(settingsPrefix.size()), request.body);
        }

        static const std::wstring profilesPrefix = L"/profiles/";
        if (Utils::StartsWith(path, profilesPrefix)) {
            std::wstring rest = path.substr(profilesPrefix.size());
            static const std::wstring matchesSuffix = L"/matches";
            static const std::wstring apikeySuffix = L"/apikey";
            if (Utils::EndsWith(rest, apikeySuffix) && method == "GET") {
                return ApiGetProfileApiKey(rest.substr(0, rest.size() - apikeySuffix.size()));
            }
            if (Utils::EndsWith(rest, matchesSuffix)) {
                std::wstring id = rest.substr(0, rest.size() - matchesSuffix.size());
                if (method == "GET") return ApiGetMatches(id);
                if (method == "DELETE") return ApiDeleteMatches(id);
            } else {
                if (method == "PUT") return ApiPutProfile(rest, request.body);
                if (method == "DELETE") return ApiDeleteProfile(rest);
            }
        }
    } catch (const std::exception& e) {
        LOG(L"[EngineApp] Control API error: " + Utils::Utf8ToWide(e.what()));
        return JsonResponse(500, "{\"error\": \"internal error\"}");
    } catch (...) {
        return JsonResponse(500, "{\"error\": \"internal error\"}");
    }

    return JsonResponse(404, "{\"error\": \"not found\"}");
}

HttpResponse EngineApp::ApiGetStatus() {
    std::lock_guard lock(stateMutex_);
    json j;
#ifdef AR_VERSION_STRING
    j["engineVersion"] = AR_VERSION_STRING;
#else
    j["engineVersion"] = "dev";
#endif
    j["unlocked"] = settings_->IsUnlocked();
    j["masterPasswordEnabled"] = settings_->IsMasterPasswordEnabled();
    j["modelDownloadRequired"] = modelDownloadRequired_;
    j["modelDownloadInProgress"] = modelDownloadInProgress_;
    j["modelDownloadFailed"] = modelDownloadFailed_;
    j["modelDownloadPercent"] = modelDownloadPercent_;
    j["modelDownloadStatus"] = Utils::WideToUtf8(modelDownloadStatus_);
    json profiles = json::array();
    for (const auto& p : settings_->GetProfiles()) {
        json pj;
        pj["id"] = Utils::WideToUtf8(p.id);
        pj["port"] = p.port;
        pj["enabled"] = p.enabled;
        pj["proxyRunning"] = IsProxyRunning(p.port);
        profiles.push_back(std::move(pj));
    }
    j["profiles"] = std::move(profiles);
    return JsonResponse(200, j.dump());
}

HttpResponse EngineApp::ApiGetSettings() {
    json j;
    j["startOnBoot"] = settings_->IsStartOnBoot();
    j["onnxProvider"] = Utils::WideToUtf8(settings_->GetOnnxProvider());
    j["loggingEnabled"] = settings_->IsLoggingEnabled();
    j["showSensitive"] = logManager_->IsShowSensitive();
    j["appLanguage"] = Utils::WideToUtf8(settings_->GetAppLanguage());
    j["masterPasswordEnabled"] = settings_->IsMasterPasswordEnabled();
    j["unlocked"] = settings_->IsUnlocked();
    return JsonResponse(200, j.dump());
}

HttpResponse EngineApp::ApiPutSetting(const std::wstring& key, const std::string& body) {
    json j = json::parse(body);
    if (key == L"startOnBoot") {
        settings_->SetStartOnBoot(j.at("value").get<bool>());
    } else if (key == L"onnxProvider") {
        settings_->SetOnnxProvider(Utils::Utf8ToWide(j.at("value").get<std::string>()));
    } else if (key == L"loggingEnabled") {
        bool enabled = j.at("value").get<bool>();
        settings_->SetLoggingEnabled(enabled);
        logManager_->SetLoggingEnabled(enabled);
    } else if (key == L"showSensitive") {
        // Session-only; intentionally not persisted.
        logManager_->SetShowSensitive(j.at("value").get<bool>());
    } else if (key == L"appLanguage") {
        settings_->SetAppLanguage(Utils::Utf8ToWide(j.at("value").get<std::string>()));
    } else if (key == L"enableMasterPassword") {
        if (!settings_->EnableMasterPassword(Utils::Utf8ToWide(j.at("value").get<std::string>()))) {
            return JsonResponse(400, "{\"error\": \"failed to enable master password\"}");
        }
    } else if (key == L"changeMasterPassword") {
        auto oldPassword = Utils::Utf8ToWide(j.at("oldValue").get<std::string>());
        auto newPassword = Utils::Utf8ToWide(j.at("value").get<std::string>());
        if (!settings_->ChangeMasterPassword(oldPassword, newPassword)) {
            return JsonResponse(400, "{\"error\": \"failed to change master password\"}");
        }
    } else if (key == L"disableMasterPassword") {
        settings_->DisableMasterPassword();
    } else if (key == L"clearLogs") {
        logManager_->ClearLogs();
    } else {
        return JsonResponse(404, "{\"error\": \"unknown setting\"}");
    }
    return JsonResponse(200, "{\"ok\": true}");
}

HttpResponse EngineApp::ApiGetProfiles() {
    json arr = json::array();
    for (auto p : settings_->GetProfiles()) {
        // Never leak the API key over the wire; the GUI only displays it masked.
        if (!p.apiKey.empty()) {
            p.apiKey = p.apiKey.substr(0, std::min<size_t>(3, p.apiKey.size())) + L"...****";
        }
        json pj;
        p.ToJson(pj);
        pj["proxyRunning"] = IsProxyRunning(p.port);
        arr.push_back(std::move(pj));
    }
    return JsonResponse(200, arr.dump());
}

HttpResponse EngineApp::ApiGetProfileApiKey(const std::wstring& id) {
    // The one endpoint that returns a secret: enforce the master-password
    // lock server-side (everywhere else the lock is UX-level, as in the GUI).
    if (settings_->IsMasterPasswordEnabled() && !settings_->IsUnlocked()) {
        return JsonResponse(403, "{\"error\": \"locked\"}");
    }
    auto profile = settings_->GetProfileById(id);
    if (!profile) {
        return JsonResponse(404, "{\"error\": \"unknown profile\"}");
    }
    json j;
    j["apiKey"] = Utils::WideToUtf8(profile->apiKey);
    return JsonResponse(200, j.dump());
}

HttpResponse EngineApp::ApiPostProfile(const std::string& body) {
    json j = json::parse(body.empty() ? std::string("{}") : body);
    ApiKeyProfile profile = ApiKeyProfile::FromJson(j);
    settings_->AddProfile(profile);
    RestartProxyServers();
    return JsonResponse(200, "{\"ok\": true}");
}

HttpResponse EngineApp::ApiPutProfile(const std::wstring& id, const std::string& body) {
    if (!settings_->GetProfileById(id)) {
        return JsonResponse(404, "{\"error\": \"unknown profile\"}");
    }
    json j = json::parse(body);
    ApiKeyProfile profile = ApiKeyProfile::FromJson(j);
    profile.id = id;
    // A masked apiKey means "unchanged": keep the stored key.
    if (profile.apiKey.find(L"...****") != std::wstring::npos) {
        profile.apiKey = settings_->GetProfileById(id)->apiKey;
    }
    settings_->UpdateProfile(profile);
    RestartProxyServers();
    return JsonResponse(200, "{\"ok\": true}");
}

HttpResponse EngineApp::ApiDeleteProfile(const std::wstring& id) {
    if (!settings_->GetProfileById(id)) {
        return JsonResponse(404, "{\"error\": \"unknown profile\"}");
    }
    settings_->RemoveProfile(id);
    RestartProxyServers();
    return JsonResponse(200, "{\"ok\": true}");
}

HttpResponse EngineApp::ApiGetMatches(const std::wstring& id) {
    json arr = json::array();
    for (const auto& m : proxyEngine_->GetSessionMatches(id)) {
        json mj;
        mj["type"] = Utils::WideToUtf8(m.type);
        mj["matchedText"] = Utils::WideToUtf8(m.matchedText);
        mj["detail"] = Utils::WideToUtf8(m.detail);
        mj["timestamp"] = Utils::WideToUtf8(m.timestamp);
        arr.push_back(std::move(mj));
    }
    return JsonResponse(200, arr.dump());
}

HttpResponse EngineApp::ApiDeleteMatches(const std::wstring& id) {
    proxyEngine_->ClearSessionMatches(id);
    return JsonResponse(200, "{\"ok\": true}");
}

HttpResponse EngineApp::ApiUnlock(const std::string& body) {
    json j = json::parse(body);
    bool ok = settings_->UnlockWithPassword(Utils::Utf8ToWide(j.at("password").get<std::string>()));
    return JsonResponse(200, std::string("{\"ok\": ") + (ok ? "true" : "false") + "}");
}

HttpResponse EngineApp::ApiGetLogs(const std::wstring& profileParam) {
    std::vector<LogEntry> entries;
    if (profileParam.empty()) {
        entries = logManager_->GetRecentLogs();
    } else {
        entries = logManager_->GetLogsForProfile(profileParam);
    }
    json arr = json::array();
    for (const auto& e : entries) {
        json ej;
        ej["timestamp"] = Utils::WideToUtf8(e.timestamp);
        ej["profileAlias"] = Utils::WideToUtf8(e.profileAlias);
        ej["direction"] = static_cast<int>(e.direction);
        ej["summary"] = Utils::WideToUtf8(e.summary);
        ej["details"] = Utils::WideToUtf8(e.details);
        arr.push_back(std::move(ej));
    }
    return JsonResponse(200, arr.dump());
}
