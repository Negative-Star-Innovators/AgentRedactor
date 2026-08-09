#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace AgentRedactor {

class LogManager;

struct HttpRequest {
    std::wstring method;
    std::wstring path;
    std::unordered_map<std::wstring, std::wstring> headers;
    std::string body;
    std::wstring version;
};

struct HttpResponse {
    int statusCode = 200;
    std::unordered_map<std::wstring, std::wstring> headers;
    std::string body;
    // When isStreaming is true, body is ignored and streamWriter is invoked
    // after response headers have been sent. This lets handlers proxy live
    // streams (e.g. SSE) without buffering the entire response first.
    bool isStreaming = false;
    // If true, HttpServer sends nothing before invoking streamWriter. The
    // streamWriter is responsible for the full HTTP response (status line,
    // headers, and body). Use this when response headers are not known until
    // the upstream connection has been established.
    bool streamWriterOwnsHeaders = false;
    std::function<void(SOCKET clientSocket)> streamWriter;
};

class HttpServer {
public:
    HttpServer();
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    bool Start(int port, std::function<HttpResponse(const HttpRequest&)> handler);
    void Stop();
    bool IsRunning() const { return running_.load(); }
    int GetPort() const { return port_; }
    void SetLogManager(LogManager* lm) { logManager_ = lm; }

    // Helpers for HTTP/1.1 chunked transfer encoding. These send the formatted
    // chunk over the socket and are used by streaming responses.
    static bool SendChunk(SOCKET clientSocket, const char* data, size_t len);
    static bool SendChunkedEnd(SOCKET clientSocket);

private:
    void RunListener();
    void HandleClient(SOCKET clientSocket);
    bool ParseRequest(SOCKET clientSocket, HttpRequest& request);
    bool SendResponse(SOCKET clientSocket, const HttpResponse& response);

    SOCKET listenSocket_ = INVALID_SOCKET;
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<bool> shouldStop_{false};
    std::thread listenerThread_;
    std::function<HttpResponse(const HttpRequest&)> requestHandler_;
    std::atomic<int> activeConnections_{0};
    std::mutex stopMutex_;
    std::condition_variable stopCv_;
    LogManager* logManager_ = nullptr;
};

bool IsPortAvailable(int port);
int FindAvailablePort(int startPort, const std::vector<int>& excludePorts);

} // namespace AgentRedactor
