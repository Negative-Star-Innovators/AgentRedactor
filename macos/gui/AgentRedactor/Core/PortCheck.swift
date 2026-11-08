import Foundation

// Port of Utils::IsPortAvailable/FindAvailablePort (core/src/http_server.cpp):
// a dual-stack (::/IPv6-any with v4 mapped) bind test, matching the real
// proxy listener, so "available" agrees with what the engine can bind.

enum PortCheck {
    static func isPortAvailable(_ port: Int) -> Bool {
        let fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)
        if fd < 0 { return false }
        defer { close(fd) }

        var reuse: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, socklen_t(MemoryLayout<Int32>.size))

        var v6only: Int32 = 0
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, socklen_t(MemoryLayout<Int32>.size))

        var addr = sockaddr_in6()
        addr.sin6_len = UInt8(MemoryLayout<sockaddr_in6>.size)
        addr.sin6_family = sa_family_t(AF_INET6)
        addr.sin6_addr = in6addr_any
        addr.sin6_port = in_port_t(port).bigEndian

        let ok = withUnsafePointer(to: &addr) { ptr -> Bool in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                bind(fd, sa, socklen_t(MemoryLayout<sockaddr_in6>.size)) == 0
            }
        }
        return ok
    }

    /// First free port at or above startPort, skipping excluded ports.
    static func findAvailablePort(startPort: Int, excluding excluded: [Int]) -> Int {
        for port in max(startPort, 1024)...65535 {
            if excluded.contains(port) { continue }
            if isPortAvailable(port) { return port }
        }
        return -1
    }
}
