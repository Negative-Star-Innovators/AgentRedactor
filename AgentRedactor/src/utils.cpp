#include "utils.h"
#include <windows.h>
#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <mutex>
#include <atomic>

namespace AgentRedactor {
namespace Utils {

static std::wstring g_logFilePath;
static std::mutex g_logMutex;

static std::wstring g_debugTrafficLogFilePath;
static std::mutex g_debugTrafficLogMutex;

// Runtime gate for LOG/LOGF/LOG_TRAFFIC. LogManager is the single source of
// truth and pushes changes here via SetFileLoggingEnabled.
static std::atomic<bool> g_fileLoggingEnabled{ false };

void SetFileLoggingEnabled(bool enabled) {
    g_fileLoggingEnabled.store(enabled, std::memory_order_relaxed);
}

bool IsFileLoggingEnabled() {
    return g_fileLoggingEnabled.load(std::memory_order_relaxed);
}

void InitializeLogging(const std::filesystem::path& logDirOverride) {
    // Logs live in Roaming AppData (with settings), NOT in LocalAppData:
    // the Velopack install root is %LocalAppData%\AgentRedactor, and keeping
    // app data out of it lets uninstalls/reinstalls behave correctly.
    auto logDir = logDirOverride.empty() ? GetAppDataPath() : logDirOverride;
    auto sessionsDir = logDir / L"sessions";
    g_logFilePath = logDir / L"agent_redactor.log";

    CreateDirectoryW(logDir.c_str(), nullptr);
    CreateDirectoryW(sessionsDir.c_str(), nullptr);

    // Rotate previous session log if it exists and has content
    try {
        if (std::filesystem::exists(g_logFilePath) && std::filesystem::file_size(g_logFilePath) > 0) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            struct tm timeinfo;
            localtime_s(&timeinfo, &time);

            std::wostringstream oss;
            oss << std::setfill(L'0')
                << std::setw(4) << (timeinfo.tm_year + 1900)
                << std::setw(2) << (timeinfo.tm_mon + 1)
                << std::setw(2) << timeinfo.tm_mday << L"_"
                << std::setw(2) << timeinfo.tm_hour
                << std::setw(2) << timeinfo.tm_min
                << std::setw(2) << timeinfo.tm_sec;

            auto archivePath = sessionsDir / (oss.str() + L".log");
            int counter = 1;
            while (std::filesystem::exists(archivePath)) {
                std::wostringstream oss2;
                oss2 << std::setfill(L'0')
                    << std::setw(4) << (timeinfo.tm_year + 1900)
                    << std::setw(2) << (timeinfo.tm_mon + 1)
                    << std::setw(2) << timeinfo.tm_mday << L"_"
                    << std::setw(2) << timeinfo.tm_hour
                    << std::setw(2) << timeinfo.tm_min
                    << std::setw(2) << timeinfo.tm_sec
                    << L"_" << counter++;
                archivePath = sessionsDir / (oss2.str() + L".log");
            }
            std::filesystem::rename(g_logFilePath, archivePath);
        }
    } catch (...) {}

    LogLifecycleMessage(L"=== Agent Redactor Started ===");
    InitializeDebugTrafficLogging(logDirOverride);
}

void InitializeDebugTrafficLogging(const std::filesystem::path& logDirOverride) {
    auto logDir = logDirOverride.empty() ? GetAppDataPath() : logDirOverride;
    g_debugTrafficLogFilePath = logDir / L"agent_redactor_debug.log";
    CreateDirectoryW(logDir.c_str(), nullptr);
}

void LogTrafficMessage(const std::wstring& direction, const std::wstring& message) {
    if (!IsFileLoggingEnabled()) return;
    if (g_debugTrafficLogFilePath.empty()) InitializeDebugTrafficLogging();
    std::lock_guard<std::mutex> lock(g_debugTrafficLogMutex);
    try {
        std::wofstream logFile(g_debugTrafficLogFilePath, std::ios::app);
        if (logFile) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::wstring timeStr = FormatLocalizedDateTime(time);
            logFile << L"[" << timeStr << L"] [" << direction << L"] " << message << std::endl;
            logFile.flush();
        }
    } catch (...) {}
}

