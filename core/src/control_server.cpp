#include "control_server.h"
#include "utils.h"
#include "logging.h"
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <bcrypt.h>
#include <sddl.h>
#include <aclapi.h>

#pragma comment(lib, "bcrypt.lib")
#else
#include <openssl/rand.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

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

    // Self-check BEFORE advertising the port. Binding can succeed while another
    // process still owns the address (historically on Windows, where
    // SO_REUSEADDR allowed a second bind to the same 127.0.0.1:port). If that
    // happened, connections go to the OTHER process, which does not know our
    // token, so the GUI would see 401 on everything and present it as "no
    // profiles / cannot save". Writing control.json in that state is worse than
    // not starting: it points the GUI at a socket we do not serve.
    //
    // A real request to ourselves proves ownership: only our own listener can
    // answer with our freshly generated token.
    //
    // The probe is retried briefly: the listener thread was just spawned, and
    // on a heavily loaded machine the first connect can complete before the
    // accept loop has been scheduled. Only a PERSISTENT failure (the signature
    // of a foreign process serving the port) refuses startup, so a slow start
    // can never misfire a guarantee this strict.
    bool selfOk = VerifySelfServing();
    for (int attempt = 1; !selfOk && attempt < 3; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        selfOk = VerifySelfServing();
    }
    if (!selfOk) {
        LOGF(L"[ControlServer] Control API on 127.0.0.1:%d is not served by this "
             L"process (another instance owns the port); refusing to advertise it", port_);
        server_.Stop();
        port_ = 0;
        return false;
    }

    if (!WriteControlFile(ControlFilePath(configDir))) {
        LOG(L"[ControlServer] Failed to write control.json");
        server_.Stop();
        port_ = 0;
        return false;
    }

    LOGF_LIFECYCLE(L"[ControlServer] Control API listening on 127.0.0.1:%d", port_);
    return true;
}

// Issues one authenticated GET /status against our own bound port and checks
// that this process answers it. Uses a plain blocking socket with short
// timeouts so Start() cannot hang; any failure (refused, timeout, 401, or a
// non-200) means we do not own the advertised port.
bool ControlServer::VerifySelfServing() const {
#ifdef _WIN32
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;
    auto closeSock = [&]() { ::closesocket(sock); };
    DWORD timeoutMs = 2000;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));
    ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));
#else
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;
    auto closeSock = [&]() { ::closesocket(sock); };
    timeval tv{};
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(port_));

    if (::connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closeSock();
        return false;
    }

    std::string token = Utils::WideToUtf8(token_);
    std::string request =
        "GET /status HTTP/1.1\r\n"
        "Host: 127.0.0.1:" + std::to_string(port_) + "\r\n"
        "Authorization: Bearer " + token + "\r\n"
        "Connection: close\r\n\r\n";
    if (::send(sock, request.data(), (int)request.size(), 0) <= 0) {
        closeSock();
        return false;
    }

    std::string response;
    char buf[512];
    for (;;) {
        int received = ::recv(sock, buf, sizeof(buf), 0);
        if (received <= 0) break;
        response.append(buf, received);
        if (response.size() > 8192) break;  // /status is small; do not over-read
    }
    closeSock();

    // "HTTP/1.1 200" proves both that someone answered AND that it accepted our
    // token. A 401 here is the exact stale-engine signature: a foreign process
    // holds the port and rejects the token we are about to write to disk.
    return response.rfind("HTTP/1.1 200", 0) == 0;
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
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return L"";
    }
#else
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        return L"";
    }
#endif
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
#ifdef _WIN32
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
#else
    // Owner-only file (0600), replacing the Windows per-user ACL. umask may
    // widen nothing here: open() with 0600 then fchmod to strip any umask
    // bits is unnecessary (umask only removes bits), so 0600 is guaranteed.
    std::string content = "{\"port\": " + std::to_string(port_) +
        ", \"token\": \"" + Utils::WideToUtf8(token_) + "\"" +
        ", \"pid\": " + std::to_string(getpid()) + "}\n";
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    bool ok = true;
    size_t written = 0;
    while (written < content.size()) {
        ssize_t n = write(fd, content.data() + written, content.size() - written);
        if (n <= 0) { ok = false; break; }
        written += static_cast<size_t>(n);
    }
    close(fd);
    return ok;
#endif
}

} // namespace AgentRedactor
