import NetworkExtension
import os.log

class PacketTunnelProvider: NEPacketTunnelProvider {

    private var packetProcessor: PacketProcessor?
    private let logger = Logger(subsystem: "com.zapretgui.tunnel", category: "PacketTunnel")

    override func startTunnel(options: [String: NSObject]? = nil) async throws {
        logger.info("Starting Zapret packet tunnel")

        // Configure the tunnel
        let settings = NEPacketTunnelNetworkSettings(tunnelRemoteAddress: "10.120.0.2")

        let ipv4Settings = NEIPv4Settings(addresses: ["10.120.0.1"], subnetMasks: ["255.255.255.252"])
        ipv4Settings.includedRoutes = [NEIPv4Route.default()]
        settings.ipv4Settings = ipv4Settings

        settings.dnsSettings = NEDNSSettings(servers: ["1.1.1.1", "8.8.8.8"])
        settings.mtu = 1500

        try await setTunnelNetworkSettings(settings)

        // Load DPI bypass config from shared app group
        let config = loadConfig()

        // Start packet processor
        let processor = PacketProcessor()
        processor.start(packetFlow: packetFlow, config: config)
        packetProcessor = processor

        logger.info("Packet tunnel started: split=\(config.splitPos) disorder=\(config.useDisorder) fakeTTL=\(config.fakeTTL) fakeRepeats=\(config.fakeRepeats)")
    }

    override func stopTunnel(with reason: NEProviderStopReason) async {
        logger.info("Stopping Zapret packet tunnel, reason: \(String(describing: reason))")
        packetProcessor?.stop()
        packetProcessor = nil
    }

    override func handleAppMessage(_ messageData: Data) async -> Data? {
        if let command = String(data: messageData, encoding: .utf8) {
            logger.info("Received app message: \(command)")

            if command == "restart" {
                packetProcessor?.stop()
                let config = loadConfig()
                let processor = PacketProcessor()
                processor.start(packetFlow: packetFlow, config: config)
                packetProcessor = processor
            }
        }
        return nil
    }

    private func loadConfig() -> PacketProcessor.DPIConfig {
        var config = PacketProcessor.DPIConfig()

        guard let defaults = UserDefaults(suiteName: "group.com.zapretgui") else {
            logger.warning("Cannot access shared app group defaults, using defaults")
            return config
        }

        config.splitPos = defaults.integer(forKey: "splitPos")
        if config.splitPos == 0 { config.splitPos = 1 }

        config.useDisorder = defaults.bool(forKey: "useDisorder")

        let fakeTTL = defaults.integer(forKey: "fakeTTL")
        config.fakeTTL = fakeTTL > 0 ? Int32(fakeTTL) : 3

        let fakeRepeats = defaults.integer(forKey: "fakeRepeats")
        config.fakeRepeats = fakeRepeats > 0 ? fakeRepeats : 6

        // Load fake QUIC payload
        if let fakeQuicFile = defaults.string(forKey: "fakeQuicFile"),
           !fakeQuicFile.isEmpty {
            let containerURL = FileManager.default.containerURL(
                forSecurityApplicationGroupIdentifier: "group.com.zapretgui"
            )
            if let fakeURL = containerURL?.appendingPathComponent("fake/\(fakeQuicFile)") {
                do {
                    config.fakePayload = try Data(contentsOf: fakeURL)
                    logger.info("Loaded fake payload: \(fakeQuicFile) (\(config.fakePayload?.count ?? 0) bytes)")
                } catch {
                    logger.error("Failed to load fake payload \(fakeQuicFile): \(error)")
                }
            }
        }

        return config
    }
}
