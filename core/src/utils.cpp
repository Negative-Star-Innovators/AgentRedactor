#include "utils.h"
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <winhttp.h>
#else
#include <cctype>
#include <codecvt>
#include <locale>
#include <random>
#include <curl/curl.h>
#endif
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <atomic>
#include <thread>
#include <cwchar>
#include <cwctype>

namespace AgentRedactor {
namespace Utils {

static std::filesystem::path g_logFilePath;
static std::mutex g_logMutex;

static std::filesystem::path g_debugTrafficLogFilePath;
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

    CreateDirectoryRecursive(logDir);
    CreateDirectoryRecursive(sessionsDir);

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
    CreateDirectoryRecursive(logDir);
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
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::wstring timeStr = FormatLocalizedDateTime(time);
#ifdef _WIN32
        // The engine and the UI both append to this file. A CRT append stream
        // seeks to EOF and then writes, so two processes racing that pattern
        // overwrite each other's lines (seen in CI as a mangled
        // "[UpdateManager] Up to date" line). A handle opened with
        // FILE_APPEND_DATA only (no FILE_WRITE_DATA) ignores the file pointer
        // and appends every WriteFile atomically at EOF.
        HANDLE h = CreateFileW(g_logFilePath.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            const std::string utf8 = WideToUtf8(L"[" + timeStr + L"] " + message + L"\r\n");
            DWORD written = 0;
            WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            CloseHandle(h);
        }
#else
        std::wofstream logFile(g_logFilePath, std::ios::app);
        if (logFile) {
            logFile << L"[" << timeStr << L"] " << message << std::endl;
            logFile.flush();
        }
#endif
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
#ifdef _WIN32
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (size_needed <= 0) return L"";
    std::wstring result(size_needed - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, result.data(), size_needed);
    return result;
#else
    // wchar_t is 32-bit on Linux; wstrings stay opaque UTF-16-ish containers
    // (supplementary characters become surrogate pairs), exactly matching the
    // Windows representation the rest of the core assumes.
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
        return conv.from_bytes(utf8);
    } catch (...) {
        return L"";
    }
    #pragma GCC diagnostic pop
#endif
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
#ifdef _WIN32
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) return "";
    std::string result(size_needed - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), size_needed, nullptr, nullptr);
    return result;
#else
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
        return conv.to_bytes(wide);
    } catch (...) {
        return "";
    }
    #pragma GCC diagnostic pop
#endif
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

std::wstring NormalizeRegexBraces(const std::wstring& pattern) {
    std::wstring result = pattern;
    for (size_t i = 0; i + 1 < result.size(); ++i) {
        if (result[i] != L'{' || result[i + 1] != L',') continue;
        if (i > 0 && result[i - 1] == L'\\') continue; // escaped literal brace
        // The "{," shorthand is only auto-fixed when it closes as "{,}" or
        // "{,<digits>}" — i.e. exactly the form std::regex rejects but
        // .NET/PCRE/JS Annex-B accept as "{0,}" / "{0,<digits>}".
        size_t j = i + 2;
        while (j < result.size() && std::iswdigit(result[j])) ++j;
        if (j < result.size() && result[j] == L'}') {
            result.insert(i + 1, L"0");
            ++i; // skip the inserted '0'
        }
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
    // Test/diagnostic hook (both channels): AGENTREDACTOR_CONFIG_DIR overrides
    // the config directory so tests can seed settings fixtures against an
    // isolated directory. Unset in normal use.
    if (const wchar_t* overrideDir = _wgetenv(L"AGENTREDACTOR_CONFIG_DIR");
        overrideDir && *overrideDir) {
        return std::filesystem::path(overrideDir);
    }
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::filesystem::path(path) / L"AgentRedactor";
    }
    return std::filesystem::path(L"C:\\AgentRedactor");
#else
    // XDG: $XDG_CONFIG_HOME/agentredactor, defaulting to ~/.config/agentredactor.
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "agentredactor";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config" / "agentredactor";
    }
    return std::filesystem::path("/tmp/agentredactor");
#endif
}

std::filesystem::path GetCurrentLogFilePath() {
    if (g_logFilePath.empty()) {
        return GetAppDataPath() / L"agent_redactor.log";
    }
    return g_logFilePath;
}

std::filesystem::path GetExecutablePath() {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
#else
    char path[4096];
    ssize_t len = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0) return std::filesystem::current_path();
    path[len] = '\0';
    return std::filesystem::path(path).parent_path();
