#pragma once

#include <string>
#include <functional>
#include <filesystem>
#include "http_server.h"

namespace AgentRedactor {

// Localhost-only control API server, built on HttpServer. Used by the engine
// process (agentredactor.exe); the GUI and (later) CLI subcommands talk to it.
//
// Security model: the server binds 127.0.0.1 only. At startup it generates a
// random bearer token and writes {"port": N, "token": "..."} to
// <configDir>/control.json, ACL'd to the current user. Every request must
// carry "Authorization: Bearer <token>"; authenticated requests are forwarded
// to the route handler supplied by the caller.
class ControlServer {
public:
    using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

    static constexpr int kDefaultPort = 8737;

    ControlServer() = default;
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    // Binds 127.0.0.1 on preferredPort, falling back to an ephemeral port when
    // the preferred one is taken, then writes the control file. Returns false
    // only when no listener could be started at all.
    bool Start(int preferredPort, const std::filesystem::path& configDir, RouteHandler handler);
    void Stop();
    bool IsRunning() const;

    int Port() const { return port_; }
    const std::wstring& Token() const { return token_; }

    static std::filesystem::path ControlFilePath(const std::filesystem::path& configDir);

private:
    HttpResponse HandleRequest(const HttpRequest& request);
    std::wstring GenerateToken() const;
    bool WriteControlFile(const std::filesystem::path& path) const;

    HttpServer server_;
    RouteHandler routeHandler_;
    std::wstring token_;
    int port_ = 0;
};

} // namespace AgentRedactor
