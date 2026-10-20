import FlowGridCore
import Foundation
import Network

/// DirectLink 连接管理：监听（被控端）+ 主动连接（控制端），
/// 单连接（v1.1 会话层出帧队列为全局单实例）。
@MainActor
public final class DirectLinkManager: PeerConnectionControlling {

    public var onEvent: ((ConnectionEvent) -> Void)?

    public private(set) var status: ConnectionStatus = .idle
    public var isConnected: Bool { session?.isRunning == true }
    public var accessibilityTrusted: Bool { InputCaptureTap.isTrusted }

    /// 接收目录（App 层按设置注入；缺省 ~/Downloads/FlowGrid）。
    public private(set) var receiveDirectory: URL?
    /// 自动接收文件（设计默认「自动接收已开」）。
    public var autoAcceptFiles = true
    /// 延迟告警配置（App 层按设置注入）。
    public private(set) var alertThresholdMs = FGProtocolConstants.latencyAlertThresholdMs
    public private(set) var alertConsecutive = FGProtocolConstants.latencyAlertConsecutive

    private var listener: NWListener?
    private var listenPort: UInt16 = FGProtocolConstants.defaultPort
    private var session: PeerSession?
    private var localKey = SessionKey.derive(code: "0000")
    private var listenerReady = false

    // 自动重连（§10.1：最多 5 次，200ms ×2 退避，±20% 抖动）
    private var lastAddress: String?
    private var lastCode: String?
    private var userInitiatedClose = false
    private var reconnectAttempt = 0
    private var reconnectTask: Task<Void, Never>?

    private var localName: String {
        Host.current().localizedName ?? "Mac"
    }

    public init() {}

    // MARK: - 监听

    public func startListening(port: UInt16 = FGProtocolConstants.defaultPort) {
        guard listener == nil else { return }
        guard let nwPort = NWEndpoint.Port(rawValue: port) else { return }
        listenPort = port
        do {
            let listener = try NWListener(using: .tcp, on: nwPort)
            listener.newConnectionHandler = { [weak self] connection in
                MainActor.assumeIsolated {
                    self?.acceptIncoming(connection)
                }
            }
            listener.stateUpdateHandler = { [weak self] state in
                MainActor.assumeIsolated {
                    guard let self else { return }
                    switch state {
                    case .ready:
                        self.listenerReady = true
                        if !self.isConnected {
                            self.updateStatus(.listening(port: self.listenPort))
                        }
                    case let .failed(error):
                        self.listenerReady = false
                        // 已有会话（主动连接成功）时不因监听失败覆盖连接状态
                        if self.session == nil {
                            self.updateStatus(.failed(reason: "监听失败：\(error.localizedDescription)"))
                        }
                    default:
                        break
                    }
                }
            }
            listener.start(queue: .main)
            self.listener = listener
            updateStatus(.listening(port: port))
        } catch {
            updateStatus(.failed(reason: "无法监听 \(port)：\(error.localizedDescription)"))
        }
    }

    public func updateLocalPairingCode(_ code: String) {
        localKey = SessionKey.derive(code: code)
    }

    private func acceptIncoming(_ connection: NWConnection) {
        guard session == nil else {
            connection.cancel() // 单连接：已有会话时拒绝新连接
            return
        }
        startSession(on: connection, role: .responder, key: localKey)
    }

    // MARK: - 连接

    public func connect(address: String, pairingCode: String) {
        guard session == nil else { return }
        let trimmed = address.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty else {
            updateStatus(.failed(reason: "请输入设备地址"))
            return
        }
        userInitiatedClose = false
        reconnectAttempt = 0
        reconnectTask?.cancel()
        reconnectTask = nil
        lastAddress = trimmed
        lastCode = pairingCode
        startClient(address: trimmed, code: pairingCode)
    }

    /// 建立客户端连接（手动/自动重连共用）。
    private func startClient(address: String, code: String) {
        let (host, port) = Self.parseAddress(address)
        guard let nwPort = NWEndpoint.Port(rawValue: port) else {
            updateStatus(.failed(reason: "端口无效：\(port)"))
            return
        }
        // 自身监听的就近地址：会与本进程的全局会话队列冲突
        if listenerReady, port == listenPort, Self.isLoopback(host) {
            updateStatus(.failed(reason: "不能连接本机自身（请使用另一台设备）"))
            return
        }
        updateStatus(.connecting(address: "\(host):\(port)"))
        let connection = NWConnection(host: NWEndpoint.Host(host), port: nwPort, using: .tcp)
        startSession(on: connection, role: .initiator, key: SessionKey.derive(code: code))
    }

    public func disconnect() {
        userInitiatedClose = true
        reconnectAttempt = 0
        reconnectTask?.cancel()
        reconnectTask = nil
        lastAddress = nil
        lastCode = nil
        session?.close(reason: nil)
        session = nil
        GoSession.setSink(false)
        updateStatus(listener == nil ? .idle : .listening(port: listenPort))
    }

