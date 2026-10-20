import Foundation

/// 连接状态（UI 只读镜像；真实连接由 FlowGridGo.DirectLinkManager 驱动）。
public enum ConnectionStatus: Equatable, Sendable {
    case idle
    case listening(port: UInt16)
    case connecting(address: String)
    case handshaking
    case connected(peerName: String)
    case failed(reason: String)

    public var displayText: String {
        switch self {
        case .idle: "未连接"
        case let .listening(port): "未连接 · 监听 \(port)"
        case let .connecting(address): "正在连接 \(address)"
        case .handshaking: "握手中"
        case let .connected(name): "已连接 \(name)"
        case let .failed(reason): "连接失败：\(reason)"
        }
    }

    public var isConnected: Bool {
        if case .connected = self { return true }
        return false
    }

    /// 正在连接/握手（禁用连接按钮等）。
    public var isBusy: Bool {
        switch self {
        case .connecting, .handshaking: true
        default: false
        }
    }
}

/// 对端身份（IDENTIFY 帧解析结果）。
public struct PeerInfo: Equatable, Sendable {
    public var name: String
    public var platform: FGPlatform
    /// 0x01 Host / 0x02 Target / 0x03 Both。
    public var role: UInt8
    public var capabilities: FGCapabilities

    public init(name: String, platform: FGPlatform, role: UInt8, capabilities: FGCapabilities) {
        self.name = name
        self.platform = platform
        self.role = role
        self.capabilities = capabilities
    }
}

/// 连接层事件：FlowGridGo 侧产生，App 层经 AppModel 消费。
public enum ConnectionEvent: Sendable {
    case status(ConnectionStatus)
    case peer(PeerInfo)
    case latency(ms: Double, jitter: Double, quality: FGLatencyQuality)
    case latencyAlert(ms: Double)
    case clipboardIn(mime: String, data: Data)
    case transferProgress(id: UInt16, sent: Int, total: Int)
    case transferFinished(id: UInt16, name: String, errorCode: Int)
    case offerReceived(transferID: UInt16, name: String, size: UInt64, accepted: Bool)
    case fileReceived(name: String, savedPath: String?, errorCode: Int)
    case log(String)
}

/// 传输控制面（实现在 FlowGridGo：DirectLink TCP + 协议会话接线）。
/// UI 包只依赖本协议与事件的镜像状态。
@MainActor
public protocol PeerConnectionControlling: AnyObject {
    var onEvent: ((ConnectionEvent) -> Void)? { get set }

    /// 开始监听（被控端）。端口按协议默认 24801。
    func startListening(port: UInt16)
    /// 本机配对码（派生 HMAC 会话密钥；对端需输入相同码）。
    func updateLocalPairingCode(_ code: String)
    /// 主动连接对端（控制端发起）。
    func connect(address: String, pairingCode: String)
    func disconnect()

    func sendClipboard(mime: String, data: Data)
    func offerFile(name: String, data: Data) throws
    /// 更新接收目录（接收端落盘位置）。
    func updateReceiveDirectory(_ path: String)
    /// 延迟告警配置（阈值 ms / 连续次数）。
    func updateLatencyAlert(thresholdMs: Double, consecutive: Int)
    /// 捕获本机键鼠并发送到对端（需要辅助功能权限）。返回是否成功生效。
    @discardableResult
    func setCaptureEnabled(_ enabled: Bool) -> Bool
    var accessibilityTrusted: Bool { get }
    var isConnected: Bool { get }
}
