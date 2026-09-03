#include "log_manager.h"
#include "utils.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace AgentRedactor {

LogManager::LogManager() {
}

void LogManager::AddLog(const std::wstring& profileAlias, LogDirection direction, const std::wstring& summary, const std::wstring& details) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loggingEnabled_) return;
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::wstring timeStr(64, L'\0');
    struct tm timeinfo;
    localtime_s(&timeinfo, &time);
    wcsftime(timeStr.data(), timeStr.size(), L"%H:%M:%S", &timeinfo);
    timeStr.resize(wcslen(timeStr.c_str()));

    LogEntry entry;
    entry.timestamp = timeStr;
    entry.profileAlias = profileAlias;
    entry.direction = direction;
    entry.summary = summary;
    entry.details = details;

    logs_.push_back(entry);
    while (logs_.size() > MAX_LOGS) {
        logs_.pop_front();
    }
}

std::vector<LogEntry> LogManager::GetRecentLogs(size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogEntry> result;
    size_t start = logs_.size() > count ? logs_.size() - count : 0;
    for (size_t i = start; i < logs_.size(); ++i) {
        result.push_back(logs_[i]);
    }
    return result;
}

std::vector<LogEntry> LogManager::GetLogsForProfile(const std::wstring& alias, size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogEntry> result;
    for (auto it = logs_.rbegin(); it != logs_.rend() && result.size() < count; ++it) {
        if (it->profileAlias == alias) {
            result.push_back(*it);
        }
    }
    std::reverse(result.begin(), result.end());
    return result;
}

void LogManager::ClearLogs() {
    std::lock_guard<std::mutex> lock(mutex_);
    logs_.clear();
}

void LogManager::SetLoggingEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    loggingEnabled_ = enabled;
    if (!enabled) showSensitive_ = false;
    // Keep the file-logging gate in sync (single source of truth is here).
    Utils::SetFileLoggingEnabled(enabled);
}

bool LogManager::IsLoggingEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return loggingEnabled_;
}

void LogManager::SetShowSensitive(bool show) {
    std::lock_guard<std::mutex> lock(mutex_);
    showSensitive_ = show;
}

bool LogManager::IsShowSensitive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return showSensitive_;
}

} // namespace AgentRedactor