void LogShutdown() {
    LogLifecycleMessage(L"=== Agent Redactor Shutdown ===");
}

static void WriteLogLine(const std::wstring& message) {
    if (g_logFilePath.empty()) InitializeLogging();
    std::lock_guard<std::mutex> lock(g_logMutex);
    try {
        std::wofstream logFile(g_logFilePath, std::ios::app);
        if (logFile) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::wstring timeStr = FormatLocalizedDateTime(time);
            logFile << L"[" << timeStr << L"] " << message << std::endl;
            logFile.flush();
        }
    } catch (...) {}
    OutputDebugStringW((message + L"\n").c_str());
}

void LogMessage(const std::wstring& message) {
    if (!IsFileLoggingEnabled()) return;
    WriteLogLine(message);
}

void LogLifecycleMessage(const std::wstring& message) {
    // Always written: app-lifecycle diagnostics only, never user content.
    WriteLogLine(message);
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (size_needed <= 0) return L"";
    std::wstring result(size_needed - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, result.data(), size_needed);
    return result;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) return "";
    std::string result(size_needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), size_needed, nullptr, nullptr);
    return result;
}

std::wstring ToLower(const std::wstring& str) {
    std::wstring result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return towlower(c); });
    return result;
}

std::wstring Trim(const std::wstring& str) {
    size_t start = str.find_first_not_of(L" \t\n\r\f\v");
    if (start == std::wstring::npos) return L"";
    size_t end = str.find_last_not_of(L" \t\n\r\f\v");
    return str.substr(start, end - start + 1);
}

bool StartsWith(const std::wstring& str, const std::wstring& prefix) {
    if (prefix.size() > str.size()) return false;
    return str.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::wstring& str, const std::wstring& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::wstring> Split(const std::wstring& str, wchar_t delimiter) {
    std::vector<std::wstring> result;
    std::wstringstream ss(str);
    std::wstring item;
    while (std::getline(ss, item, delimiter)) {
        result.push_back(item);
    }
    return result;
}

std::wstring Join(const std::vector<std::wstring>& parts, const std::wstring& delimiter) {
    if (parts.empty()) return L"";
    std::wstring result = parts[0];
    for (size_t i = 1; i < parts.size(); ++i) {
        result += delimiter + parts[i];
    }
    return result;
}

std::wstring ReplaceAll(const std::wstring& str, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return str;
    std::wstring result = str;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::wstring::npos) {
        result.replace(pos, from.size(), to);
        pos += to.size();
    }
    return result;
}

std::wstring ReplaceAllCaseInsensitive(const std::wstring& str, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return str;
    std::wstring result = str;
    size_t pos = 0;
    while (true) {
        auto it = std::search(result.begin() + pos, result.end(), from.begin(), from.end(),
            [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); });
        if (it == result.end()) break;
        pos = it - result.begin();
        result.replace(pos, from.size(), to);
        pos += to.size();
    }
    return result;
}

size_t HashCombine(size_t seed, size_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

size_t HashWString(const std::wstring& str) {
    return std::hash<std::wstring>{}(str);
}

std::optional<std::wstring> ReadFileAsString(const std::filesystem::path& path) {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) return std::nullopt;
        std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return Utf8ToWide(bytes);
    } catch (...) {
        return std::nullopt;
    }
}

bool WriteStringToFile(const std::filesystem::path& path, const std::wstring& content) {
    try {
        std::string utf8 = WideToUtf8(content);
        std::ofstream file(path, std::ios::binary);
        if (!file) return false;
        file << utf8;
        return file.good();
    } catch (...) {
        return false;
    }
}

bool FileExists(const std::filesystem::path& path) {
    return std::filesystem::exists(path);
}

bool CreateDirectoryRecursive(const std::filesystem::path& path) {
    try {
        return std::filesystem::create_directories(path);
    } catch (...) {
        return false;
    }
}

std::filesystem::path GetAppDataPath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::filesystem::path(path) / L"AgentRedactor";
    }
    return std::filesystem::path(L"C:\\AgentRedactor");
}

std::filesystem::path GetCurrentLogFilePath() {
    if (g_logFilePath.empty()) {
        return GetAppDataPath() / L"agent_redactor.log";
    }
    return g_logFilePath;
}

