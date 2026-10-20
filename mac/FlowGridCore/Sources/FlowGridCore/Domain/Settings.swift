import Foundation

/// 优先链路设置。
public enum PreferredLink: String, CaseIterable, Sendable {
    case intelligentFirst = "IntelligentLink 优先"
    case directFirst = "DirectLink 优先"
    case intelligentOnly = "仅 IntelligentLink"
}

/// 剪贴板历史保留条数档位（协议桥暂存上限 200）。
public enum ClipboardHistoryLimit: Int, CaseIterable, Sendable {
    case fifty = 50
    case hundred = 100
    case twoHundred = 200

    public var displayText: String { "\(rawValue) 条" }
}

/// 应用设置（演示阶段内存态；持久化随传输阶段落地到
/// ~/Library/Application Support/FlowGrid/settings.yaml，见 PROTOCOL §13.2）。
public struct AppSettings: Sendable, Equatable {
    // 连接
    public var preferredLink: PreferredLink = .intelligentFirst
    public var autoRoute = true
    public var launchAtLogin = true

    // 输入
    public var keymapEnabled = true
    public var mouseSmoothing = false
    public var edgeSwitchThresholdPx = 8
    public var keyRepeatFollowsTarget = false
    public var sendRawScanCodes = false

    // 剪贴板
    public var clipboardSync = true
    public var clipboardSyncImages = true
    public var ignorePasswordManagers = true
    public var autoWriteLocal = false
    public var clipboardHistoryLimit: ClipboardHistoryLimit = .fifty
    public var excludedAppsCount = 3

    // 文件接收
    public var fileAutoAccept = true
    public var fileAskBeforeReceive = false
    public var clearRecordsOnQuit = false
    public var receiveDirectory = "~/Downloads/FlowGrid"

    // 安全
    public var requireConfirmNewDevice = true

    // 本机配对码（DirectLink MVP 的 HMAC 共享秘密；两端需一致）
    public var localPairingCode = "8421"

    // 延迟告警（阈值默认取协议 LatencyAlertThresholdMs = 5）
    public var latencyAlertThresholdMs: Double = FGProtocolConstants.latencyAlertThresholdMs
    public var latencyAlertConsecutive = FGProtocolConstants.latencyAlertConsecutive
    public var autoSwitchOnAlert = true

    public init() {}
}
