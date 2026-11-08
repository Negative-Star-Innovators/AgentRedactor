import Foundation

// GUI-side client for the engine's localhost control API — the macOS port of
// windows/EngineClient.h + linux/gui/engine_client.h. Reads
// <configDir>/control.json ({"port": N, "token": "...", "pid": N}) and issues
// bearer-authenticated requests against 127.0.0.1.
//
// The Windows/Linux clients are synchronous (localhost round-trips are
// sub-millisecond and failures surface as "engine not running"); this port
// keeps that contract with a semaphore-wrapped URLSession so the facades stay
// call-site identical. CALL FROM A BACKGROUND QUEUE ONLY (the app uses a
// dedicated serial queue in AppState).

enum EngineClientError: Error, LocalizedError {
    case notConnected
    case transport(String)
    case unauthorized
    case serverError(Int, String?)

    var errorDescription: String? {
        switch self {
        case .notConnected:
            return "Engine control API is not reachable."
        case .transport(let message):
            return message
        case .unauthorized:
            return "Engine control API authorization failed."
        case .serverError(let code, let message):
            return message ?? "Engine returned status \(code)."
        }
    }
}

struct EngineResponse {
    var status: Int
    var body: Any?
}

final class EngineClient {
    private struct ConnectionInfo {
        var port: Int
        var token: String
        var pid: Int32
    }

    private var connection: ConnectionInfo?
    private let session: URLSession
    /// Guards `connection` (poll thread + UI-initiated mutations).
    private let lock = NSLock()

    init() {
        let config = URLSessionConfiguration.ephemeral
        config.timeoutIntervalForRequest = 5
        config.timeoutIntervalForResource = 5
        session = URLSession(configuration: config)
    }

    // MARK: - Connection

    /// Loads connection info from <configDir>/control.json. Returns false when
    /// the file is missing or malformed (engine not started yet).
    @discardableResult
    func connect(configDir: URL) -> Bool {
        struct ControlFile: Decodable {
            var port: Int
            var token: String
            var pid: Int32?
        }
        guard let data = try? Data(contentsOf: configDir.appendingPathComponent("control.json")),
              let parsed = try? JSONDecoder().decode(ControlFile.self, from: data),
              parsed.port > 0, !parsed.token.isEmpty else {
            return false
        }
        lock.lock()
        connection = ConnectionInfo(port: parsed.port, token: parsed.token, pid: parsed.pid ?? 0)
        lock.unlock()
        return true
    }

    func isConnected() -> Bool {
        lock.lock()
        let connected = connection != nil
        lock.unlock()
        return connected
    }

    // MARK: - Requests

    func ping() -> Bool {
        guard let response = try? get("/status") else { return false }
        return response.status == 200
    }

    func get(_ path: String) throws -> EngineResponse {
        try request("GET", path: path, body: nil, longTimeout: false)
    }

    @discardableResult
    func post(_ path: String, body: Any? = nil) throws -> EngineResponse {
        try request("POST", path: path, body: body, longTimeout: false)
    }

    /// POST /unlock and PUT /settings/disableMasterPassword can sit behind a
    /// user-consent prompt (the Windows/Linux transports give these 90 s).
    @discardableResult
    func postWithConsentTimeout(_ path: String, body: Any? = nil) throws -> EngineResponse {
        try request("POST", path: path, body: body, longTimeout: true)
    }

    @discardableResult
    func put(_ path: String, body: Any) throws -> EngineResponse {
        try request("PUT", path: path, body: body, longTimeout: false)
    }

    @discardableResult
    func putWithConsentTimeout(_ path: String, body: Any) throws -> EngineResponse {
        try request("PUT", path: path, body: body, longTimeout: true)
    }

    @discardableResult
    func delete(_ path: String) throws -> EngineResponse {
        try request("DELETE", path: path, body: nil, longTimeout: false)
    }

    // MARK: - Plumbing

    private func request(_ method: String, path: String, body: Any?, longTimeout: Bool) throws -> EngineResponse {
        lock.lock()
        let info = connection
        lock.unlock()
        guard let info else { throw EngineClientError.notConnected }

        var url = URLComponents()
        url.scheme = "http"
        url.host = "127.0.0.1"
        url.port = info.port
        url.path = path
        guard let target = url.url else { throw EngineClientError.transport("bad URL") }

        var request = URLRequest(url: target)
        request.httpMethod = method
        request.setValue("Bearer \(info.token)", forHTTPHeaderField: "Authorization")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.timeoutInterval = longTimeout ? 90 : 5
        if let body {
            request.httpBody = try? JSONSerialization.data(withJSONObject: body)
        }

        let semaphore = DispatchSemaphore(value: 0)
        struct RawResult {
            var data: Data
            var response: URLResponse
        }
        var result: Result<RawResult, Error>?
        session.dataTask(with: request) { data, response, error in
            if let error {
                result = .failure(error)
            } else if let data, let response {
                result = .success(RawResult(data: data, response: response))
            } else {
                result = .failure(EngineClientError.transport("empty response"))
            }
            semaphore.signal()
        }.resume()
        semaphore.wait()

        guard case .success(let raw) = result else {
            let message: String
            if case .failure(let error) = result {
                message = error.localizedDescription
            } else {
                message = "no response"
            }
            throw EngineClientError.transport(message)
        }
        let status = (raw.response as? HTTPURLResponse)?.statusCode ?? 0
        if status == 401 { throw EngineClientError.unauthorized }

        let parsed: Any? = (try? JSONSerialization.jsonObject(with: raw.data)).flatMap { $0 }
        if status < 200 || status >= 300 {
            let message = (parsed as? [String: Any])?["error"] as? String
            throw EngineClientError.serverError(status, message)
        }
        return EngineResponse(status: status, body: parsed)
    }
}
