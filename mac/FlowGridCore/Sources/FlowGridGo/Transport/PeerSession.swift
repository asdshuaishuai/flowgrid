import FlowGridCore
import Foundation
import Network

/// 单个对端连接上的协议引擎：握手（IDENTIFY 双向）、序列号、心跳、延迟、
/// 会话层（剪贴板/文件）接线与 HAL 注入。
/// 全部在主 actor 上运行（NWConnection 回调经 assumeIsolated 归位）。
@MainActor
final class PeerSession {

    enum Role {
        case initiator
        case responder
    }

    // MARK: - 配置

    struct Configuration {
        let role: Role
        let hmacKey: Data
        let localName: String
        let receiveDirectory: @MainActor () -> URL?
        let alertThresholdMs: @MainActor () -> Double
        let alertConsecutive: @MainActor () -> Int
        let autoAcceptFiles: Bool
        let onEvent: (ConnectionEvent) -> Void
        let onStatus: (ConnectionStatus) -> Void
        let onClose: (String?, _ retryable: Bool) -> Void
    }

    // MARK: - 状态

    private let connection: NWConnection
    private let config: Configuration
    private let seqInit: UInt16
    private let outSeq: GoSequenceTracker
    private var inSeq: GoSequenceTracker?

    private let clipboard: GoClipboardSession
    private let fileSender: GoFileSender
    private let fileReceiver: GoFileReceiver
    private let latencyMonitor: GoLatencyMonitor
    private let heartbeatTracker: GoHeartbeatTracker
    private let injector = InputInjector()
    private var capture: InputCaptureTap?

    private var receiveBuffer = Data()
    private var handshakeStartedAt: DispatchTime?
    private var receivedIdentify = false
    private var receivedResponse = false
    private var responseSent = false
    private var identifySent = false
    private var running = false
    private var closed = false
    private var peer: PeerInfo?
    private var hmacFailures = 0
    private var ticker: Task<Void, Never>?
    private var tickCount = 0
    private var consecutiveOverThreshold = 0
    private var alertLatched = false

    // MARK: - Init

    init(connection: NWConnection, config: Configuration) throws {
        self.connection = connection
        self.config = config
        // 随机 seq_init（§4.2，防序列号冲突）
        self.seqInit = UInt16.random(in: 0...UInt16.max)
        self.outSeq = try GoSequenceTracker(seqInit: UInt32(seqInit))
        self.clipboard = try GoClipboardSession()
        self.fileSender = try GoFileSender()
        self.fileReceiver = try GoFileReceiver()
        self.latencyMonitor = try GoLatencyMonitor(maxSamples: 64)
        self.heartbeatTracker = try GoHeartbeatTracker()
    }

    var isRunning: Bool { running }

    // MARK: - 生命周期

    func start() {
        connection.stateUpdateHandler = { [weak self] state in
            MainActor.assumeIsolated {
                self?.handleConnectionState(state)
            }
        }
        connection.start(queue: .main)
        receiveLoop()
    }

    func close(reason: String?, retryable: Bool = true) {
        guard !closed else { return }
        closed = true
        running = false
        ticker?.cancel()
        ticker = nil
        stopCapture()
        // 对端可能停在「按住 Ctrl/鼠标左键」的状态上断线；注入的是 CGEvent，
        // 系统不会自己清状态，必须显式松开，否则本机一直卡着修饰键。
        injector.reset()
        GoSession.setSink(false)
        connection.cancel()
        config.onClose(reason, retryable)
    }

    // MARK: - 连接状态与接收

    private func handleConnectionState(_ state: NWConnection.State) {
        switch state {
        case .ready:
            guard !handshakeStartedAt.boolValue else { return }
            handshakeStartedAt = DispatchTime.now()
            config.onStatus(.handshaking)
            sendIdentify()
            startTicker()
        case let .failed(error):
            close(reason: "连接失败：\(error.localizedDescription)")
        case .cancelled:
            close(reason: nil)
        default:
            break
        }
    }