#endif
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
#ifdef _WIN32
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
#else
    // RFC 4122 v4 UUID from OS randomness.
    try {
        unsigned char b[16];
        std::random_device rd;
        for (auto& byte : b) byte = static_cast<unsigned char>(rd());
        b[6] = (b[6] & 0x0F) | 0x40;
        b[8] = (b[8] & 0x3F) | 0x80;
        std::wostringstream oss;
        oss << std::hex << std::setfill(L'0');
        for (int i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) oss << L'-';
            oss << std::setw(2) << static_cast<int>(b[i]);
        }
        return oss.str();
    } catch (...) {
        return L"uuid_" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
    }
#endif
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

#ifdef _WIN32
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
#endif

std::wstring FormatLocalizedTime(const std::time_t& time) {
    std::wstring buffer(64, L'\0');
    struct tm timeinfo;
    localtime_s(&timeinfo, &time);
#ifdef _WIN32
    SYSTEMTIME st = TmToSystemTime(timeinfo);
    int len = GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, buffer.data(), static_cast<int>(buffer.size()));
    if (len > 0) {
        buffer.resize(len - 1);
    } else {
        wcsftime(buffer.data(), buffer.size(), L"%H:%M:%S", &timeinfo);
        buffer.resize(wcslen(buffer.c_str()));
    }
#else
    wcsftime(buffer.data(), buffer.size(), L"%H:%M", &timeinfo);
    buffer.resize(wcslen(buffer.c_str()));
#endif
    return buffer;
}

std::wstring FormatLocalizedDateTime(const std::time_t& time) {
    std::wstring buffer(128, L'\0');
    struct tm timeinfo;
    localtime_s(&timeinfo, &time);
#ifdef _WIN32
    SYSTEMTIME st = TmToSystemTime(timeinfo);
    int len = GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, buffer.data(), static_cast<int>(buffer.size()), nullptr);
    if (len > 0) {
        buffer.resize(len - 1);
    } else {
        wcsftime(buffer.data(), buffer.size(), L"%Y-%m-%d", &timeinfo);
        buffer.resize(wcslen(buffer.c_str()));
    }
#else
    wcsftime(buffer.data(), buffer.size(), L"%Y-%m-%d", &timeinfo);
    buffer.resize(wcslen(buffer.c_str()));
#endif
    std::wstring timeStr = FormatLocalizedTime(time);
    return buffer + L" " + timeStr;
}

#ifdef _WIN32
namespace {

struct WinHttpHandle {
    HINTERNET h = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : h(handle) {}
    ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
};

// Reads the Content-Length header as a 64-bit value from the header string
// (the numeric DWORD query truncates files larger than 4 GB). Returns 0 when
// the header is absent.
uint64_t QueryContentLength(HINTERNET request) {
    DWORD bufSize = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER, &bufSize, WINHTTP_NO_HEADER_INDEX);
    if (bufSize == 0) return 0;
    std::wstring value(bufSize / sizeof(wchar_t) + 1, L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
        value.data(), &bufSize, WINHTTP_NO_HEADER_INDEX)) return 0;
    return _wcstoui64(value.c_str(), nullptr, 10);
}

