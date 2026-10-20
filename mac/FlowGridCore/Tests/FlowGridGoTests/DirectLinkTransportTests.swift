import CryptoKit
import Foundation
import Network
import Testing

import FlowGridCore
@testable import FlowGridGo

/// DirectLink 真实链路集成测试（swift-testing）：
/// Node 侧（DirectLinkManager + PeerSession）与 Mock 对端在 localhost 上跑完整协议
/// —— IDENTIFY 握手、心跳、延迟、剪贴板双向、文件传输双向、配对码不一致拒连。
@Suite(.serialized)
@MainActor
final class DirectLinkTransportTests {

    // MARK: - Mock 对端

    @MainActor
    private final class MockPeer {
        let listener: NWListener
        let key: Data
        var connection: NWConnection?
        var buffer = Data()
        var outSeq: UInt16 = 100
        var received: [(type: UInt8, payload: Data)] = []
        var onFrame: ((UInt8, Data, MockPeer) -> Void)?
        var identifyName = "Mock Peer"
        var acceptedConnections = 0

        init(code: String) throws {
            listener = try NWListener(using: .tcp, on: .any)
            key = SessionKey.derive(code: code)
        }

        var port: UInt16? { listener.port?.rawValue }

        func start() {
            listener.newConnectionHandler = { [weak self] connection in
                MainActor.assumeIsolated {
                    guard let self else { return }
                    self.acceptedConnections += 1
                    self.connection = connection
                    connection.stateUpdateHandler = { state in
                        MainActor.assumeIsolated {
                            if case .ready = state {
                                self.readLoop()
                                self.sendHandshake()
                            }
                        }
                    }
                    connection.start(queue: .main)
                }
            }
            listener.start(queue: .main)
        }

        func stop() {
            connection?.cancel()
            listener.cancel()
        }

        /// 模拟对端掉线：关闭当前连接（监听继续，供重连测试）。
        func dropConnection() {
            connection?.cancel()
            connection = nil
        }

        func sendFrame(_ type: UInt8, _ payload: Data) {
            guard let connection else { return }
            outSeq = outSeq &+ 1
            guard let wire = try? GoFrameCodec.encodeWithHMAC(
                frameType: type, sequence: outSeq, timestamp: 1_700_000_000_000,
                payload: payload, hmacKey: key
            ) else { return }
            connection.send(content: TCPFraming.encode(wire), completion: .contentProcessed { _ in })
        }

        private func sendHandshake() {
            sendFrame(FGFrameType.identify, identifyPayload(seqInit: outSeq &+ 1))
            sendFrame(FGFrameType.identifyResponse, responsePayload())
            sendFrame(FGFrameType.heartbeat, Data())
        }

        private func identifyPayload(seqInit: UInt16) -> Data {
            var data = Data([1, FGPlatform.windows.rawValue, 0x03, FGCapabilities.directLink.rawValue])
            // Seq Init 为 UInt32 BE（§4.2）
            data.append(0)
            data.append(0)
            data.append(UInt8(seqInit >> 8))
            data.append(UInt8(seqInit & 0xFF))
            data.append(contentsOf: Array(identifyName.utf8))
            data.append(0)
            return data
        }

        private func responsePayload() -> Data {
            var data = Data([0x00, FGPlatform.windows.rawValue, FGCapabilities.directLink.rawValue])
            data.append(contentsOf: Array(identifyName.utf8))
            data.append(0)
            return data
        }

