#include "utils.h"
#include <windows.h>
#include <shlobj.h>
#include <winhttp.h>
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

} // namespace Utils
} // namespace AgentRedactor
