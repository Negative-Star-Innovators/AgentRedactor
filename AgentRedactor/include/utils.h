#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <optional>
#include <windows.h>
#include "logging.h"

namespace AgentRedactor {
namespace Utils {

void InitializeLogging(const std::filesystem::path& logDir = L"");
void LogMessage(const std::wstring& message);
// Always-written lifecycle diagnostics (no user content allowed).
void LogLifecycleMessage(const std::wstring& message);

// Runtime flag backing LOG/LOGF/LOG_TRAFFIC. LogManager is the single
// source of truth and pushes changes here via SetFileLoggingEnabled.
void SetFileLoggingEnabled(bool enabled);
bool IsFileLoggingEnabled();

// Separate development debug traffic log.
void InitializeDebugTrafficLogging(const std::filesystem::path& logDir = L"");
void LogTrafficMessage(const std::wstring& direction, const std::wstring& message);

std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);
std::wstring ToLower(const std::wstring& str);
std::wstring Trim(const std::wstring& str);
bool StartsWith(const std::wstring& str, const std::wstring& prefix);
bool EndsWith(const std::wstring& str, const std::wstring& suffix);
std::vector<std::wstring> Split(const std::wstring& str, wchar_t delimiter);
std::wstring Join(const std::vector<std::wstring>& parts, const std::wstring& delimiter);
std::wstring ReplaceAll(const std::wstring& str, const std::wstring& from, const std::wstring& to);
std::wstring ReplaceAllCaseInsensitive(const std::wstring& str, const std::wstring& from, const std::wstring& to);

size_t HashCombine(size_t seed, size_t value);
size_t HashWString(const std::wstring& str);

std::optional<std::wstring> ReadFileAsString(const std::filesystem::path& path);
bool WriteStringToFile(const std::filesystem::path& path, const std::wstring& content);
bool FileExists(const std::filesystem::path& path);
bool CreateDirectoryRecursive(const std::filesystem::path& path);
std::filesystem::path GetAppDataPath();
std::filesystem::path GetExecutablePath();
std::filesystem::path GetCurrentLogFilePath();
void LogShutdown();

std::wstring GetCurrentMonth();
int64_t GetCurrentTimestamp();
std::wstring GenerateUUID();
std::wstring FormatSize(size_t size);

// Locale-aware formatting helpers
std::wstring FormatLocalizedFloat(double value, int precision = 6);
double ParseLocalizedFloat(const std::wstring& text);
std::wstring FormatLocalizedTime(const std::time_t& time);
std::wstring FormatLocalizedDateTime(const std::time_t& time);

// Simple synchronous HTTP(S) GET helpers (WinHTTP), shared by the update
// manager and the first-run model downloader. Follow redirects across hosts
// (the update feed and GitHub release assets both 302). Never throw.
bool HttpGetString(const std::wstring& url, std::string& outBody);
// Resumes from an existing non-empty `destPath` via a Range request when the
// server honors it (206); a 200 restarts from zero. Fails when the server
// reports a total size and fewer bytes arrive (truncated transfer).
bool HttpDownloadFile(const std::wstring& url, const std::filesystem::path& destPath,
    const std::function<void(uint64_t downloaded, uint64_t total)>& progress = nullptr);
// Parallel variant for large files: probes the total size and range support,
// then downloads up to `maxSegments` ranges concurrently into `<destPath>.partN`
// files (each resumable) and concatenates them into `destPath`, verifying the
// final size. Part files are kept on failure so the next call resumes. Falls
// back to HttpDownloadFile when ranges are unsupported or the file is small.
bool HttpDownloadFileSegmented(const std::wstring& url, const std::filesystem::path& destPath,
    const std::function<void(uint64_t downloaded, uint64_t total)>& progress = nullptr,
    size_t maxSegments = 8);

} // namespace Utils
} // namespace AgentRedactor