// Performs a GET against `url`, following redirects manually (the default
// auto policy is disabled so cross-host chains like worker -> github.com ->
// release asset CDN are explicit). `extraHeaders` (e.g. a Range header) is
// re-sent on every hop of the redirect chain. On HTTP 200 or 206, invokes
// `consume` with the open request handle, the Content-Length (0 when absent)
// and the status code; its return value becomes the result.
bool HttpGetInternal(const std::wstring& url,
    const std::function<bool(HINTERNET request, uint64_t contentLength, DWORD status)>& consume,
    const std::wstring& extraHeaders = L"") {
    std::wstring current = url;
    for (int redirect = 0; redirect < 5; ++redirect) {
        URL_COMPONENTS uc = { sizeof(uc) };
        uc.dwSchemeLength = (DWORD)-1;
        uc.dwHostNameLength = (DWORD)-1;
        uc.dwUrlPathLength = (DWORD)-1;
        uc.dwExtraInfoLength = (DWORD)-1;
        if (!WinHttpCrackUrl(current.c_str(), 0, 0, &uc)) return false;

        std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
        if (uc.dwExtraInfoLength > 0) path += std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
        if (path.empty()) path = L"/";

        WinHttpHandle session{ WinHttpOpen(L"AgentRedactor/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
        if (!session.h) return false;
        WinHttpHandle connect{ WinHttpConnect(session.h, host.c_str(), uc.nPort, 0) };
        if (!connect.h) return false;
        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        WinHttpHandle request{ WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) };
        if (!request.h) return false;

        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        // Generous receive timeout: model weights and update packages are large.
        WinHttpSetTimeouts(request.h, 30000, 30000, 30000, 300000);

        const wchar_t* headers = extraHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extraHeaders.c_str();
        DWORD headersLength = extraHeaders.empty() ? 0 : static_cast<DWORD>(extraHeaders.size());
        if (!WinHttpSendRequest(request.h, headers, headersLength,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return false;
        if (!WinHttpReceiveResponse(request.h, nullptr)) return false;

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);

        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            DWORD bufSize = 0;
            WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                WINHTTP_NO_OUTPUT_BUFFER, &bufSize, WINHTTP_NO_HEADER_INDEX);
            if (bufSize == 0) return false;
            std::wstring location(bufSize / sizeof(wchar_t) + 1, L'\0');
            if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                location.data(), &bufSize, WINHTTP_NO_HEADER_INDEX)) return false;
            location.resize(wcslen(location.c_str()));
            if (location.empty()) return false;
            current = location;
            continue;
        }
        if (status != 200 && status != 206) return false;

        return consume(request.h, QueryContentLength(request.h), status);
    }
    return false;
}

} // anonymous namespace

bool HttpGetString(const std::wstring& url, std::string& outBody) {
    outBody.clear();
    return HttpGetInternal(url, [&outBody](HINTERNET request, uint64_t, DWORD) {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(request, &avail) && avail > 0) {
            std::vector<char> buffer(avail);
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), avail, &read)) return false;
            if (read > 0) outBody.append(buffer.data(), read);
        }
        return true;
    });
}

bool HttpDownloadFile(const std::wstring& url, const std::filesystem::path& destPath,
    const std::function<void(uint64_t downloaded, uint64_t total)>& progress) {
    // Resume from an existing partial file when the server honors ranges.
    uint64_t existing = 0;
    {
        std::error_code ec;
        auto size = std::filesystem::file_size(destPath, ec);
        if (!ec) existing = size;
    }
    std::wstring rangeHeader;
    if (existing > 0) rangeHeader = L"Range: bytes=" + std::to_wstring(existing) + L"-\r\n";
    return HttpGetInternal(url, [&](HINTERNET request, uint64_t contentLength, DWORD status) {
        // 206: the server honored the Range request, append. 200: it answered
        // the whole file instead (or the partial was already complete) —
        // discard the partial and restart from zero.
        const bool resuming = (existing > 0 && status == 206);
        std::ofstream file(destPath, std::ios::binary | (resuming ? std::ios::app : std::ios::trunc));
        if (!file) return false;
        const uint64_t total = resuming ? existing + contentLength : contentLength;
        uint64_t downloaded = resuming ? existing : 0;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(request, &avail) && avail > 0) {
            std::vector<char> buffer(avail);
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), avail, &read)) return false;
            if (read > 0) {
                file.write(buffer.data(), read);
                downloaded += read;
                if (progress) progress(downloaded, total);
            }
        }
        file.flush();
        // A cleanly closed connection ends the loop above early; a short file
        // is a failed download, not a success.
        return file.good() && (total == 0 || downloaded == total);
    }, rangeHeader);
}

