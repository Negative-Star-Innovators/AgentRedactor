#include "control_api_client.h"
#include "utils.h"
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

using namespace AgentRedactor;

bool ControlApiClient::Connect(const std::filesystem::path& configDir) {
    port_ = 0;
    token_.clear();

    auto content = Utils::ReadFileAsString(configDir / L"control.json");
    if (!content) return false;
    try {
        json j = json::parse(Utils::WideToUtf8(*content));
        port_ = j.at("port").get<int>();
        token_ = Utils::Utf8ToWide(j.at("token").get<std::string>());
    } catch (...) {
        port_ = 0;
        token_.clear();
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
    // /unlock/hello and /hello/verify hold the request while the user answers
    // the Windows Hello consent prompt (the engine cancels it after 60s), so
    // they get a long receive timeout.
    if (path == L"/unlock/hello" || path == L"/hello/verify") {
        WinHttpSetTimeouts(hSession, 1500, 1500, 3000, 90000);
    } else {
        WinHttpSetTimeouts(hSession, 1500, 1500, 3000, 3000);
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)port_, 0);
    if (hConnect) {
        // Tag the hello consent calls with this process's console HWND so the
        // engine can own the Windows Hello prompt on our window (foreground).
        std::wstring effPath = path;
        if (path == L"/unlock/hello" || path == L"/hello/verify") {
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
