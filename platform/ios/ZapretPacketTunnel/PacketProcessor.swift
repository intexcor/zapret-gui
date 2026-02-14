import Foundation
import NetworkExtension
import os.log

/// Main packet processor for iOS VPN tunnel.
///
/// Reads packets from the TUN, dispatches TCP/UDP to respective relays,
/// and writes response packets back to the TUN.
class PacketProcessor {

    private let logger = Logger(subsystem: "com.zapretgui.tunnel", category: "PacketProcessor")

    struct DPIConfig {
        var fakePayload: Data?
        var fakeTTL: Int32 = 3
        var fakeRepeats: Int = 6
        var splitPos: Int = 1
        var useDisorder: Bool = false
    }

    private var tcpRelay: TCPRelay?
    private var udpRelay: UDPRelay?
    private var cleanupTimer: DispatchSourceTimer?
    private let queue = DispatchQueue(label: "com.zapretgui.packet-processor")
    private weak var packetFlow: NEPacketTunnelFlow?

    func start(packetFlow: NEPacketTunnelFlow, config: DPIConfig) {
        self.packetFlow = packetFlow

        logger.info("PacketProcessor starting: split=\(config.splitPos) disorder=\(config.useDisorder) fakeTTL=\(config.fakeTTL) fakeRepeats=\(config.fakeRepeats)")

        // Initialize relays
        let tcpConfig = TCPRelay.Config(splitPos: config.splitPos,
                                        useDisorder: config.useDisorder)
        tcpRelay = TCPRelay(config: tcpConfig)
        tcpRelay?.onPacketReady = { [weak self] data in
            self?.writeToTun(data)
        }

        let udpConfig = UDPRelay.Config(fakePayload: config.fakePayload,
                                        fakeTTL: config.fakeTTL,
                                        fakeRepeats: config.fakeRepeats)
        udpRelay = UDPRelay(config: udpConfig)
        udpRelay?.onPacketReady = { [weak self] data in
            self?.writeToTun(data)
        }

        // Start periodic session cleanup
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + 10, repeating: 10)
        timer.setEventHandler { [weak self] in
            self?.tcpRelay?.cleanup()
            self?.udpRelay?.cleanup()
        }
        timer.resume()
        cleanupTimer = timer

        // Start read loop
        readLoop()
    }

    func stop() {
        logger.info("PacketProcessor stopping")

        cleanupTimer?.cancel()
        cleanupTimer = nil

        tcpRelay?.destroy()
        tcpRelay = nil

        udpRelay?.destroy()
        udpRelay = nil
    }

    // MARK: - Private

    private func readLoop() {
        packetFlow?.readPackets { [weak self] packets, protocols in
            guard let self = self else { return }

            for (i, packet) in packets.enumerated() {
                // protocols[i] should be AF_INET (2) for IPv4
                let proto = protocols[i].intValue
                if proto == AF_INET {
                    self.processIPv4Packet(packet)
                }
            }

            // Continue reading
            self.readLoop()
        }
    }

    private func processIPv4Packet(_ packet: Data) {
        packet.withUnsafeBytes { ptr in
            guard let base = ptr.baseAddress?.assumingMemoryBound(to: UInt8.self) else { return }
            let len = Int32(packet.count)

            var ip = dpi_ip_info_t()
            guard dpi_parse_ipv4(base, len, &ip) == 0 else { return }

            if ip.protocol == 6 { // TCP
                var tcp = dpi_tcp_info_t()
                guard dpi_parse_tcp(ip.l4_data, ip.l4_len, &tcp) == 0 else { return }

                let payload: Data
                if tcp.payload_len > 0 {
                    payload = Data(bytes: tcp.payload, count: Int(tcp.payload_len))
                } else {
                    payload = Data()
                }

                tcpRelay?.processPacket(
                    srcAddr: ip.src_addr, dstAddr: ip.dst_addr,
                    srcPort: tcp.src_port, dstPort: tcp.dst_port,
                    seq: tcp.seq, ack: tcp.ack, flags: tcp.flags,
                    payload: payload
                )
            } else if ip.protocol == 17 { // UDP
                var udp = dpi_udp_info_t()
                guard dpi_parse_udp(ip.l4_data, ip.l4_len, &udp) == 0 else { return }

                let payload: Data
                if udp.payload_len > 0 {
                    payload = Data(bytes: udp.payload, count: Int(udp.payload_len))
                } else {
                    payload = Data()
                }

                udpRelay?.processPacket(
                    srcAddr: ip.src_addr, dstAddr: ip.dst_addr,
                    srcPort: udp.src_port, dstPort: udp.dst_port,
                    payload: payload
                )
            }
        }
    }

    private func writeToTun(_ packet: Data) {
        // Protocol number 2 = AF_INET (IPv4)
        packetFlow?.writePackets([packet], withProtocols: [NSNumber(value: AF_INET)])
    }
}
