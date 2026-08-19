#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <deque>
// On Windows this must come before the class: windows.h (via winevent.h)
// defines IsLoggingEnabled -> IsLoggingEnabledW under UNICODE, and the
// declaration, definition and every call site must all see the macro.
#include "platform_compat.h"

namespace AgentRedactor {

enum class LogDirection {
    UserToProxy,
    ProxyToLLM,
    LLMToProxy,
    ProxyToUser
};

struct LogEntry {
    std::wstring timestamp;
    std::wstring profileAlias;
    LogDirection direction;
    std::wstring summary;
    std::wstring details;
};

class LogManager {
public:
    LogManager();
    void AddLog(const std::wstring& profileAlias, LogDirection direction, const std::wstring& summary, const std::wstring& details = L"");
    std::vector<LogEntry> GetRecentLogs(size_t count = 100) const;
    std::vector<LogEntry> GetLogsForProfile(const std::wstring& alias, size_t count = 100) const;
    void ClearLogs();

    void SetLoggingEnabled(bool enabled);
    bool IsLoggingEnabled() const;
    void SetShowSensitive(bool show);
    bool IsShowSensitive() const;

private:
    mutable std::mutex mutex_;
    std::deque<LogEntry> logs_;
    bool loggingEnabled_ = false;
    bool showSensitive_ = false;
    static constexpr size_t MAX_LOGS = 1000;
};

} // namespace AgentRedactor
