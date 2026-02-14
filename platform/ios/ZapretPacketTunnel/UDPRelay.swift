import Foundation
import os.log
import Darwin

/// UDP relay with QUIC fake injection for iOS.
///
/// Manages UDP sessions via BSD sockets (which automatically bypass the tunnel
/// in a Network Extension context). Detects QUIC Initial packets and injects
/// fake packets with low TTL before forwarding the original.
class UDPRelay {

    private let logger = Logger(subsystem: "com.zapretgui.tunnel", category: "UDPRelay")

    struct Config {
        var fakePayload: Data?
        var fakeTTL: Int32 = 3
        var fakeRepeats: Int = 6
    }

    struct Session {
        let srcPort: UInt16   // app-side source port
        let dstAddr: UInt32   // destination IP (network byte order)
        let dstPort: UInt16   // destination port
        let fd: Int32         // BSD socket
        var lastActivity: Date
    }

    private var sessions: [String: Session] = [:]
    private let config: Config
    private let sessionTimeout: TimeInterval = 120
    private let queue = DispatchQueue(label: "com.zapretgui.udp-relay")
    private var readSources: [String: DispatchSourceRead] = [:]

    /// Callback to write a constructed IP packet back to the TUN
    var onPacketReady: ((Data) -> Void)?

    init(config: Config) {
        self.config = config
    }

    /// Process an outgoing UDP packet (app → internet)
    func processPacket(srcAddr: UInt32, dstAddr: UInt32,
                       srcPort: UInt16, dstPort: UInt16,
                       payload: Data) {
        queue.async { [self] in
            let key = sessionKey(srcPort: srcPort, dstAddr: dstAddr, dstPort: dstPort)
            let session = getOrCreateSession(key: key, srcPort: srcPort,
                                             dstAddr: dstAddr, dstPort: dstPort)
            guard let session = session else { return }

            // Check for QUIC Initial
            if let fakeData = config.fakePayload,
               payload.count > 0,
               payload.withUnsafeBytes({ ptr -> Bool in
                   guard let base = ptr.baseAddress else { return false }
                   return dpi_is_quic_initial(base.assumingMemoryBound(to: UInt8.self),
                                              Int32(payload.count))
               }) {
                logger.debug("QUIC Initial detected, injecting \(self.config.fakeRepeats) fakes")
                sendWithFakes(session: session, payload: payload, fakeData: fakeData)
            } else {
                payload.withUnsafeBytes { ptr in
                    guard let base = ptr.baseAddress else { return }
                    Darwin.send(session.fd, base, payload.count, 0)
                }
            }
        }
    }

    /// Clean up expired sessions
    func cleanup() {
        queue.async { [self] in
            let now = Date()
            let expired = sessions.filter { now.timeIntervalSince($0.value.lastActivity) > sessionTimeout }
            for (key, session) in expired {
                readSources[key]?.cancel()
                readSources.removeValue(forKey: key)
                Darwin.close(session.fd)
                sessions.removeValue(forKey: key)
            }
        }
    }

    /// Destroy all sessions
    func destroy() {
        queue.sync {
            for (key, session) in sessions {
                readSources[key]?.cancel()
                Darwin.close(session.fd)
            }
            sessions.removeAll()
            readSources.removeAll()
        }
    }

    // MARK: - Private

    private func sessionKey(srcPort: UInt16, dstAddr: UInt32, dstPort: UInt16) -> String {
        "\(srcPort)-\(dstAddr)-\(dstPort)"
    }

    private func getOrCreateSession(key: String, srcPort: UInt16,
                                    dstAddr: UInt32, dstPort: UInt16) -> Session? {
        if var existing = sessions[key] {
            existing.lastActivity = Date()
            sessions[key] = existing
            return existing
        }

        // Create BSD UDP socket (auto-bypasses tunnel in Network Extension)
        let fd = Darwin.socket(AF_INET, SOCK_DGRAM, 0)
        if fd < 0 {
            logger.error("socket(SOCK_DGRAM) failed: \(String(cString: strerror(errno)))")
            return nil
        }

        // Connect to destination
        var dst = sockaddr_in()
        dst.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        dst.sin_family = sa_family_t(AF_INET)
        dst.sin_port = dstPort.bigEndian
        dst.sin_addr.s_addr = dstAddr.bigEndian

        let connectResult = withUnsafePointer(to: &dst) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                Darwin.connect(fd, sa, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }

        if connectResult < 0 {
            logger.error("connect(udp) failed: \(String(cString: strerror(errno)))")
            Darwin.close(fd)
            return nil
        }

        let session = Session(srcPort: srcPort, dstAddr: dstAddr,
                              dstPort: dstPort, fd: fd, lastActivity: Date())
        sessions[key] = session

        // Set up dispatch source to read responses
        setupReadSource(key: key, session: session)

        return session
    }

    private func setupReadSource(key: String, session: Session) {
        let source = DispatchSource.makeReadSource(fileDescriptor: session.fd, queue: queue)

        source.setEventHandler { [weak self] in
            guard let self = self else { return }

            var buf = [UInt8](repeating: 0, count: 65536)
            let n = Darwin.recv(session.fd, &buf, buf.count, 0)
            if n <= 0 { return }

            // Build IP+UDP response for TUN
            var pkt = [UInt8](repeating: 0, count: 65536)
            let pktLen = dpi_build_ipv4_udp(
                &pkt, Int32(pkt.count),
                session.dstAddr,    // response: server → app
                0x0A78_0001,        // 10.120.0.1 (TUN address)
                session.dstPort,
                session.srcPort,
                buf, Int32(n)
            )

            if pktLen > 0 {
                let data = Data(bytes: pkt, count: Int(pktLen))
                self.onPacketReady?(data)
            }
        }

        source.setCancelHandler {
            // fd is closed in destroy/cleanup, not here
        }

        source.resume()
        readSources[key] = source
    }

    private func sendWithFakes(session: Session, payload: Data, fakeData: Data) {
        // Set low TTL for fakes
        var ttl = config.fakeTTL
        setsockopt(session.fd, IPPROTO_IP, IP_TTL, &ttl, socklen_t(MemoryLayout<Int32>.size))

        // Send N fake packets
        fakeData.withUnsafeBytes { ptr in
            guard let base = ptr.baseAddress else { return }
            for _ in 0..<config.fakeRepeats {
                Darwin.send(session.fd, base, fakeData.count, 0)
            }
        }

        // Restore normal TTL and send original
        ttl = 64
        setsockopt(session.fd, IPPROTO_IP, IP_TTL, &ttl, socklen_t(MemoryLayout<Int32>.size))

        payload.withUnsafeBytes { ptr in
            guard let base = ptr.baseAddress else { return }
            Darwin.send(session.fd, base, payload.count, 0)
        }
    }
}
