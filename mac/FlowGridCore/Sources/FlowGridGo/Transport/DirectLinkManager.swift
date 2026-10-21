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
    private enum Target {
        case address(String)
        case service(NWEndpoint)
    }
    private var lastTarget: Target?
    private var lastCode: String?
    private var userInitiatedClose = false
    private var reconnectAttempt = 0
    private var reconnectTask: Task<Void, Never>?

    private var localName: String {
        Host.current().localizedName ?? "Mac"
    }

    // mDNS 发现（§3.2）
    private var browser: NWBrowser?
    private var advertisedServiceName: String?
    public private(set) var discoveredPeers: [DiscoveredPeer] = []
    private var discoveredEndpoints: [String: NWEndpoint] = [:]
    private var mdnsPeers: [DiscoveredPeer] = []

    // 智能连接（LAN 私有链，§3.1）
    private var identity: GoLanID.Identity?
    private var announcer: GoLanID.Announcer?
    private var lanListener: GoLanID.Listener?
    private var trustChain: GoLanID.TrustChain?
    private var lanIDLink: LanIDLink?
    private var lanPeers: [GoLanID.Peer] = []

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
                        print("[FlowGrid] mDNS 广告就绪：\(self.advertisedServiceName ?? "?")")
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
            // mDNS 广告：_flowgrid._tcp + TXT（§3.2.1）
            let serviceName = localName
            var txt = NWTXTRecord()
            txt["version"] = String(FGProtocolConstants.protocolVersion)
            txt["platform"] = FGPlatform.macOS.mdnsName
            txt["name"] = serviceName
            txt["caps"] = "directlink,clipboard,filetransfer"
            txt["port"] = String(port)
            txt["encryption"] = "none"
            listener.service = NWListener.Service(name: serviceName, type: "_flowgrid._tcp", txtRecord: txt)
            advertisedServiceName = serviceName

            listener.start(queue: .main)
            self.listener = listener
            startBrowsing()
            startIntelligentLink()
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
        resetReconnectState()
        lastTarget = .address(trimmed)
        lastCode = pairingCode
        startClient(.address(trimmed), code: pairingCode)
    }

    /// 连接 mDNS 发现的设备（§3.2）。
    public func connect(serviceName: String, pairingCode: String) {
        guard session == nil else { return }
        guard let endpoint = discoveredEndpoints[serviceName] else {
            updateStatus(.failed(reason: "设备已离线，请等待重新发现"))
            return
        }
        resetReconnectState()
        lastTarget = .service(endpoint)
        lastCode = pairingCode
        startClient(.service(endpoint), code: pairingCode)
    }

    private func resetReconnectState() {
        userInitiatedClose = false
        reconnectAttempt = 0
        reconnectTask?.cancel()
        reconnectTask = nil
    }

    /// 建立客户端连接（手动/重连共用）。
    private func startClient(_ target: Target, code: String) {
        let connection: NWConnection
        switch target {
        case let .address(address):
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
            connection = NWConnection(host: NWEndpoint.Host(host), port: nwPort, using: .tcp)
        case let .service(endpoint):
            updateStatus(.connecting(address: "Bonjour 端点"))
            connection = NWConnection(to: endpoint, using: .tcp)
        }
        startSession(on: connection, role: .initiator, key: SessionKey.derive(code: code))
    }

    public func disconnect() {
        userInitiatedClose = true
        reconnectAttempt = 0
        reconnectTask?.cancel()
        reconnectTask = nil
        lastTarget = nil
        lastCode = nil
        session?.close(reason: nil)
        session = nil
        GoSession.setSink(false)
        updateStatus(listener == nil ? .idle : .listening(port: listenPort))
    }

    // MARK: - mDNS 浏览（§3.2）

    /// 开始浏览 `_flowgrid._tcp`；随监听一起启动，也可单独用于发现。
    public func startBrowsing() {
        guard browser == nil else { return }
        let browser = NWBrowser(for: .bonjour(type: "_flowgrid._tcp", domain: nil), using: .tcp)
        browser.browseResultsChangedHandler = { [weak self] results, _ in
            let snapshot = Array(results)
            Task { @MainActor in
                self?.applyDiscovery(snapshot)
            }
        }
        browser.stateUpdateHandler = { state in
            switch state {
            case .ready:
                print("[FlowGrid] mDNS 浏览就绪")
            case let .failed(error):
                print("[FlowGrid] mDNS 浏览失败：\(error.localizedDescription)")
            default:
                break
            }
        }
        browser.start(queue: .main)
        self.browser = browser
    }

    public func stopBrowsing() {
        browser?.cancel()
        browser = nil
        stopIntelligentLink()
        if !discoveredPeers.isEmpty {
            discoveredPeers = []
            discoveredEndpoints = [:]
            mdnsPeers = []
            onEvent?(.discovered([]))
        }
    }

    private func applyDiscovery(_ results: [NWBrowser.Result]) {
        var peers: [DiscoveredPeer] = []
        var endpoints: [String: NWEndpoint] = [:]
        for result in results {
            guard case let .service(serviceName, _, _, _) = result.endpoint else { continue }
            // 过滤自身广告（监听未就绪时不广告，不做过滤）
            if listenerReady, serviceName == advertisedServiceName { continue }
            var txt: NWTXTRecord?
            if case let .bonjour(record) = result.metadata { txt = record }
            let displayName = txt?["name"] ?? serviceName
            let platform = txt?["platform"].flatMap(FGPlatform.init(mdnsName:))
            let port = txt?["port"].flatMap { UInt16($0) }
            peers.append(DiscoveredPeer(id: serviceName, name: displayName, platform: platform, port: port))
            endpoints[serviceName] = result.endpoint
        }
        peers.sort { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
        print("[FlowGrid] mDNS 发现 \(peers.count) 台：\(peers.map(\.name).joined(separator: ", "))")
        mdnsPeers = peers
        discoveredEndpoints = endpoints
        refreshDiscoveredPeers()
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
        guard !userInitiatedClose, let target = lastTarget, let code = lastCode else { return }
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
            self.startClient(target, code: code)
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

    // MARK: - 智能连接（LAN 私有链，§3.1）

    private func startIntelligentLink() {
        guard let identity = loadOrCreateIdentity() else { return }
        self.identity = identity
        guard let announcer = try? GoLanID.Announcer(
            identity: identity,
            platform: .macOS,
            capabilities: [.directLink, .clipboard, .fileTransfer],
            port: listenPort,
            name: localName
        ) else { return }
        guard let lanListener = try? GoLanID.Listener() else { return }
        guard let trustChain = try? GoLanID.TrustChain(identity: identity) else { return }
        self.announcer = announcer
        self.lanListener = lanListener
        self.trustChain = trustChain

        let link = LanIDLink(announcer: announcer, listener: lanListener)
        link.onPeersChanged = { [weak self] peers in
            Task { @MainActor in
                guard let self else { return }
                self.lanPeers = peers
                self.refreshDiscoveredPeers()
            }
        }
        self.lanIDLink = link
        link.start()
        print("[FlowGrid] 智能连接已启动（LAN 私有链发现）")
    }

    private func stopIntelligentLink() {
        lanIDLink?.stop()
        lanIDLink = nil
        announcer = nil
        lanListener = nil
        trustChain = nil
        identity = nil
        lanPeers = []
    }

    private func refreshDiscoveredPeers() {
        var peers = mdnsPeers
        for peer in lanPeers {
            let trusted = trustChain?.isTrusted(peerID: peer.publicKey) ?? false
            peers.append(DiscoveredPeer(
                id: peer.fingerprint,
                name: peer.name,
                platform: peer.platform,
                port: peer.port,
                source: .lan,
                fingerprint: peer.fingerprint,
                trusted: trusted
            ))
        }
        discoveredPeers = peers
        onEvent?(.discovered(peers))
    }

    private func loadOrCreateIdentity() -> GoLanID.Identity? {
        let url = Self.identityFileURL()
        if let data = try? Data(contentsOf: url), let identity = try? GoLanID.Identity(importing: data) {
            return identity
        }
        guard let identity = try? GoLanID.Identity() else { return nil }
        if let exported = try? identity.exportPrivateKey() {
            try? exported.write(to: url)
        }
        return identity
    }

    private static func identityFileURL() -> URL {
        let dir = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/FlowGrid", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent("identity.key")
    }

    deinit {
        listener?.cancel()
        browser?.cancel()
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
