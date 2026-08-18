#include "http_server.h"
#include "utils.h"
#include "logging.h"
#include "log_manager.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace AgentRedactor {

HttpServer::HttpServer() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

HttpServer::~HttpServer() {
    Stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool HttpServer::Start(int port, std::function<HttpResponse(const HttpRequest&)> handler, bool loopbackOnly) {
    if (running_.load()) return false;
    requestHandler_ = handler;
    port_ = port;

    int addrLen = 0;
    sockaddr_in6 addr6 = {};
    sockaddr_in addr4 = {};
    sockaddr* bindAddr = nullptr;
    if (loopbackOnly) {
        listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        addr4.sin_family = AF_INET;
        addr4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr4.sin_port = htons(static_cast<u_short>(port));
        bindAddr = (sockaddr*)&addr4;
        addrLen = sizeof(addr4);
    } else {
        listenSocket_ = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(static_cast<u_short>(port));
        bindAddr = (sockaddr*)&addr6;
        addrLen = sizeof(addr6);
    }
    if (listenSocket_ == INVALID_SOCKET) {
        LOG(L"[HttpServer] Failed to create socket");
        return false;
    }

    int reuse = 1;
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    if (!loopbackOnly) {
        // Dual-stack: accept both IPv4 and IPv6 connections on one socket
        int v6only = 0;
        setsockopt(listenSocket_, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v6only, sizeof(v6only));
    }

    if (bind(listenSocket_, bindAddr, addrLen) == SOCKET_ERROR) {
        LOG(L"[HttpServer] Failed to bind to port " + std::to_wstring(port));
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        return false;
    }

    // Resolve the actual bound port (relevant when port 0 was requested).
    {
        sockaddr_in6 bound = {};
        ar_socklen_t boundLen = sizeof(bound);
        if (getsockname(listenSocket_, (sockaddr*)&bound, &boundLen) == 0) {
            port_ = ntohs(bound.sin6_port);
        }
    }

    if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
        LOG(L"[HttpServer] Failed to listen");
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        return false;
    }

    running_ = true;
    shouldStop_ = false;
    listenerThread_ = std::thread(&HttpServer::RunListener, this);
    LOGF(L"[HttpServer] Listening on port %d", port);
    return true;
}

void HttpServer::Stop() {
    if (!running_.load()) return;
    shouldStop_ = true;
    running_ = false;

    if (listenSocket_ != INVALID_SOCKET) {
        // Wake the listener's select() so it can exit; the socket is only
        // closed AFTER the thread is joined. Closing it first races with
        // FD_SET on POSIX (glibc aborts on the invalidated descriptor).
        shutdown(listenSocket_, SD_BOTH);
    }

    if (listenerThread_.joinable()) {
        listenerThread_.join();
    }

    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }

    // Wait for all client connections to finish
    std::unique_lock<std::mutex> lock(stopMutex_);
    stopCv_.wait(lock, [this]() { return activeConnections_.load() == 0; });

    LOG(L"[HttpServer] Stopped");
}

void HttpServer::RunListener() {
    while (!shouldStop_.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket_, &readSet);
        timeval tv = {0, 100000}; // 100ms timeout
        // nfds is ignored on Windows; POSIX requires the highest fd + 1.
        int selectResult = select(static_cast<int>(listenSocket_) + 1, &readSet, nullptr, nullptr, &tv);
        if (selectResult <= 0) continue;
        if (!FD_ISSET(listenSocket_, &readSet)) continue;

        sockaddr_in6 clientAddr;
        ar_socklen_t addrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenSocket_, (sockaddr*)&clientAddr, &addrLen);
        if (clientSocket == INVALID_SOCKET) continue;

        std::thread clientThread(&HttpServer::HandleClient, this, clientSocket);
        clientThread.detach();
    }
}