bool HttpDownloadFileSegmented(const std::wstring& url, const std::filesystem::path& destPath,
    const std::function<void(uint64_t downloaded, uint64_t total)>& progress,
    size_t maxSegments) {
    constexpr uint64_t kMinSegmentedBytes = 64ull * 1024 * 1024;

    // Probe the total size and range support before committing to segments.
    uint64_t totalSize = 0;
    HttpGetInternal(url, [&](HINTERNET, uint64_t contentLength, DWORD) {
        totalSize = contentLength;
        return true;
    });
    bool rangesSupported = false;
    if (totalSize > 0) {
        HttpGetInternal(url, [&](HINTERNET, uint64_t, DWORD status) {
            rangesSupported = (status == 206);
            return true;
        }, L"Range: bytes=0-0\r\n");
    }

    size_t segmentCount = static_cast<size_t>(std::min<uint64_t>(maxSegments, totalSize / kMinSegmentedBytes));
    if (!rangesSupported || segmentCount < 2) {
        LOGF_LIFECYCLE(L"[Utils] Segmented download: single-stream fallback for %s (ranges %s, size %llu)",
            url.c_str(), rangesSupported ? L"supported" : L"unsupported",
            static_cast<unsigned long long>(totalSize));
        return HttpDownloadFile(url, destPath, progress);
    }

    LOGF_LIFECYCLE(L"[Utils] Segmented download: %llu bytes in %zu segments from %s",
        static_cast<unsigned long long>(totalSize), segmentCount, url.c_str());

    const uint64_t segmentSize = totalSize / segmentCount;
    std::vector<std::filesystem::path> partPaths(segmentCount);
    for (size_t i = 0; i < segmentCount; ++i) {
        partPaths[i] = destPath;
        partPaths[i] += L".part" + std::to_wstring(i);
    }

    std::atomic<uint64_t> totalDownloaded{ 0 };
    std::mutex progressMutex;
    auto reportProgress = [&](uint64_t downloaded) {
        if (progress) {
            std::lock_guard lock(progressMutex);
            progress(downloaded, totalSize);
        }
    };

    // One thread per segment, each on its own WinHTTP connection with an
    // explicit Range header (redirects are followed per segment, like
    // HttpGetInternal does for single-stream downloads).
    std::vector<char> results(segmentCount, 0);
    std::vector<std::thread> threads;
    threads.reserve(segmentCount);
    try {
        for (size_t i = 0; i < segmentCount; ++i) {
            threads.emplace_back([&, i] {
                try {
                    const uint64_t begin = i * segmentSize;
                    const uint64_t end = (i + 1 == segmentCount) ? totalSize - 1 : begin + segmentSize - 1;
                    const uint64_t expected = end - begin + 1;
                    const auto& partPath = partPaths[i];

                    // Resume a partially downloaded segment. A complete part
                    // needs no request; an oversized one is corrupt.
                    uint64_t existing = 0;
                    {
                        std::error_code ec;
                        auto size = std::filesystem::file_size(partPath, ec);
                        if (!ec) existing = size;
                    }
                    if (existing == expected) {
                        reportProgress(totalDownloaded.fetch_add(expected) + expected);
                        results[i] = 1;
                        return;
                    }
                    if (existing > expected) {
                        std::error_code ec;
                        std::filesystem::remove(partPath, ec);
                        existing = 0;
                    }

                    totalDownloaded.fetch_add(existing);
                    const std::wstring rangeHeader = L"Range: bytes=" + std::to_wstring(begin + existing) +
                        L"-" + std::to_wstring(end) + L"\r\n";
                    results[i] = HttpGetInternal(url, [&](HINTERNET request, uint64_t, DWORD status) {
                        if (status != 206) return false;  // server ignored the Range header
                        std::ofstream file(partPath, std::ios::binary | (existing > 0 ? std::ios::app : std::ios::trunc));
                        if (!file) return false;
                        uint64_t have = existing;
                        while (have < expected) {
                            DWORD avail = 0;
                            if (!WinHttpQueryDataAvailable(request, &avail)) return false;
                            if (avail == 0) break;
                            DWORD chunk = static_cast<DWORD>(std::min<uint64_t>(avail, expected - have));
                            std::vector<char> buffer(chunk);
                            DWORD read = 0;
                            if (!WinHttpReadData(request, buffer.data(), chunk, &read)) return false;
                            if (read == 0) break;
                            file.write(buffer.data(), read);
                            have += read;
                            reportProgress(totalDownloaded.fetch_add(read) + read);
                        }
                        file.flush();
                        return file.good() && have == expected;
                    }, rangeHeader) ? 1 : 0;
                } catch (...) {
                    results[i] = 0;
                }
            });
        }
    } catch (...) {
        for (auto& t : threads) if (t.joinable()) t.join();
        return false;
    }
    for (auto& t : threads) t.join();
    for (size_t i = 0; i < segmentCount; ++i) {
        if (!results[i]) {
            // Part files are kept so the next retry resumes each segment.
            LOGF_LIFECYCLE(L"[Utils] Segmented download: segment %zu failed for %s", i, url.c_str());
            return false;
        }
    }

    // Concatenate the segments in order and verify the final size. On any
    // failure the part files survive for the next retry.
    {
        std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        std::vector<char> buffer(1024 * 1024);
        for (size_t i = 0; i < segmentCount; ++i) {
            std::ifstream in(partPaths[i], std::ios::binary);
            if (!in) return false;
            while (in) {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                auto got = in.gcount();
                if (got > 0) out.write(buffer.data(), got);
            }
        }
        out.flush();
        if (!out.good()) return false;
    }
    std::error_code ec;
    auto finalSize = std::filesystem::file_size(destPath, ec);
    if (ec || finalSize != totalSize) {
        LOGF_LIFECYCLE(L"[Utils] Segmented download: size mismatch after concat for %s", url.c_str());
        std::filesystem::remove(destPath, ec);
        return false;
    }
    for (const auto& partPath : partPaths) std::filesystem::remove(partPath, ec);
    reportProgress(totalSize);
    LOG_LIFECYCLE(L"[Utils] Segmented download complete");
    return true;
}

