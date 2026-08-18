#include "control_api_client.h"
#include "hello_unlock.h"
#include "utils.h"
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

using namespace AgentRedactor;

namespace {

// The Windows Hello consent dialog is created by the ENGINE (a background
// process), which can never attach it to the console window (conhost owns
// the console), so the unowned system dialog would otherwise pop up behind
// other windows. Bring the console to the foreground first so the dialog
// appears in front of the user; flash the taskbar entry as a fallback when
// the foreground lock refuses us.
void ActivateConsoleWindow() {
    HWND console = ::GetConsoleWindow();
    if (!console) return;
    if (::IsIconic(console)) ::ShowWindow(console, SW_RESTORE);
    ::ShowWindow(console, SW_SHOW);
    if (!::SetForegroundWindow(console)) {
        FLASHWINFO fi{};
        fi.cbSize = sizeof(fi);
        fi.hwnd = console;
        fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
        fi.uCount = 0;
        ::FlashWindowEx(&fi);
    }
}

} // namespace

bool ControlApiClient::Connect(const std::filesystem::path& configDir) {
    port_ = 0;
    token_.clear();

    auto content = Utils::ReadFileAsString(configDir / L"control.json");
    if (!content) return false;
    try {
        json j = json::parse(Utils::WideToUtf8(*content));
        port_ = j.at("port").get<int>();
        token_ = Utils::Utf8ToWide(j.at("token").get<std::string>());
        enginePid_ = j.value("pid", 0UL);
    } catch (...) {
        port_ = 0;
        token_.clear();
        enginePid_ = 0;
        return false;
    }
    return IsConnected();
}

bool ControlApiClient::Get(const std::wstring& path, json& out) const {
    long status = 0;
    std::string body;
    if (!Request(L"GET", path, nullptr, status, body) || status != 200) return false;
    try {
        out = json::parse(body);
    } catch (...) {
        return false;
    }
    return true;
}

bool ControlApiClient::Post(const std::wstring& path, const json& body, json* out) const {
    std::string payload = body.dump();
    long status = 0;
    std::string respBody;
    if (!Request(L"POST", path, &payload, status, respBody) || status != 200) return false;
    if (out) {
        try {
            *out = json::parse(respBody);
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool ControlApiClient::Put(const std::wstring& path, const json& body, json* out) const {
    std::string payload = body.dump();
    long status = 0;
    std::string respBody;
    if (!Request(L"PUT", path, &payload, status, respBody) || status != 200) return false;
    if (out) {
        try {
            *out = json::parse(respBody);
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool ControlApiClient::Delete(const std::wstring& path) const {
    long status = 0;
    std::string body;
    return Request(L"DELETE", path, nullptr, status, body) && status == 200;
}

HelloConsentOutcome ControlApiClient::ConsentWithHello() const {
    // The consent runs IN-PROCESS here, in the CLI process: when the user runs
    // a command from a terminal, THIS process is the active application, so
    // the Windows Security dialog (attached to a window this process owns, see
    // hello_unlock.cpp) comes to the foreground. An engine-owned prompt would
    // belong to a background process and always land in the background.
    // Bring the console forward first so the dialog appears over it.
    if (HWND console = ::GetConsoleWindow()) {
        if (::IsIconic(console)) ::ShowWindow(console, SW_RESTORE);
        ::ShowWindow(console, SW_SHOW);
        ::SetForegroundWindow(console);
    }
    switch (RequestHelloUnlock(nullptr, L"Unlock Agent Redactor with Windows Hello")) {
    case HelloUnlockResult::Verified: {
        // The user verified in-process: unlock the engine session via the
        // trusted "caller already verified" endpoint (the GUI does the same
        // after its in-process prompt).
        json out;
        if (!Post(L"/unlock", json::object(), &out)) return HelloConsentOutcome::Failed;
        return out.value("ok", false) ? HelloConsentOutcome::Granted : HelloConsentOutcome::Failed;
    }
    case HelloUnlockResult::Canceled:
        return HelloConsentOutcome::Canceled;
    case HelloUnlockResult::RetriesExhausted:
        return HelloConsentOutcome::RetriesExhausted;
    case HelloUnlockResult::TimedOut:
        return HelloConsentOutcome::TimedOut;
    case HelloUnlockResult::Unavailable:
        return HelloConsentOutcome::Unavailable;
    default:
        return HelloConsentOutcome::Failed;
    }
}

bool ControlApiClient::Request(const std::wstring& method, const std::wstring& path,
    const std::string* body, long& statusCode, std::string& responseBody) const {
    statusCode = 0;
    responseBody.clear();
    if (!IsConnected()) return false;

    bool ok = false;
    HINTERNET hSession = WinHttpOpen(L"AgentRedactor-CLI/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    // Short timeouts: the engine is localhost; a hang means it is not running.
    // /unlock/hello, /hello/verify and /settings/disableMasterPassword hold the
    // request while the user answers the Windows Hello consent prompt (the
    // engine cancels it after 60s), so they get a long receive timeout.
    if (path == L"/unlock/hello" || path == L"/hello/verify" || path == L"/settings/disableMasterPassword") {
        WinHttpSetTimeouts(hSession, 1500, 1500, 3000, 90000);
        // Foreground the console so the engine's consent dialog is visible,
        // and lift the foreground lock for the engine process (its dialog is
        // created from a background process).
        ActivateConsoleWindow();
        if (enginePid_ != 0) ::AllowSetForegroundWindow(enginePid_);
    } else {
        WinHttpSetTimeouts(hSession, 1500, 1500, 3000, 3000);
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)port_, 0);
    if (hConnect) {
        // Tag the hello consent calls (incl. the gated disable) with this
        // process's console HWND so the engine can own the Windows Hello
        // prompt on our window (foreground where possible).
        std::wstring effPath = path;
        if (path == L"/unlock/hello" || path == L"/hello/verify" || path == L"/settings/disableMasterPassword") {
            effPath += L"?hwnd=" + std::to_wstring(reinterpret_cast<uintptr_t>(::GetConsoleWindow()));
        }
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, method.c_str(), effPath.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hRequest) {
            std::wstring headers = L"Authorization: Bearer " + token_ + L"\r\nContent-Type: application/json";
            LPVOID bodyPtr = body ? (LPVOID)body->data() : WINHTTP_NO_REQUEST_DATA;
            DWORD bodyLen = body ? (DWORD)body->size() : 0;
            if (WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.size(),
                    bodyPtr, bodyLen, bodyLen, 0) &&
                WinHttpReceiveResponse(hRequest, nullptr)) {
                DWORD code = 0;
                DWORD codeSize = sizeof(code);
                WinHttpQueryHeaders(hRequest,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX);
                statusCode = (long)code;
                for (;;) {
                    DWORD available = 0;
                    if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
                    if (available == 0) { ok = true; break; }
                    std::string chunk(available, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(hRequest, chunk.data(), available, &read)) break;
                    chunk.resize(read);
                    responseBody += chunk;
                }
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return ok;
}
