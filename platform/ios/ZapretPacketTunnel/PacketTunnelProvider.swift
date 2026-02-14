import NetworkExtension
import os.log

class PacketTunnelProvider: NEPacketTunnelProvider {

    private var tpwsProcess: Process?
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

        // Start tpws
        startTpws()

        logger.info("Packet tunnel started successfully")
    }

    override func stopTunnel(with reason: NEProviderStopReason) async {
        logger.info("Stopping Zapret packet tunnel, reason: \(String(describing: reason))")
        stopTpws()
    }

    override func handleAppMessage(_ messageData: Data) async -> Data? {
        // Handle messages from the main app (e.g., strategy changes)
        if let command = String(data: messageData, encoding: .utf8) {
            logger.info("Received app message: \(command)")

            if command == "restart" {
                stopTpws()
                startTpws()
            }
        }
        return nil
    }

    private func startTpws() {
        guard let tpwsPath = Bundle.main.path(forResource: "tpws", ofType: nil) else {
            logger.error("tpws binary not found in bundle")
            return
        }

        // Get the container directory for config files
        let containerURL = FileManager.default.containerURL(
            forSecurityApplicationGroupIdentifier: "group.com.zapretgui"
        )
        let listsDir = containerURL?.appendingPathComponent("lists").path ?? ""

        let process = Process()
        process.executableURL = URL(fileURLWithPath: tpwsPath)
        process.arguments = [
            "--port", "1080",
            "--bind-addr=127.0.0.1",
            "--hostlist=\(listsDir)/list-general.txt",
            "--split-pos=1"
        ]

        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe

        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            if let output = String(data: data, encoding: .utf8), !output.isEmpty {
                self?.logger.info("tpws: \(output)")
            }
        }

        do {
            try process.run()
            tpwsProcess = process
            logger.info("tpws started with PID \(process.processIdentifier)")
        } catch {
            logger.error("Failed to start tpws: \(error.localizedDescription)")
        }
    }

    private func stopTpws() {
        if let process = tpwsProcess, process.isRunning {
            process.terminate()
            process.waitUntilExit()
            logger.info("tpws stopped")
        }
        tpwsProcess = nil
    }
}