#else // POSIX: libcurl implementations of the same three helpers

namespace {

// Result of the headers callback: fail the request, continue receiving the
// body, or stop here successfully (headers-only probe — mirrors the WinHTTP
// code closing the request handle right after the consume callback returns).
enum class HeadersAction { Fail, Proceed, HeadersOnly };

struct CurlGetContext {
    CURL* curl = nullptr;
    std::function<HeadersAction(long status, uint64_t contentLength)> onHeaders;
    std::function<bool(const char* data, size_t len)> onData;
    std::string location;
    HeadersAction action = HeadersAction::Proceed;
};

static bool AsciiStartsWithNoCase(const std::string& s, const char* prefix) {
    for (size_t i = 0; prefix[i]; ++i) {
        if (i >= s.size() || tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) return false;
    }
    return true;
}

size_t CurlHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    const size_t len = size * nitems;
    auto* ctx = static_cast<CurlGetContext*>(userdata);
    const std::string line(buffer, len);
    if (AsciiStartsWithNoCase(line, "Location:")) {
        std::string value = line.substr(9);
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);
        ctx->location = value;
        return len;
    }
    if (line == "\r\n" || line == "\n") {
        // End of the header block. Redirect responses are handled by the
        // caller after perform; only invoke onHeaders for the final status.
        long status = 0;
        curl_off_t contentLength = -1;
        curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &status);
        if (status >= 300 && status < 400) return len;
        curl_easy_getinfo(ctx->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &contentLength);
        ctx->action = ctx->onHeaders(status, contentLength > 0 ? static_cast<uint64_t>(contentLength) : 0);
        if (ctx->action != HeadersAction::Proceed) return 0; // abort transfer
    }
    return len;
}

size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t len = size * nmemb;
    auto* ctx = static_cast<CurlGetContext*>(userdata);
    return ctx->onData(ptr, len) ? len : 0;
}

// Performs a GET against `url`, following redirects manually (the default
// auto policy is disabled so cross-host chains like worker -> github.com ->
// release asset CDN are explicit). `extraHeaders` (e.g. a Range header) is
// re-sent on every hop of the redirect chain. onHeaders decides whether the
// body is consumed; onData feeds body bytes. Never throws.
bool CurlGet(const std::wstring& url, const std::wstring& extraHeaders,
    const std::function<HeadersAction(long status, uint64_t contentLength)>& onHeaders,
    const std::function<bool(const char* data, size_t len)>& onData) {
    std::string current = WideToUtf8(url);
    for (int redirect = 0; redirect < 5; ++redirect) {
        CURL* curl = curl_easy_init();
        if (!curl) return false;
        CurlGetContext ctx;
        ctx.curl = curl;
        ctx.onHeaders = onHeaders;
        ctx.onData = onData;
        curl_easy_setopt(curl, CURLOPT_URL, current.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "AgentRedactor/1.0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
        // WinHTTP-equivalent timeouts: 30 s connect; abort when the transfer
        // stalls below 1 byte/s for 300 s (receive timeout for large files).
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 30000L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 300L);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, CurlHeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
        struct curl_slist* headerList = nullptr;
        if (!extraHeaders.empty()) {
            for (const auto& h : Split(extraHeaders, L'\n')) {
                const std::string narrow = WideToUtf8(Trim(h));
                if (!narrow.empty()) headerList = curl_slist_append(headerList, narrow.c_str());
            }
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
        }
        CURLcode res = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);

        if (ctx.action == HeadersAction::Fail) return false;
        if (ctx.action == HeadersAction::HeadersOnly) return true;
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (ctx.location.empty()) return false;
            current = ctx.location;
            continue;
        }
        if (status != 200 && status != 206) return false;
        return res == CURLE_OK;
    }
    return false;
}

} // anonymous namespace

