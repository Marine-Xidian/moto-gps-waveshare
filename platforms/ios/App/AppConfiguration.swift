import Foundation

enum AppConfiguration {
    // HTTP is permitted only for loopback or the explicitly configured LAN gateway.
    static func allowsGatewayURL(_ url: URL) -> Bool {
        if url.scheme == "https" { return true }
        guard url.scheme == "http" else { return false }
        if ["localhost", "127.0.0.1"].contains(url.host ?? "") { return true }
        let gateway = gatewayBaseURL
        return gateway.scheme == "http"
            && gateway.host == "192.168.31.57"
            && url.host == gateway.host
            && url.port == gateway.port
    }

    static var gatewayBaseURL: URL {
        if let value = Bundle.main.object(forInfoDictionaryKey: "MOTOGPSGatewayBaseURL") as? String,
           let url = URL(string: value)
        {
            return url
        }
        // Public sources intentionally do not use the author's private gateway.
        // Configure MOTOGPSGatewayBaseURL in project.yml, then run xcodegen.
        return URL(string: "https://example.invalid/moto-gps/api/")!
    }
}