    private func receiveLoop() {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] data, _, isComplete, error in
            MainActor.assumeIsolated {
                guard let self else { return }
                if let data, !data.isEmpty {
                    self.receiveBuffer.append(data)
                    self.drainReceiveBuffer()
                }
                if let error {
                    self.close(reason: "接收失败：\(error.localizedDescription)")
                    return
                }
                if isComplete {
                    self.close(reason: "对端关闭连接")
                    return
                }
                self.receiveLoop()
            }
        }
    }

    private func drainReceiveBuffer() {
        while let record = TCPFraming.takeRecord(from: &receiveBuffer) {
            handleRecord(record)
        }
    }

    private func handleRecord(_ record: Data) {
        guard let frame = try? GoFrameCodec.decodeWithHMAC(record, hmacKey: config.hmacKey) else {
            hmacFailures += 1
            if hmacFailures >= 3 {
                close(reason: "帧校验连续失败（配对码不一致？）", retryable: false)
            }
            return
        }
        hmacFailures = 0
        handleFrame(frame)
    }

    // MARK: - 帧分发

    private func handleFrame(_ frame: GoFrameCodec.DecodedFrame) {
        // 首帧 IDENTIFY：先用载荷里的 seq_init 初始化入站序列号
        if frame.frameType == FGFrameType.identify {
            guard !receivedIdentify, let identify = GoIdentify.parse(frame.payload) else { return }
            let tracker = try? GoSequenceTracker(seqInit: UInt32(identify.seqInit))
            if let tracker {
                if !tracker.check(frame.sequence) {
                    tracker.reset(seqInit: UInt32(frame.sequence))
                    _ = tracker.check(frame.sequence)
                    config.onEvent(.log("seq_init 与首帧不一致，已重同步"))
                }
            }
            inSeq = tracker
            receivedIdentify = true
            peer = PeerInfo(
                name: identify.name.isEmpty ? "FlowGrid 对端" : identify.name,
                platform: identify.platform,
                role: identify.role,
                capabilities: identify.capabilities
            )
            if let peer { config.onEvent(.peer(peer)) }
            if !responseSent {
                responseSent = true
                sendResponse()
            }
            maybeCompleteHandshake()
            return
        }

        guard let inSeq else { return } // 握手前的其余帧丢弃
        guard inSeq.check(frame.sequence) else { return } // gap/重复/过期：丢帧不回 ERROR（§10.3）

        switch frame.frameType {
        case FGFrameType.identifyResponse:
            guard let response = GoIdentify.parseResponse(frame.payload) else { return }
            guard response.status == 0 else {
                close(reason: response.status == 1 ? "版本不兼容" : "对端拒绝连接", retryable: false)
                return
            }
            receivedResponse = true
            maybeCompleteHandshake()

        case FGFrameType.clipboard:
            try? clipboard.handleFrame(frame.payload)

        case FGFrameType.fileOffer, FGFrameType.fileChunk:
            _ = try? fileReceiver.handleFrame(frameType: frame.frameType, payload: frame.payload)

        case FGFrameType.fileAck, FGFrameType.fileAbort:
            try? fileSender.handleFrame(frameType: frame.frameType, payload: frame.payload)

        case FGFrameType.heartbeat:
            heartbeatTracker.receivedHeartbeat()
            // 收帧仅喂狗、不回帧：双侧各自 2s 周期发送（§9.1）。
            // 「收到即回」在双向角色（0x03）下会与对端形成无限回显风暴
            // （PROTOCOL-ISSUES P4；Windows 端已按此方案实现）。

        case FGFrameType.latencyPing:
            sendFrame(FGFrameType.latencyPong, frame.payload) // 原样回 T1（§9.2）

        case FGFrameType.latencyPong:
            handlePong(frame.payload)

        case FGFrameType.disconnect:
            close(reason: "对端断开连接")

        case FGFrameType.error:
            config.onEvent(.log("收到对端 ERROR 帧"))

        case FGFrameType.keyDown, FGFrameType.keyUp, FGFrameType.mouseMove,
             FGFrameType.mouseButton, FGFrameType.scroll:
            injector.inject(frameType: frame.frameType, payload: frame.payload)

        default:
            break
        }
    }

    private func handlePong(_ payload: Data) {
        guard payload.count >= 8 else { return }
        let bytes = [UInt8](payload)
        var timestamp: UInt64 = 0
        for byte in bytes[0..<8] { timestamp = (timestamp << 8) | UInt64(byte) }
        let nowUs = DispatchTime.now().uptimeNanoseconds / 1000
        guard let sample = latencyMonitor.recordPong(pongTimestampUs: timestamp, nowUs: nowUs) else { return }
        let quality = latencyMonitor.latest()?.quality ?? .bad
        config.onEvent(.latency(ms: sample.ms, jitter: sample.jitter, quality: quality))

        // 延迟告警：按用户配置的阈值/连续次数（设置改动即时生效）
        if sample.ms > config.alertThresholdMs() {
            consecutiveOverThreshold += 1
            if !alertLatched, consecutiveOverThreshold >= max(1, config.alertConsecutive()) {
                alertLatched = true
                config.onEvent(.latencyAlert(ms: sample.ms))
            }
        } else {
            consecutiveOverThreshold = 0
            alertLatched = false
        }
    }

    // MARK: - 握手

    private func sendIdentify() {
        guard !identifySent else { return }
        identifySent = true
        let caps: FGCapabilities = [.directLink, .clipboard, .fileTransfer]
        guard let payload = try? GoIdentify.encode(
            platform: .macOS, role: 0x03, capabilities: caps,
            seqInit: UInt32(seqInit), name: config.localName
        ) else { return }
        sendFrame(FGFrameType.identify, payload)
        drainOutbox()
    }

    private func sendResponse() {
        let caps: FGCapabilities = [.directLink, .clipboard, .fileTransfer]
        guard let payload = try? GoIdentify.encodeResponse(
            status: 0x00, platform: .macOS, capabilities: caps, name: config.localName
        ) else { return }
        sendFrame(FGFrameType.identifyResponse, payload)
        drainOutbox()
    }

    private func maybeCompleteHandshake() {
        guard !running, receivedIdentify, receivedResponse else { return }
        running = true
        GoSession.setSink(true)
        let name = peer?.name ?? "对端"
        config.onStatus(.connected(peerName: name))
    }

    // MARK: - 发送

    private func sendFrame(_ type: UInt8, _ payload: Data) {
        let nowUs = DispatchTime.now().uptimeNanoseconds / 1000
        guard let wire = try? GoFrameCodec.encodeWithHMAC(
            frameType: type,
            sequence: outSeq.next(),
            timestamp: nowUs,
            payload: payload,
            hmacKey: config.hmacKey
        ) else { return }
        connection.send(content: TCPFraming.encode(wire), completion: .contentProcessed { _ in })
    }

    /// 会话层出帧队列 → 传输。
    private func drainOutbox() {
        guard running else { return }
        while let out = GoSession.pollOut() {
            sendFrame(out.frameType, out.payload)
        }
    }

    // MARK: - 定时器（100ms 一拍；2s 心跳/延迟）

    private func startTicker() {
        guard ticker == nil else { return }
        ticker = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: .milliseconds(100))
                guard let self, !Task.isCancelled else { return }
                self.tick()
            }
        }
    }

    private func tick() {
        guard !closed else { return }
        tickCount += 1

        // 握手超时：5s 内未完成
        if !running, let started = handshakeStartedAt, DispatchTime.now().uptimeNanoseconds - started.uptimeNanoseconds > 5_000_000_000 {
            close(reason: "握手超时（配对码不一致或对端未响应）", retryable: false)
            return
        }
        guard running else { return }

        drainOutbox()

        // 文件发送推进（每拍最多 128 块 ≈ 32KB）
        _ = try? fileSender.pump(maxChunks: 512)
        drainOutbox()
        if let snapshot = try? fileSender.poll(), snapshot.state != .idle {
            switch snapshot.state {
            case .active:
                config.onEvent(.transferProgress(id: 0, sent: snapshot.sent, total: snapshot.total))
            case .done:
                config.onEvent(.transferFinished(id: 0, name: "", errorCode: 0))
            case .failed:
                config.onEvent(.transferFinished(id: 0, name: "", errorCode: snapshot.errorCode))
            case .idle:
                break
            }
        }

        // 剪贴板入站
        while let received = clipboard.takePending() {
            config.onEvent(.clipboardIn(mime: received.mime, data: received.data))
        }

        // 文件 offer / 完成
        while let offer = fileReceiver.takeOffer() {
            var accepted = false
            if config.autoAcceptFiles {
                try? fileReceiver.accept(id: offer.id)
                accepted = true
            }
            config.onEvent(.offerReceived(transferID: offer.id, name: offer.name, size: offer.size, accepted: accepted))
        }
        while let completion = fileReceiver.takeComplete() {
            deliverReceivedFile(completion)
        }

        // 2s 心跳 / 延迟
        if tickCount % 20 == 0 {
            sendFrame(FGFrameType.heartbeat, Data())
            if heartbeatTracker.tick() {
                close(reason: "心跳超时（对端无响应）")
                return
            }
            let nowUs = DispatchTime.now().uptimeNanoseconds / 1000
            latencyMonitor.recordPing(sendTimeUs: nowUs)
            if let payload = try? GoPayloads.latencyPing(timestamp: nowUs) {
                sendFrame(FGFrameType.latencyPing, payload)
            }
        }
        drainOutbox()
    }

    // MARK: - 收文件落盘（SHA-256 已由 Go 侧校验）

    private func deliverReceivedFile(_ completion: GoFileReceiver.Completion) {
        guard completion.errorCode == 0, let data = completion.data else {
            config.onEvent(.fileReceived(name: completion.name, savedPath: nil, errorCode: completion.errorCode))
            return
        }
        let safeName = URL(fileURLWithPath: completion.name).lastPathComponent
        let directory = config.receiveDirectory() ?? FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Downloads/FlowGrid", isDirectory: true)
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        var target = directory.appendingPathComponent(safeName.isEmpty ? "received-\(Int(Date().timeIntervalSince1970))" : safeName)
        if FileManager.default.fileExists(atPath: target.path) {
            let base = target.deletingPathExtension().lastPathComponent
            let ext = target.pathExtension
            var index = 2
            repeat {
                let name = ext.isEmpty ? "\(base) (\(index))" : "\(base) (\(index)).\(ext)"
                target = directory.appendingPathComponent(name)
                index += 1
            } while FileManager.default.fileExists(atPath: target.path)
        }
        do {
            try data.write(to: target)
            config.onEvent(.fileReceived(name: completion.name, savedPath: target.path, errorCode: 0))
        } catch {
            config.onEvent(.fileReceived(name: completion.name, savedPath: nil, errorCode: Int(error.localizedDescription.hashValue & 0xFF)))
        }
    }

    // MARK: - 对外动作（由 Manager 调用）

    func sendClipboard(mime: String, data: Data) {
        guard running else { return }
        try? clipboard.send(mime: mime, data: data, passes: 1) // TCP 可靠链路：单轮即可
        drainOutbox()
    }

    func offerFile(name: String, data: Data) throws -> UInt16 {
        let id = try fileSender.offer(name: name, data: data)
        drainOutbox()
        return id
    }

    @discardableResult
    func setCaptureEnabled(_ enabled: Bool) -> Bool {
        if enabled {
            if capture != nil { return true }
            guard running else { return false }
            guard InputCaptureTap.isTrusted else {
                config.onEvent(.log("未授予辅助功能权限，无法捕获本机键鼠"))
                return false
            }
            let tap = InputCaptureTap()
            let started = tap.start(
                targetPlatform: { [weak self] in self?.peer?.platform ?? .macOS }
            ) { [weak self] type, payload in
                guard let self, self.running else { return }
                self.sendFrame(type, payload)
                self.drainOutbox()
            }
            guard started else {
                config.onEvent(.log("CGEventTap 创建失败（需要辅助功能权限）"))
                return false
            }
            capture = tap
            return true
        }
        stopCapture()
        return true
    }

    private func stopCapture() {
        capture?.stop()
        capture = nil
    }
}

private extension Optional where Wrapped == DispatchTime {
    var boolValue: Bool {
        switch self {
        case .some: true
        case .none: false
        }
    }
}