bool HttpGetString(const std::wstring& url, std::string& outBody) {
    outBody.clear();
    return CurlGet(url, L"",
        [](long status, uint64_t) {
            return (status == 200 || status == 206) ? HeadersAction::Proceed : HeadersAction::Fail;
        },
        [&outBody](const char* data, size_t len) {
            outBody.append(data, len);
            return true;
        });
}

bool HttpDownloadFile(const std::wstring& url, const std::filesystem::path& destPath,
    const std::function<void(uint64_t downloaded, uint64_t total)>& progress) {
    // Resume from an existing partial file when the server honors ranges.
    uint64_t existing = 0;
    {
        std::error_code ec;
        auto size = std::filesystem::file_size(destPath, ec);
        if (!ec) existing = size;
    }
    std::wstring rangeHeader;
    if (existing > 0) rangeHeader = L"Range: bytes=" + std::to_wstring(existing) + L"-\r\n";

    struct State {
        std::ofstream file;
        uint64_t total = 0;
        uint64_t downloaded = 0;
        bool resuming = false;
    } st;
    const bool transportOk = CurlGet(url, rangeHeader,
        [&](long status, uint64_t contentLength) {
            if (status != 200 && status != 206) return HeadersAction::Fail;
            // 206: the server honored the Range request, append. 200: it
            // answered the whole file instead — discard the partial and
            // restart from zero.
            st.resuming = (existing > 0 && status == 206);
            st.file.open(destPath, std::ios::binary | (st.resuming ? std::ios::app : std::ios::trunc));
            if (!st.file) return HeadersAction::Fail;
            st.total = st.resuming ? existing + contentLength : contentLength;
            st.downloaded = st.resuming ? existing : 0;
            return HeadersAction::Proceed;
        },
        [&](const char* data, size_t len) {
            st.file.write(data, static_cast<std::streamsize>(len));
            st.downloaded += len;
            if (progress) progress(st.downloaded, st.total);
            return st.file.good();
        });
    if (st.file.is_open()) st.file.flush();
    // A cleanly closed connection ends the transfer early; a short file is a
    // failed download, not a success.
    return transportOk && st.file.good() && (st.total == 0 || st.downloaded == st.total);
}