void HttpServer::HandleClient(SOCKET clientSocket) {
    activeConnections_.fetch_add(1);
    HttpRequest request;
    if (ParseRequest(clientSocket, request)) {
        bool showSensitive = logManager_ && logManager_->IsShowSensitive();
        if (!quiet_) {
            LOGF(L"[HTTP] Parsed request: %s %s %s, headers=%zu, body=%zu",
                request.method.c_str(), request.path.c_str(), request.version.c_str(),
                request.headers.size(), request.body.size());
        }
        if (showSensitive && !quiet_) {
            std::wstring headerLog;
            for (const auto& [name, value] : request.headers) {
                headerLog += name + L": " + value + L"\r\n";
            }
            LOGF(L"[HTTP] Request headers from client:\n%s", headerLog.c_str());
            LOGF(L"[HTTP] Request body from client (%zu bytes):\n%s", request.body.size(), Utils::Utf8ToWide(request.body).c_str());
        }
        if (quiet_) {
            // Control API: no per-request traffic logging (see SetQuiet).
        } else if (showSensitive) {
            std::wstring trafficHeaders;
            for (const auto& [name, value] : request.headers) {
                std::wstring lowerName = Utils::ToLower(name);
                if (lowerName == L"authorization" || lowerName == L"proxy-authorization") {
                    trafficHeaders += name + L": <REDACTED>\r\n";
                } else {
                    trafficHeaders += name + L": " + value + L"\r\n";
                }
            }
            LOG_TRAFFIC(L"CLIENT_IN",
                request.method + L" " + request.path + L" " + request.version + L"\r\n" +
                trafficHeaders + L"\r\n" +
                Utils::Utf8ToWide(request.body));
        } else {
            // Non-sensitive mode: the raw client request is pre-redaction, so
            // only metadata is logged. The redacted body appears at UPSTREAM_OUT.
            LOG_TRAFFIC(L"CLIENT_IN",
                request.method + L" " + request.path + L" " + request.version +
                L" | " + std::to_wstring(request.body.size()) + L" bytes (body omitted)");
        }
        try {
            HttpResponse response = requestHandler_(request);
            SendResponse(clientSocket, response);
        } catch (...) {
            HttpResponse err;
            err.statusCode = 502;
            err.body = "{\"error\": \"Proxy error\"}";
            err.headers[L"Content-Type"] = L"application/json";
            SendResponse(clientSocket, err);
        }
    }
    // Graceful close: send FIN, wait for client to close, then cleanup
    shutdown(clientSocket, SD_SEND);
    char drain[256];
    int drainResult;
    // Try to read until client closes (up to ~5 seconds total)
    for (int i = 0; i < 100; ++i) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(clientSocket, &readSet);
        timeval tv = {0, 50000}; // 50ms
        if (select(static_cast<int>(clientSocket) + 1, &readSet, nullptr, nullptr, &tv) > 0 && FD_ISSET(clientSocket, &readSet)) {
            drainResult = recv(clientSocket, drain, sizeof(drain), 0);
            if (drainResult == 0 || drainResult == SOCKET_ERROR) break;
        }
    }
    closesocket(clientSocket);
    activeConnections_.fetch_sub(1);
    stopCv_.notify_all();
}

