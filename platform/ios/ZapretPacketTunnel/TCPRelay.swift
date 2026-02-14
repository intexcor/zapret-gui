import Foundation
import Network
import os.log

/// TCP relay with TLS ClientHello split for iOS.
///
/// Uses NWConnection for TCP relay (automatically bypasses the tunnel in
/// a Network Extension context). Splits the first TLS ClientHello at
/// the configured position to bypass DPI inspection.
class TCPRelay {

    private let logger = Logger(subsystem: "com.zapretgui.tunnel", category: "TCPRelay")

    struct Config {
        var splitPos: Int = 1
        var useDisorder: Bool = false
    }

    class Session {
        let srcPort: UInt16
        let dstAddr: UInt32
        let dstPort: UInt16

        var connection: NWConnection?
        var tunSeq: UInt32 = 0
        var tunAck: UInt32 = 0
        var firstDataSent: Bool = false
        var lastActivity: Date = Date()
        var active: Bool = true

        init(srcPort: UInt16, dstAddr: UInt32, dstPort: UInt16) {
            self.srcPort = srcPort
            self.dstAddr = dstAddr
            self.dstPort = dstPort
        }
    }

    private var sessions: [String: Session] = [:]
    private let config: Config
    private let sessionTimeout: TimeInterval = 300
    private let queue = DispatchQueue(label: "com.zapretgui.tcp-relay")

    /// Callback to write a constructed IP packet back to the TUN
    var onPacketReady: ((Data) -> Void)?

    init(config: Config) {
        self.config = config
    }

    /// Process an outgoing TCP packet (app → internet)
    func processPacket(srcAddr: UInt32, dstAddr: UInt32,
                       srcPort: UInt16, dstPort: UInt16,
                       seq: UInt32, ack: UInt32, flags: UInt8,
                       payload: Data) {
        queue.async { [self] in
            let key = sessionKey(srcPort: srcPort, dstAddr: dstAddr, dstPort: dstPort)

            // RST
            if flags & UInt8(DPI_TCP_RST) != 0 {
                if let session = sessions[key] {
                    closeSession(key: key, session: session)
                }
                return
            }

            // SYN
            if flags & UInt8(DPI_TCP_SYN) != 0 {
                handleSyn(key: key, srcPort: srcPort, dstAddr: dstAddr,
                         dstPort: dstPort, seq: seq)
                return
            }

            guard let session = sessions[key] else { return }

            // FIN
            if flags & UInt8(DPI_TCP_FIN) != 0 {
                handleFin(key: key, session: session, seq: seq)
                return
            }

            // Data
            if !payload.isEmpty {
                handleData(key: key, session: session, payload: payload, seq: seq)
            }
        }
    }

    /// Clean up expired sessions
    func cleanup() {
        queue.async { [self] in
            let now = Date()
            let expired = sessions.filter { now.timeIntervalSince($0.value.lastActivity) > sessionTimeout }
            for (key, session) in expired {
                sendToTun(session: session, flags: UInt8(DPI_TCP_RST), payload: Data())
                closeSession(key: key, session: session)
            }
        }
    }

    /// Destroy all sessions
    func destroy() {
        queue.sync {
            for (key, session) in sessions {
                closeSession(key: key, session: session)
            }
        }
    }

    // MARK: - Private

    private func sessionKey(srcPort: UInt16, dstAddr: UInt32, dstPort: UInt16) -> String {
        "\(srcPort)-\(dstAddr)-\(dstPort)"
    }

    private func handleSyn(key: String, srcPort: UInt16,
                           dstAddr: UInt32, dstPort: UInt16, seq: UInt32) {
        // Close existing session if re-SYN
        if let existing = sessions[key] {
            closeSession(key: key, session: existing)
        }

        let session = Session(srcPort: srcPort, dstAddr: dstAddr, dstPort: dstPort)

        // Generate our ISN
        let now = DispatchTime.now().uptimeNanoseconds
        session.tunSeq = UInt32(truncatingIfNeeded: now) ^ (UInt32(dstPort) << 16 | UInt32(srcPort))
        session.tunAck = seq &+ 1  // ACK the SYN

        // Create NWConnection (automatically bypasses tunnel)
        let addrBytes = withUnsafeBytes(of: dstAddr.bigEndian) { Array($0) }
        let addrString = "\(addrBytes[0]).\(addrBytes[1]).\(addrBytes[2]).\(addrBytes[3])"
        let host = NWEndpoint.Host(addrString)
        let port = NWEndpoint.Port(rawValue: dstPort)!

        let params = NWParameters.tcp
        params.preferNoProxies = true

        let connection = NWConnection(host: host, port: port, using: params)
        session.connection = connection

        sessions[key] = session

        // Send SYN-ACK to app immediately (optimistic — we'll RST if connect fails)
        sendToTun(session: session,
                  flags: UInt8(DPI_TCP_SYN) | UInt8(DPI_TCP_ACK),
                  payload: Data())

        // Set up connection
        connection.stateUpdateHandler = { [weak self, weak session] state in
            guard let self = self, let session = session else { return }
            self.queue.async {
                switch state {
                case .ready:
                    self.logger.debug("TCP connected to \(addrString):\(dstPort)")
                    self.startReceiving(key: key, session: session)
                case .failed(let error):
                    self.logger.error("TCP connect failed: \(error)")
                    self.sendToTun(session: session, flags: UInt8(DPI_TCP_RST), payload: Data())
                    self.closeSession(key: key, session: session)
                case .cancelled:
                    break
                default:
                    break
                }
            }
        }

        connection.start(queue: queue)
    }