bool HttpDownloadFileSegmented(const std::wstring& url, const std::filesystem::path& destPath,
    const std::function<void(uint64_t downloaded, uint64_t total)>& progress,
    size_t maxSegments) {
    constexpr uint64_t kMinSegmentedBytes = 64ull * 1024 * 1024;

    // Probe the total size and range support before committing to segments.
    uint64_t totalSize = 0;
    CurlGet(url, L"",
        [&](long status, uint64_t contentLength) {
            if (status != 200 && status != 206) return HeadersAction::Fail;
            totalSize = contentLength;
            return HeadersAction::HeadersOnly;
        },
        [](const char*, size_t) { return true; });
    bool rangesSupported = false;
    if (totalSize > 0) {
        CurlGet(url, L"Range: bytes=0-0\r\n",
            [&](long status, uint64_t) {
                rangesSupported = (status == 206);
                return HeadersAction::HeadersOnly;
            },
            [](const char*, size_t) { return true; });
    }

    size_t segmentCount = static_cast<size_t>(std::min<uint64_t>(maxSegments, totalSize / kMinSegmentedBytes));
    if (!rangesSupported || segmentCount < 2) {
        LOGF_LIFECYCLE(L"[Utils] Segmented download: single-stream fallback for %s (ranges %s, size %llu)",
            url.c_str(), rangesSupported ? L"supported" : L"unsupported",
            static_cast<unsigned long long>(totalSize));
        return HttpDownloadFile(url, destPath, progress);
    }

    LOGF_LIFECYCLE(L"[Utils] Segmented download: %llu bytes in %zu segments from %s",
        static_cast<unsigned long long>(totalSize), segmentCount, url.c_str());

    const uint64_t segmentSize = totalSize / segmentCount;
    std::vector<std::filesystem::path> partPaths(segmentCount);
    for (size_t i = 0; i < segmentCount; ++i) {
        partPaths[i] = destPath;
        partPaths[i] += L".part" + std::to_wstring(i);
    }

    std::atomic<uint64_t> totalDownloaded{ 0 };
    std::mutex progressMutex;
    auto reportProgress = [&](uint64_t downloaded) {
        if (progress) {
            std::lock_guard lock(progressMutex);
            progress(downloaded, totalSize);
        }
    };

    // One thread per segment, each on its own curl connection with an
    // explicit Range header (redirects are followed per segment, like
    // CurlGet does for single-stream downloads).
    std::vector<char> results(segmentCount, 0);
    std::vector<std::thread> threads;
    threads.reserve(segmentCount);
    try {
        for (size_t i = 0; i < segmentCount; ++i) {
            threads.emplace_back([&, i] {
                try {
                    const uint64_t begin = i * segmentSize;
                    const uint64_t end = (i + 1 == segmentCount) ? totalSize - 1 : begin + segmentSize - 1;
                    const uint64_t expected = end - begin + 1;
                    const auto& partPath = partPaths[i];

                    // Resume a partially downloaded segment. A complete part
                    // needs no request; an oversized one is corrupt.
                    uint64_t existing = 0;
                    {
                        std::error_code ec;
                        auto size = std::filesystem::file_size(partPath, ec);
                        if (!ec) existing = size;
                    }
                    if (existing == expected) {
                        reportProgress(totalDownloaded.fetch_add(expected) + expected);
                        results[i] = 1;
                        return;
                    }
                    if (existing > expected) {
                        std::error_code ec;
                        std::filesystem::remove(partPath, ec);
                        existing = 0;
                    }

                    totalDownloaded.fetch_add(existing);
                    const std::wstring rangeHeader = L"Range: bytes=" + std::to_wstring(begin + existing) +
                        L"-" + std::to_wstring(end) + L"\r\n";
                    uint64_t have = existing;
                    std::ofstream file;
                    results[i] = CurlGet(url, rangeHeader,
                        [&](long status, uint64_t) {
                            if (status != 206) return HeadersAction::Fail; // server ignored the Range header
                            file.open(partPath, std::ios::binary | (existing > 0 ? std::ios::app : std::ios::trunc));
                            return file ? HeadersAction::Proceed : HeadersAction::Fail;
                        },
                        [&](const char* data, size_t len) {
                            if (have >= expected) return false;
                            const size_t chunk = static_cast<size_t>(std::min<uint64_t>(len, expected - have));
                            file.write(data, static_cast<std::streamsize>(chunk));
                            have += chunk;
                            reportProgress(totalDownloaded.fetch_add(chunk) + chunk);
                            return file.good() && have <= expected;
                        }) ? 1 : 0;
                    file.flush();
                    if (results[i] && (!file.good() || have != expected)) results[i] = 0;
                } catch (...) {
                    results[i] = 0;
                }
            });
        }
    } catch (...) {
        for (auto& t : threads) if (t.joinable()) t.join();
        return false;
    }
    for (auto& t : threads) t.join();
    for (size_t i = 0; i < segmentCount; ++i) {
        if (!results[i]) {
            // Part files are kept so the next retry resumes each segment.
            LOGF_LIFECYCLE(L"[Utils] Segmented download: segment %zu failed for %s", i, url.c_str());
            return false;
        }
    }

    // Concatenate the segments in order and verify the final size. On any
    // failure the part files survive for the next retry.
    {
        std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        std::vector<char> buffer(1024 * 1024);
        for (size_t i = 0; i < segmentCount; ++i) {
            std::ifstream in(partPaths[i], std::ios::binary);
            if (!in) return false;
            while (in) {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                auto got = in.gcount();
                if (got > 0) out.write(buffer.data(), got);
            }
        }
        out.flush();
        if (!out.good()) return false;
    }
    std::error_code ec;
    auto finalSize = std::filesystem::file_size(destPath, ec);
    if (ec || finalSize != totalSize) {
        LOGF_LIFECYCLE(L"[Utils] Segmented download: size mismatch after concat for %s", url.c_str());
        std::filesystem::remove(destPath, ec);
        return false;
    }
    for (const auto& partPath : partPaths) std::filesystem::remove(partPath, ec);
    reportProgress(totalSize);
    LOG_LIFECYCLE(L"[Utils] Segmented download complete");
    return true;
}

#endif // _WIN32

} // namespace Utils
} // namespace AgentRedactor