bool HttpServer::ParseRequest(SOCKET clientSocket, HttpRequest& request) {
    std::string buffer;
    char temp[4096];
    int totalRead = 0;
    bool headersComplete = false;
    size_t headerEnd = std::string::npos;

    while (totalRead < 1024 * 1024) { // 1MB max headers
        int received = recv(clientSocket, temp, sizeof(temp), 0);
        if (received <= 0) break;
        buffer.append(temp, received);
        totalRead += received;
        headerEnd = buffer.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            headersComplete = true;
            break;
        }
    }

    if (!headersComplete) return false;

    // Parse request line
    size_t lineEnd = buffer.find("\r\n");
    if (lineEnd == std::string::npos) return false;
    std::string requestLine = buffer.substr(0, lineEnd);
    std::istringstream reqStream(requestLine);
    std::string method, path, version;
    reqStream >> method >> path >> version;
    request.method = Utils::Utf8ToWide(method);
    request.path = Utils::Utf8ToWide(path);
    request.version = Utils::Utf8ToWide(version);

    // Parse headers
    size_t headerStart = lineEnd + 2;
    while (headerStart < headerEnd) {
        size_t nextLine = buffer.find("\r\n", headerStart);
        if (nextLine == std::string::npos || nextLine > headerEnd) break;
        std::string headerLine = buffer.substr(headerStart, nextLine - headerStart);
        size_t colon = headerLine.find(':');
        if (colon != std::string::npos) {
            std::string name = headerLine.substr(0, colon);
            std::string value = headerLine.substr(colon + 1);
            // trim value
            size_t valStart = value.find_first_not_of(" \t");
            if (valStart != std::string::npos) value = value.substr(valStart);
            request.headers[Utils::Utf8ToWide(name)] = Utils::Utf8ToWide(value);
        }
        headerStart = nextLine + 2;
    }

    // Read body if Content-Length present (case-insensitive lookup)
    auto it = request.headers.end();
    for (auto hdrIt = request.headers.begin(); hdrIt != request.headers.end(); ++hdrIt) {
        if (Utils::ToLower(hdrIt->first) == L"content-length") {
            it = hdrIt;
            break;
        }
    }
    if (it != request.headers.end()) {
        int contentLength = std::stoi(Utils::WideToUtf8(it->second));
        size_t bodyStart = headerEnd + 4;
        size_t currentBodySize = buffer.size() - bodyStart;
        request.body = buffer.substr(bodyStart, currentBodySize);

        if (!quiet_) LOGF(L"[HTTP] Body read: contentLength=%d, initialBodySize=%zu", contentLength, request.body.size());

        int recvCount = 0;
        while ((int)request.body.size() < contentLength && totalRead < 50 * 1024 * 1024) { // 50MB max body
            int received = recv(clientSocket, temp, sizeof(temp), 0);
            recvCount++;
            if (received <= 0) {
                int err = WSAGetLastError();
                if (!quiet_) LOGF(L"[HTTP] Body recv returned %d (recvCount=%d, WSAError=%d)", received, recvCount, err);
                break;
            }
            request.body.append(temp, received);
            totalRead += received;
            if (!quiet_) LOGF(L"[HTTP] Body recv chunk: received=%d, totalBody=%zu", received, request.body.size());
        }
        if (!quiet_) LOGF(L"[HTTP] Body read complete: finalBodySize=%zu, expected=%d, recvCalls=%d", request.body.size(), contentLength, recvCount);
    }

    return true;
}

bool HttpServer::SendResponse(SOCKET clientSocket, const HttpResponse& response) {
    if (response.isStreaming && response.streamWriter) {
        if (response.streamWriterOwnsHeaders) {
            response.streamWriter(clientSocket);
            return true;
        }
        std::ostringstream responseStream;
        responseStream << "HTTP/1.1 " << response.statusCode << " OK\r\n";
        for (const auto& [name, value] : response.headers) {
            std::string nameUtf8 = Utils::WideToUtf8(name);
            std::string valueUtf8 = Utils::WideToUtf8(value);
            while (!valueUtf8.empty() && (valueUtf8.back() == ' ' || valueUtf8.back() == '\t' || valueUtf8.back() == '\r' || valueUtf8.back() == '\n')) {
                valueUtf8.pop_back();
            }
            responseStream << nameUtf8 << ": " << valueUtf8 << "\r\n";
        }
        responseStream << "Connection: close\r\n";
        responseStream << "\r\n";
        std::string headerData = responseStream.str();
        int totalSent = 0;
        while (totalSent < (int)headerData.size()) {
            int sent = send(clientSocket, headerData.c_str() + totalSent, (int)headerData.size() - totalSent, 0);
            if (sent <= 0) return false;
            totalSent += sent;
        }
        LOGF(L"[HTTP] Sent streaming response headers (%d bytes)", totalSent);
        response.streamWriter(clientSocket);
        return true;
    }

    std::string statusText = "OK";
    switch (response.statusCode) {
        case 200: statusText = "OK"; break;
        case 201: statusText = "Created"; break;
        case 204: statusText = "No Content"; break;
        case 400: statusText = "Bad Request"; break;
        case 401: statusText = "Unauthorized"; break;
        case 403: statusText = "Forbidden"; break;
        case 404: statusText = "Not Found"; break;
        case 500: statusText = "Internal Server Error"; break;
        case 502: statusText = "Bad Gateway"; break;
        case 503: statusText = "Service Unavailable"; break;
    }
    std::ostringstream responseStream;
    responseStream << "HTTP/1.1 " << response.statusCode << " " << statusText << "\r\n";
    bool hasContentLength = false;
    for (const auto& [name, value] : response.headers) {
        std::string nameUtf8 = Utils::WideToUtf8(name);
        std::string valueUtf8 = Utils::WideToUtf8(value);
        // Trim trailing whitespace (WinHTTP may include trailing \r in header values)
        while (!valueUtf8.empty() && (valueUtf8.back() == ' ' || valueUtf8.back() == '\t' || valueUtf8.back() == '\r' || valueUtf8.back() == '\n')) {
            valueUtf8.pop_back();
        }
        if (_stricmp(nameUtf8.c_str(), "content-length") == 0) {
            hasContentLength = true;
        }
        responseStream << nameUtf8 << ": " << valueUtf8 << "\r\n";
    }
    if (!hasContentLength) {
        responseStream << "Content-Length: " << response.body.size() << "\r\n";
    }
    responseStream << "Connection: close\r\n";
    responseStream << "\r\n";
    responseStream << response.body;

    std::string data = responseStream.str();

    // Log the raw response (headers only, body preview) only when show-sensitive
    // mode is on to avoid writing sensitive values to the log file.
    bool showSensitive = logManager_ && logManager_->IsShowSensitive();
    if (showSensitive && !quiet_) {
        std::string headerSection = data.substr(0, data.find("\r\n\r\n") + 4);
        std::wstring wHeaderSection = Utils::Utf8ToWide(headerSection);
        LOGF(L"[HTTP] Sending raw response headers:\n%s", wHeaderSection.c_str());
        LOGF(L"[HTTP] Response body (%zu bytes):\n%s",
            response.body.size(),
            Utils::Utf8ToWide(response.body).c_str());
    }
    if (!quiet_) {
        if (showSensitive) {
            LOG_TRAFFIC(L"CLIENT_OUT", Utils::Utf8ToWide(data));
        } else {
            // Non-sensitive mode: the final client response is post-unredaction,
            // so only metadata is logged. The redacted body appears at UPSTREAM_IN.
            LOG_TRAFFIC(L"CLIENT_OUT",
                std::to_wstring(response.statusCode) + L" | " + std::to_wstring(data.size()) + L" bytes (body omitted)");
        }
    }

    int totalSent = 0;
    while (totalSent < (int)data.size()) {
        int sent = send(clientSocket, data.c_str() + totalSent, (int)data.size() - totalSent, 0);
        if (sent <= 0) return false;
        totalSent += sent;
    }
    if (!quiet_) LOGF(L"[HTTP] Sent %d bytes total to client", totalSent);
    return true;
}