    private func handleData(key: String, session: Session, payload: Data, seq: UInt32) {
        session.lastActivity = Date()
        session.tunAck = seq &+ UInt32(payload.count)

        guard let connection = session.connection else { return }

        // Check if this is the first data and contains TLS ClientHello
        if !session.firstDataSent && config.splitPos > 0 &&
           payload.count > config.splitPos {

            let isTLS = payload.withUnsafeBytes { ptr -> Bool in
                guard let base = ptr.baseAddress else { return false }
                return dpi_is_tls_client_hello(base.assumingMemoryBound(to: UInt8.self),
                                               Int32(payload.count))
            }

            if isTLS {
                logger.debug("TLS ClientHello detected, splitting at pos \(self.config.splitPos)")
                let pos = config.splitPos

                if config.useDisorder {
                    // Send second part first (disorder)
                    connection.send(content: payload[pos...], completion: .contentProcessed { _ in })
                    connection.send(content: payload[..<pos], completion: .contentProcessed { _ in })
                } else {
                    connection.send(content: payload[..<pos], completion: .contentProcessed { _ in })
                    connection.send(content: payload[pos...], completion: .contentProcessed { _ in })
                }
                session.firstDataSent = true

                // ACK the data
                sendToTun(session: session, flags: UInt8(DPI_TCP_ACK), payload: Data())
                return
            }
        }

        // Forward as-is
        connection.send(content: payload, completion: .contentProcessed { _ in })
        session.firstDataSent = true

        // ACK the data
        sendToTun(session: session, flags: UInt8(DPI_TCP_ACK), payload: Data())
    }

    private func handleFin(key: String, session: Session, seq: UInt32) {
        session.tunAck = seq &+ 1

        // ACK the FIN
        sendToTun(session: session, flags: UInt8(DPI_TCP_ACK), payload: Data())

        // Close our side
        session.connection?.send(content: nil, contentContext: .finalMessage,
                                isComplete: true,
                                completion: .contentProcessed { _ in })
    }

    private func startReceiving(key: String, session: Session) {
        guard let connection = session.connection else { return }

        connection.receive(minimumIncompleteLength: 1, maximumLength: 65536) {
            [weak self, weak session] content, _, isComplete, error in
            guard let self = self, let session = session, session.active else { return }

            self.queue.async {
                if let data = content, !data.isEmpty {
                    session.lastActivity = Date()
                    self.sendToTun(session: session,
                                   flags: UInt8(DPI_TCP_ACK) | UInt8(DPI_TCP_PSH),
                                   payload: data)
                    // Continue receiving
                    self.startReceiving(key: key, session: session)
                } else if isComplete || error != nil {
                    // Server closed connection
                    self.sendToTun(session: session,
                                   flags: UInt8(DPI_TCP_FIN) | UInt8(DPI_TCP_ACK),
                                   payload: Data())
                    self.closeSession(key: key, session: session)
                } else {
                    self.startReceiving(key: key, session: session)
                }
            }
        }
    }

    private func sendToTun(session: Session, flags: UInt8, payload: Data) {
        var pkt = [UInt8](repeating: 0, count: 65536)

        let pktLen: Int32 = payload.withUnsafeBytes { payloadPtr -> Int32 in
            let payloadBase = payloadPtr.baseAddress?.assumingMemoryBound(to: UInt8.self)
            return dpi_build_ipv4_tcp(
                &pkt, Int32(pkt.count),
                session.dstAddr,        // response: server → app
                0x0A78_0001,            // 10.120.0.1 (TUN address)
                session.dstPort,
                session.srcPort,
                session.tunSeq,
                session.tunAck,
                flags,
                32768,                  // window
                payloadBase, Int32(payload.count)
            )
        }

        if pktLen > 0 {
            // Advance seq for data/SYN/FIN
            if !payload.isEmpty {
                session.tunSeq = session.tunSeq &+ UInt32(payload.count)
            }
            if flags & (UInt8(DPI_TCP_SYN) | UInt8(DPI_TCP_FIN)) != 0 {
                session.tunSeq = session.tunSeq &+ 1
            }

            let data = Data(bytes: pkt, count: Int(pktLen))
            onPacketReady?(data)
        }
    }

    private func closeSession(key: String, session: Session) {
        session.active = false
        session.connection?.cancel()
        session.connection = nil
        sessions.removeValue(forKey: key)
    }
}