        func readLoop() {
            connection?.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] data, _, isComplete, error in
                MainActor.assumeIsolated {
                    guard let self else { return }
                    if let data, !data.isEmpty {
                        self.buffer.append(data)
                        while let record = TCPFraming.takeRecord(from: &self.buffer) {
                            if let frame = try? GoFrameCodec.decodeWithHMAC(record, hmacKey: self.key) {
                                self.received.append((frame.frameType, frame.payload))
                                self.onFrame?(frame.frameType, frame.payload, self)
                            }
                        }
                    }
                    if error != nil || isComplete { return }
                    self.readLoop()
                }
            }
        }

        func firstReceived(_ type: UInt8) -> Data? {
            received.first { $0.type == type }?.payload
        }

        func count(of type: UInt8) -> Int {
            received.filter { $0.type == type }.count
        }
    }

    // MARK: - 事件收集

    @MainActor
    private final class EventSink {
        var events: [ConnectionEvent] = []

        func status() -> ConnectionStatus? {
            events.compactMap { if case let .status(s) = $0 { s } else { nil } }.last
        }
        func lastLatency() -> (Double, FGLatencyQuality)? {
            events.compactMap { if case let .latency(ms, _, q) = $0 { (ms, q) } else { nil } }.last
        }
        func clipboardIn() -> (String, Data)? {
            events.compactMap { if case let .clipboardIn(mime, data) = $0 { (mime, data) } else { nil } }.last
        }
        func filesReceived() -> [(String, String?, Int)] {
            events.compactMap {
                if case let .fileReceived(name, path, code) = $0 { (name, path, code) } else { nil }
            }
        }
        func transferFinished() -> Int? {
            events.compactMap {
                if case let .transferFinished(_, _, code) = $0 { code } else { nil }
            }.last
        }
    }

    // MARK: - 等待工具

    struct WaitTimeout: Error, CustomStringConvertible {
        let description: String
    }

    private func waitFor(
        _ label: String,
        timeout: TimeInterval = 5,
        _ condition: () -> Bool
    ) async throws {
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            if condition() { return }
            try await Task.sleep(for: .milliseconds(50))
        }
        throw WaitTimeout(description: "等待超时：\(label)")
    }

    // MARK: - 用例：完整协议流程

    @Test func handshakeClipboardLatencyAndFileTransfers() async throws {
        let mock = try MockPeer(code: "1234")
        mock.start()
        try await waitFor("mock 监听端口就绪", timeout: 8) { (mock.port ?? 0) > 0 }

        let manager = DirectLinkManager()
        let sink = EventSink()
        manager.onEvent = { event in sink.events.append(event) }
        let receiveDir = FileManager.default.temporaryDirectory
            .appendingPathComponent("flowgrid-test-recv-\(UUID().uuidString)", isDirectory: true)
        manager.updateReceiveDirectory(receiveDir.path)
        defer {
            manager.disconnect()
            mock.stop()
            try? FileManager.default.removeItem(at: receiveDir)
        }

        mock.onFrame = { type, payload, peer in
            switch type {
            case FGFrameType.heartbeat:
                peer.sendFrame(FGFrameType.heartbeat, Data())
            case FGFrameType.latencyPing:
                peer.sendFrame(FGFrameType.latencyPong, payload)
            case FGFrameType.fileOffer:
                let id = UInt16(payload[payload.startIndex]) << 8 | UInt16(payload[payload.startIndex + 1])
                peer.sendFrame(FGFrameType.fileAck, Data([UInt8(id >> 8), UInt8(id & 0xFF), 0x00]))
            default:
                break
            }
        }

        manager.connect(address: "127.0.0.1:\(mock.port!)", pairingCode: "1234")

        // 1) 握手
        try await waitFor("握手完成") { sink.status()?.isConnected == true }
        #expect(sink.status() == .connected(peerName: "Mock Peer"), "实际状态：\(String(describing: sink.status()))")

        // 2) 延迟 ping/pong（2s 首拍）
        try await waitFor("首个延迟样本", timeout: 6) { sink.lastLatency() != nil }
        let latency = try #require(sink.lastLatency())
        #expect(latency.0 > 0)

        // 3) 心跳互发
        try await waitFor("心跳帧到达", timeout: 6) { mock.count(of: FGFrameType.heartbeat) > 0 }

        // 4) 剪贴板 A → mock
        manager.sendClipboard(mime: "text/plain", data: Data("hello-from-mac".utf8))
        try await waitFor("剪贴板帧到达 mock") { mock.firstReceived(FGFrameType.clipboard) != nil }
        let outPayload = try #require(mock.firstReceived(FGFrameType.clipboard))
        let outClip = try #require(GoPayloads.parseClipboard(outPayload))
        #expect(outClip.mime == "text/plain")
        #expect(outClip.data == Data("hello-from-mac".utf8))

        // 5) 剪贴板 mock → A
        mock.sendFrame(FGFrameType.clipboard, clipboardPayload(mime: "text/plain", data: Data("from-mock".utf8)))
        try await waitFor("剪贴板入站事件") { sink.clipboardIn() != nil }
        let incoming = try #require(sink.clipboardIn())
        #expect(incoming.0 == "text/plain")
        #expect(incoming.1 == Data("from-mock".utf8))

        // 6) 文件 A → mock（2 块；mock 收齐后回 ACK 完成）
        let outgoing = Data((0..<300).map { UInt8($0 % 251) })
        var chunkCount = 0
        mock.onFrame = { type, payload, peer in
            switch type {
            case FGFrameType.heartbeat:
                peer.sendFrame(FGFrameType.heartbeat, Data())
            case FGFrameType.fileOffer:
                let id = UInt16(payload[payload.startIndex]) << 8 | UInt16(payload[payload.startIndex + 1])
                peer.sendFrame(FGFrameType.fileAck, Data([UInt8(id >> 8), UInt8(id & 0xFF), 0x00]))
            case FGFrameType.fileChunk:
                chunkCount += 1
                if chunkCount >= 2 {
                    let id = UInt16(payload[payload.startIndex]) << 8 | UInt16(payload[payload.startIndex + 1])
                    peer.sendFrame(FGFrameType.fileAck, Data([UInt8(id >> 8), UInt8(id & 0xFF), 0x05]))
                }
            default:
                break
            }
        }
        try manager.offerFile(name: "out.bin", data: outgoing)
        try await waitFor("文件发送完成", timeout: 6) { sink.transferFinished() != nil }
        #expect(sink.transferFinished() == 0)
        #expect(chunkCount >= 2)

        // 7) 文件 mock → A（自动接收 + SHA-256 校验 + 落盘）
        mock.onFrame = nil
        let ackBaseline = mock.count(of: FGFrameType.fileAck)
        let incomingData = Data((0..<260).map { UInt8($0 % 249) })
        mock.sendFrame(FGFrameType.fileOffer, fileOfferPayload(id: 7, data: incomingData, name: "in.bin"))
        try await waitFor("接收方 ACK") { mock.count(of: FGFrameType.fileAck) > ackBaseline }

        var chunkIndex: UInt32 = 0
        var offset = 0
        while offset < incomingData.count {
            let end = min(offset + 249, incomingData.count)
            var chunk = Data([0x00, 0x07])
            chunk.append(UInt8((chunkIndex >> 24) & 0xFF))
            chunk.append(UInt8((chunkIndex >> 16) & 0xFF))
            chunk.append(UInt8((chunkIndex >> 8) & 0xFF))
            chunk.append(UInt8(chunkIndex & 0xFF))
            chunk.append(incomingData[offset..<end])
            mock.sendFrame(FGFrameType.fileChunk, chunk)
            chunkIndex += 1
            offset = end
        }
        try await waitFor("文件接收完成", timeout: 6) { !sink.filesReceived().isEmpty }
        let received = try #require(sink.filesReceived().last)
        #expect(received.0 == "in.bin")
        #expect(received.2 == 0)
        let savedPath = try #require(received.1)
        #expect(try Data(contentsOf: URL(fileURLWithPath: savedPath)) == incomingData)
    }

    // MARK: - 用例：对端掉线 → 按协议退避自动重连

    @Test func reconnectsAfterPeerDrop() async throws {
        let mock = try MockPeer(code: "1234")
        mock.start()
        try await waitFor("mock 监听端口就绪", timeout: 8) { (mock.port ?? 0) > 0 }

        let manager = DirectLinkManager()
        let sink = EventSink()
        manager.onEvent = { event in sink.events.append(event) }
        let receiveDir = FileManager.default.temporaryDirectory
            .appendingPathComponent("flowgrid-test-recv-\(UUID().uuidString)", isDirectory: true)
        manager.updateReceiveDirectory(receiveDir.path)
        defer {
            manager.disconnect()
            mock.stop()
            try? FileManager.default.removeItem(at: receiveDir)
        }

        mock.onFrame = { type, _, peer in
            if type == FGFrameType.heartbeat { peer.sendFrame(FGFrameType.heartbeat, Data()) }
        }

        manager.connect(address: "127.0.0.1:\(mock.port!)", pairingCode: "1234")
        try await waitFor("首次连接") { sink.status()?.isConnected == true }
        let firstAccepts = mock.acceptedConnections

        mock.dropConnection()
        try await waitFor("自动重连成功", timeout: 6) {
            if case .connected = sink.status(), mock.acceptedConnections > firstAccepts {
                return true
            }
            return false
        }
    }

    // MARK: - 用例：配对码不一致 → 帧校验失败拒连

    @Test func wrongPairingCodeRejected() async throws {
        let mock = try MockPeer(code: "2222")
        mock.start()
        try await waitFor("mock 监听端口就绪", timeout: 8) { (mock.port ?? 0) > 0 }

        let manager = DirectLinkManager()
        let sink = EventSink()
        manager.onEvent = { event in sink.events.append(event) }
        defer {
            manager.disconnect()
            mock.stop()
        }

        manager.connect(address: "127.0.0.1:\(mock.port!)", pairingCode: "1111")

        try await waitFor("连接被拒", timeout: 8) {
            if case .failed = sink.status() { return true }
            return false
        }
        guard case let .failed(reason) = sink.status() else {
            Issue.record("应因 HMAC 校验失败被拒，实际：\(String(describing: sink.status()))")
            return
        }
        #expect(reason.contains("校验"), "实际原因：\(reason)")
    }

    // MARK: - 载荷构造（mock 侧，按 PROTOCOL §5.3/§7.4 手工编码）

    private func clipboardPayload(mime: String, data: Data) -> Data {
        let mimeBytes = Array(mime.utf8)
        var payload = Data([UInt8(mimeBytes.count)])
        payload.append(contentsOf: mimeBytes)
        let total = UInt32(data.count)
        payload.append(UInt8((total >> 24) & 0xFF))
        payload.append(UInt8((total >> 16) & 0xFF))
        payload.append(UInt8((total >> 8) & 0xFF))
        payload.append(UInt8(total & 0xFF))
        payload.append(0)
        payload.append(1)
        payload.append(data)
        return payload
    }

    private func fileOfferPayload(id: UInt16, data: Data, name: String) -> Data {
        var payload = Data([UInt8(id >> 8), UInt8(id & 0xFF)])
        let size = UInt64(data.count)
        for shift in stride(from: 56, through: 0, by: -8) {
            payload.append(UInt8((size >> UInt64(shift)) & 0xFF))
        }
        payload.append(contentsOf: SHA256.hash(data: data))
        let nameBytes = Array(name.utf8)
        payload.append(UInt8(nameBytes.count))
        payload.append(contentsOf: nameBytes)
        return payload
    }
}