bool HttpServer::SendChunk(SOCKET clientSocket, const char* data, size_t len) {
    if (len == 0) return true;
    std::ostringstream chunkStream;
    chunkStream << std::hex << len << "\r\n";
    std::string chunkHeader = chunkStream.str();
    std::string chunkData(data, len);
    std::string terminator = "\r\n";

    const char* buffers[3] = { chunkHeader.c_str(), chunkData.c_str(), terminator.c_str() };
    int sizes[3] = { (int)chunkHeader.size(), (int)chunkData.size(), (int)terminator.size() };
    for (int i = 0; i < 3; ++i) {
        int totalSent = 0;
        while (totalSent < sizes[i]) {
            int sent = send(clientSocket, buffers[i] + totalSent, sizes[i] - totalSent, 0);
            if (sent <= 0) return false;
            totalSent += sent;
        }
    }
    return true;
}

bool HttpServer::SendChunkedEnd(SOCKET clientSocket) {
    const char* end = "0\r\n\r\n";
    int len = (int)strlen(end);
    int totalSent = 0;
    while (totalSent < len) {
        int sent = send(clientSocket, end + totalSent, len - totalSent, 0);
        if (sent <= 0) return false;
        totalSent += sent;
    }
    return true;
}

bool IsPortAvailable(int port) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
#endif

    SOCKET testSocket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (testSocket == INVALID_SOCKET) {
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    int reuse = 1;
    setsockopt(testSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    // Dual-stack port availability test (must match the real listener)
    int v6only = 0;
    setsockopt(testSocket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v6only, sizeof(v6only));

    sockaddr_in6 addr = {};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(static_cast<u_short>(port));

    bool available = (bind(testSocket, (sockaddr*)&addr, sizeof(addr)) == 0);
    closesocket(testSocket);
#ifdef _WIN32
    WSACleanup();
#endif
    return available;
}

int FindAvailablePort(int startPort, const std::vector<int>& excludePorts) {
    for (int port = startPort; port <= 65535; ++port) {
        if (port < 1024) continue;
        if (std::find(excludePorts.begin(), excludePorts.end(), port) != excludePorts.end()) continue;
        if (IsPortAvailable(port)) return port;
    }
    return -1;
}

} // namespace AgentRedactor
