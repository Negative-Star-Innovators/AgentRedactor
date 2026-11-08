import Foundation

// Port of windows/EngineClient.h's ProxyFacade: session matches live in the
// engine's ProxyEngine and are exposed per profile over the control API.

final class ProxyFacade {
    private let client: EngineClient

    init(client: EngineClient) {
        self.client = client
    }

    func getSessionMatches(profileId: String) throws -> [SessionMatchModel] {
        let response = try client.get("/profiles/\(profileId)/matches")
        return (response.body as? [Any])?.compactMap(SessionMatchModel.fromJSON) ?? []
    }

    func clearSessionMatches(profileId: String) throws {
        try client.delete("/profiles/\(profileId)/matches")
    }
}
