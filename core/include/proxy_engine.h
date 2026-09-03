#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <unordered_map>
#include <deque>
#include <mutex>
#include "platform_compat.h"
#include <functional>
#include "api_key_profile.h"
#include "pii_detector.h"
#include "regex_engine.h"
#include "keyword_engine.h"
#include "log_manager.h"

#ifdef _WIN32
#pragma comment(lib, "winhttp.lib")
#endif

namespace AgentRedactor {

struct RedactionState {
    std::wstring originalText;
    std::wstring redactedText;
    std::map<std::wstring, std::wstring> piiMap;
    std::map<std::wstring, std::wstring> regexMap;
    std::map<std::wstring, std::wstring> keywordMap;
};

struct SessionMatch {
    std::wstring type;
    std::wstring matchedText;
    std::wstring detail;
    std::wstring timestamp;
};

struct CachedFragment {
    std::wstring redactedText;
    std::map<std::wstring, std::wstring> piiMap;
    std::map<std::wstring, std::wstring> regexMap;
    std::map<std::wstring, std::wstring> keywordMap;
};

struct SessionState {
    size_t configHash = 0;
    std::unordered_map<size_t, CachedFragment> fragmentCache;
    std::map<std::wstring, std::wstring> piiLabelMap;
    std::map<std::wstring, std::wstring> piiTypeMap;
    std::map<std::wstring, std::wstring> regexLabelMap;
    std::map<std::wstring, std::wstring> keywordLabelMap;
    int piiCounter = 0;
    int regexCounter = 0;
    int keywordCounter = 0;
};

class ProxyEngine {
public:
    ProxyEngine(PIIDetector* detector, LogManager* logManager, std::function<void()> onUpdate);
    ~ProxyEngine() = default;

    std::string ProcessRequest(const ApiKeyProfile& profile, const std::string& method,
        const std::wstring& path, const std::vector<std::pair<std::wstring, std::wstring>>& headers,
        const std::string& body, RedactionState& state);

    std::string ProcessResponse(const ApiKeyProfile& profile, const std::string& responseBody,
        const std::vector<std::pair<std::wstring, std::wstring>>& responseHeaders,
        const RedactionState& state);

    bool ForwardToUpstream(const std::wstring& upstreamUrl, const std::wstring& apiKey,
        const std::wstring& profileAlias,
        const std::string& method, const std::wstring& path,
        const std::vector<std::pair<std::wstring, std::wstring>>& headers,
        const std::string& body,
        int& statusCode,
        std::vector<std::pair<std::wstring, std::wstring>>& responseHeaders,
        std::string& responseBody);

    // Stream the upstream response body to the client as it arrives. Returns
    // response headers via responseHeaders; each body chunk is passed to
    // onBodyChunk. If onBodyChunk returns false, streaming aborts.
    // onHeaders is invoked once as soon as the upstream status and headers
    // have been received, before any body chunks are delivered.
    bool ForwardToUpstreamStreaming(const std::wstring& upstreamUrl, const std::wstring& apiKey,
        const std::wstring& profileAlias,
        const std::string& method, const std::wstring& path,
        const std::vector<std::pair<std::wstring, std::wstring>>& headers,
        const std::string& body,
        int& statusCode,
        std::vector<std::pair<std::wstring, std::wstring>>& responseHeaders,
        std::function<bool(const char* data, size_t len)> onBodyChunk,
        std::function<void(int statusCode, const std::vector<std::pair<std::wstring, std::wstring>>& headers)> onHeaders = nullptr);

    void ClearSessionMatches(const std::wstring& profileId);
    std::vector<SessionMatch> GetSessionMatches(const std::wstring& profileId) const;

    // Rebuild an SSE stream, unredacting labels that may be split across events.
    // Streaming handlers apply it incrementally to avoid buffering the entire
    // upstream response.
    std::string RebuildSSE(const std::string& sseBody, const RedactionState& state);

private:
    size_t ComputeConfigHash(const ApiKeyProfile& profile);
    SessionState& GetSessionState(const std::wstring& profileId);
    std::wstring ApplyForwardPropagation(const std::wstring& text, SessionState& session,
        std::map<std::wstring, std::wstring>& fragPii,
        std::map<std::wstring, std::wstring>& fragRegex,
        std::map<std::wstring, std::wstring>& fragKeyword);
    std::wstring RedactWithPIIModel(const std::wstring& text, const std::vector<std::wstring>& enabledTypes,
        SessionState& session, std::map<std::wstring, std::wstring>& fragmentMap);
    std::wstring UnredactAll(const std::wstring& text, const RedactionState& state);
    bool IsSSE(const std::vector<std::pair<std::wstring, std::wstring>>& headers);
    void UpdateStats(const ApiKeyProfile& profile, size_t piiCount, size_t regexCount, size_t keywordCount);

    PIIDetector* detector_;
    LogManager* logManager_;
    std::function<void()> onUpdate_;
    RegexEngine regexEngine_;
    KeywordEngine keywordEngine_;
    std::unordered_map<std::wstring, SessionState> profileSessions_;

    struct MatchTracker {
        std::deque<SessionMatch> matches;
        static constexpr size_t MAX_MATCHES = 200;
    };
    mutable std::mutex matchMutex_;
    std::unordered_map<std::wstring, MatchTracker> matchTrackers_;
};

} // namespace AgentRedactor