    // MARK: - 会话装配

    private func startSession(on connection: NWConnection, role: PeerSession.Role, key: Data) {
        // 单会话竞态：同时发起/被连时只保留先到的一方
        guard session == nil else {
            connection.cancel()
            return
        }
        do {
            let session = try PeerSession(
                connection: connection,
                config: PeerSession.Configuration(
                    role: role,
                    hmacKey: key,
                    localName: localName,
                    receiveDirectory: { [weak self] in self?.receiveDirectory },
                    alertThresholdMs: { [weak self] in self?.alertThresholdMs ?? FGProtocolConstants.latencyAlertThresholdMs },
                    alertConsecutive: { [weak self] in self?.alertConsecutive ?? FGProtocolConstants.latencyAlertConsecutive },
                    autoAcceptFiles: autoAcceptFiles,
                    onEvent: { [weak self] event in
                        self?.onEvent?(event)
                    },
                    onStatus: { [weak self] status in
                        self?.updateStatus(status)
                    },
                    onClose: { [weak self] reason, retryable in
                        self?.handleSessionClosed(reason: reason, retryable: retryable)
                    }
                )
            )
            self.session = session
            session.start()
        } catch {
            updateStatus(.failed(reason: "会话初始化失败：\(error.localizedDescription)"))
        }
    }

    private func handleSessionClosed(reason: String?, retryable: Bool) {
        session = nil
        GoSession.setSink(false)
        if let reason {
            updateStatus(.failed(reason: reason))
            if retryable {
                scheduleReconnectIfNeeded()
            }
        } else {
            updateStatus(listener == nil ? .idle : .listening(port: listenPort))
        }
    }

    private func scheduleReconnectIfNeeded() {
        guard !userInitiatedClose, let address = lastAddress, let code = lastCode else { return }
        guard reconnectAttempt < FGProtocolConstants.reconnectMaxAttempts else {
            updateStatus(.failed(reason: "重连失败（已尝试 \(FGProtocolConstants.reconnectMaxAttempts) 次）"))
            return
        }
        reconnectAttempt += 1
        let attempt = reconnectAttempt
        updateStatus(.connecting(address: "重连中（第 \(attempt)/\(FGProtocolConstants.reconnectMaxAttempts) 次）…"))
        let delayMs = Self.backoffMilliseconds(attempt: attempt)
        reconnectTask?.cancel()
        reconnectTask = Task { [weak self] in
            try? await Task.sleep(for: .milliseconds(delayMs))
            guard let self, !Task.isCancelled, !self.userInitiatedClose, self.session == nil else { return }
            self.startClient(address: address, code: code)
        }
    }

    /// 指数退避 ±20% 抖动（§10.1：200ms → 400 → 800 → 1.6s → 3.2s，上限 5s）。
    static func backoffMilliseconds(attempt: Int) -> Int {
        let base = min(FGProtocolConstants.reconnectInitBackoffMs * pow(2, Double(attempt - 1)),
                       FGProtocolConstants.reconnectMaxBackoffMs)
        let jitter = Double.random(in: -FGProtocolConstants.reconnectJitter...FGProtocolConstants.reconnectJitter)
        return Int(base * (1 + jitter))
    }

    // MARK: - 动作

    public func sendClipboard(mime: String, data: Data) {
        session?.sendClipboard(mime: mime, data: data)
    }

    public func offerFile(name: String, data: Data) throws {
        _ = try session?.offerFile(name: name, data: data)
    }

    public func updateLatencyAlert(thresholdMs: Double, consecutive: Int) {
        alertThresholdMs = max(0.1, thresholdMs)
        alertConsecutive = max(1, consecutive)
    }

    public func updateReceiveDirectory(_ path: String) {
        let expanded = (path as NSString).expandingTildeInPath
        receiveDirectory = URL(fileURLWithPath: expanded, isDirectory: true)
    }

    @discardableResult
    public func setCaptureEnabled(_ enabled: Bool) -> Bool {
        guard let session, session.isRunning else { return false }
        return session.setCaptureEnabled(enabled)
    }

    deinit {
        listener?.cancel()
    }

    // MARK: - 工具

    private func updateStatus(_ status: ConnectionStatus) {
        self.status = status
        if status.isConnected {
            reconnectAttempt = 0
            reconnectTask?.cancel()
            reconnectTask = nil
        }
        onEvent?(.status(status))
    }

    static func isLoopback(_ host: String) -> Bool {
        host == "localhost" || host == "::1" || host.hasPrefix("127.")
    }

    static func parseAddress(_ text: String) -> (host: String, port: UInt16) {
        if let colon = text.lastIndex(of: ":"), text[text.index(after: colon)...].allSatisfy(\.isNumber) {
            let host = String(text[..<colon])
            let port = UInt16(text[text.index(after: colon)...]) ?? FGProtocolConstants.defaultPort
            return (host, port)
        }
        return (text, FGProtocolConstants.defaultPort)
    }
}