std::filesystem::path GetExecutablePath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

std::wstring GetCurrentMonth() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::wostringstream oss;
    oss << std::setfill(L'0') << (tm.tm_year + 1900) << L'-' << std::setw(2) << (tm.tm_mon + 1);
    return oss.str();
}

int64_t GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

std::wstring GenerateUUID() {
    UUID uuid = {};
    RPC_STATUS status = UuidCreate(&uuid);
    if (status != RPC_S_OK) {
        return L"uuid_" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    std::wostringstream oss;
    oss << std::hex << std::setfill(L'0')
        << std::setw(8) << uuid.Data1 << L'-' << std::setw(4) << uuid.Data2 << L'-' << std::setw(4) << uuid.Data3 << L'-';
    for (int i = 0; i < 8; ++i) oss << std::setw(2) << static_cast<int>(uuid.Data4[i]);
    return oss.str();
}

std::wstring FormatSize(size_t size) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB"};
    int unitIndex = 0;
    double s = static_cast<double>(size);
    while (s >= 1024.0 && unitIndex < 3) { s /= 1024.0; ++unitIndex; }
    return FormatLocalizedFloat(s, 1) + L" " + units[unitIndex];
}

std::wstring FormatLocalizedFloat(double value, int precision) {
    try {
        std::wostringstream oss;
        oss.imbue(std::locale(""));
        oss << std::fixed << std::setprecision(precision) << value;
        return oss.str();
    } catch (...) {
        std::wostringstream oss;
        oss << std::fixed << std::setprecision(precision) << value;
        return oss.str();
    }
}

double ParseLocalizedFloat(const std::wstring& text) {
    std::wstring normalized = text;
    try {
        auto loc = std::locale("");
        auto& facet = std::use_facet<std::numpunct<wchar_t>>(loc);
        wchar_t decimal = facet.decimal_point();
        if (decimal == L',') {
            // Replace periods with commas for German-style parsing
            std::replace(normalized.begin(), normalized.end(), L'.', L',');
        }
    } catch (...) {}
    return _wtof(normalized.c_str());
}

static SYSTEMTIME TmToSystemTime(const struct tm& timeinfo) {
    SYSTEMTIME st = {};
    st.wYear = static_cast<WORD>(timeinfo.tm_year + 1900);
    st.wMonth = static_cast<WORD>(timeinfo.tm_mon + 1);
    st.wDay = static_cast<WORD>(timeinfo.tm_mday);
    st.wHour = static_cast<WORD>(timeinfo.tm_hour);
    st.wMinute = static_cast<WORD>(timeinfo.tm_min);
    st.wSecond = static_cast<WORD>(timeinfo.tm_sec);
    st.wMilliseconds = 0;
    return st;
}

std::wstring FormatLocalizedTime(const std::time_t& time) {
    std::wstring buffer(64, L'\0');
    struct tm timeinfo;
    localtime_s(&timeinfo, &time);
    SYSTEMTIME st = TmToSystemTime(timeinfo);
    int len = GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, buffer.data(), static_cast<int>(buffer.size()));
    if (len > 0) {
        buffer.resize(len - 1);
    } else {
        wcsftime(buffer.data(), buffer.size(), L"%H:%M:%S", &timeinfo);
        buffer.resize(wcslen(buffer.c_str()));
    }
    return buffer;
}

std::wstring FormatLocalizedDateTime(const std::time_t& time) {
    std::wstring buffer(128, L'\0');
    struct tm timeinfo;
    localtime_s(&timeinfo, &time);
    SYSTEMTIME st = TmToSystemTime(timeinfo);
    int len = GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, buffer.data(), static_cast<int>(buffer.size()), nullptr);
    if (len > 0) {
        buffer.resize(len - 1);
    } else {
        wcsftime(buffer.data(), buffer.size(), L"%Y-%m-%d", &timeinfo);
        buffer.resize(wcslen(buffer.c_str()));
    }
    std::wstring timeStr = FormatLocalizedTime(time);
    return buffer + L" " + timeStr;
}

} // namespace Utils
} // namespace AgentRedactor
