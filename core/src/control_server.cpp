#include "control_server.h"
#include "utils.h"
#include "logging.h"
#include <bcrypt.h>
#include <sddl.h>
#include <aclapi.h>

#pragma comment(lib, "bcrypt.lib")

namespace AgentRedactor {

ControlServer::~ControlServer() {
    Stop();
}

std::filesystem::path ControlServer::ControlFilePath(const std::filesystem::path& configDir) {
    return configDir / L"control.json";
}

bool ControlServer::Start(int preferredPort, const std::filesystem::path& configDir, RouteHandler handler) {
    if (server_.IsRunning()) return false;
    routeHandler_ = std::move(handler);
    token_ = GenerateToken();
    if (token_.empty()) {
        LOG(L"[ControlServer] Failed to generate auth token");
        return false;
    }

    // Frontends poll /status every second; keep that chatter out of the logs.
    server_.SetQuiet(true);

    auto boundHandler = [this](const HttpRequest& req) -> HttpResponse {
        return this->HandleRequest(req);
    };

    if (!server_.Start(preferredPort, boundHandler, /*loopbackOnly=*/true)) {
        LOGF(L"[ControlServer] Preferred port %d unavailable, falling back to ephemeral", preferredPort);
        if (!server_.Start(0, boundHandler, /*loopbackOnly=*/true)) {
            LOG(L"[ControlServer] Failed to start control API listener");
            return false;
        }
    }
    port_ = server_.GetPort();

    if (!WriteControlFile(ControlFilePath(configDir))) {
        LOG(L"[ControlServer] Failed to write control.json");
        server_.Stop();
        port_ = 0;
        return false;
    }

    LOGF_LIFECYCLE(L"[ControlServer] Control API listening on 127.0.0.1:%d", port_);
    return true;
}

void ControlServer::Stop() {
    server_.Stop();
    port_ = 0;
}

bool ControlServer::IsRunning() const {
    return server_.IsRunning();
}

HttpResponse ControlServer::HandleRequest(const HttpRequest& request) {
    std::wstring expected = L"Bearer " + token_;
    bool authorized = false;
    for (const auto& [name, value] : request.headers) {
        if (Utils::ToLower(name) == L"authorization" && value == expected) {
            authorized = true;
            break;
        }
    }
    if (!authorized) {
        HttpResponse resp;
        resp.statusCode = 401;
        resp.headers[L"Content-Type"] = L"application/json";
        resp.body = "{\"error\": \"unauthorized\"}";
        return resp;
    }
    if (routeHandler_) {
        return routeHandler_(request);
    }
    HttpResponse resp;
    resp.statusCode = 404;
    resp.headers[L"Content-Type"] = L"application/json";
    resp.body = "{\"error\": \"not found\"}";
    return resp;
}

std::wstring ControlServer::GenerateToken() const {
    unsigned char bytes[16] = {};
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return L"";
    }
    static const wchar_t hex[] = L"0123456789abcdef";
    std::wstring token;
    token.reserve(sizeof(bytes) * 2);
    for (unsigned char b : bytes) {
        token += hex[b >> 4];
        token += hex[b & 0x0F];
    }
    return token;
}

bool ControlServer::WriteControlFile(const std::filesystem::path& path) const {
    // Build a security descriptor that grants full control to the current
    // user only (D:P = protected DACL, no inheritance).
    std::wstring sddl = L"D:P(A;;FA;;;";
    HANDLE tokenHandle = nullptr;
    PTOKEN_USER tokenUser = nullptr;
    LPWSTR sidString = nullptr;
    bool ok = false;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tokenHandle)) {
        DWORD needed = 0;
        GetTokenInformation(tokenHandle, TokenUser, nullptr, 0, &needed);
        if (needed > 0) {
            tokenUser = (PTOKEN_USER)LocalAlloc(LPTR, needed);
            if (tokenUser && GetTokenInformation(tokenHandle, TokenUser, tokenUser, needed, &needed)) {
                ConvertSidToStringSidW(tokenUser->User.Sid, &sidString);
            }
        }
    }
    if (sidString) {
        sddl += sidString;
        sddl += L")";

        PSECURITY_DESCRIPTOR sd = nullptr;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
            SECURITY_ATTRIBUTES sa = { sizeof(sa) };
            sa.lpSecurityDescriptor = sd;
            sa.bInheritHandle = FALSE;

            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, &sa,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                std::string content = "{\"port\": " + std::to_string(port_) +
                    ", \"token\": \"" + Utils::WideToUtf8(token_) + "\"" +
                    ", \"pid\": " + std::to_string(GetCurrentProcessId()) + "}\n";
                DWORD written = 0;
                ok = WriteFile(file, content.data(), (DWORD)content.size(), &written, nullptr) &&
                    written == content.size();
                CloseHandle(file);
            }
            LocalFree(sd);
        }
        LocalFree(sidString);
    }
    if (tokenUser) LocalFree(tokenUser);
    if (tokenHandle) CloseHandle(tokenHandle);
    return ok;
}

} // namespace AgentRedactor
