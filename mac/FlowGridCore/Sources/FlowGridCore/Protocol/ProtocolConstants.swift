// 协议常量与枚举：与 design/PROTOCOL.md / protocol Go 库逐值对应的唯一来源。
// 纯 Swift、无 C 依赖；FFI 封装（FlowGridGo）与 UI（FlowGridUI）共用此处定义。

public enum FGFrameType {
    public static let keyDown: UInt8 = 0x01
    public static let keyUp: UInt8 = 0x02
    public static let mouseMove: UInt8 = 0x03
    public static let mouseButton: UInt8 = 0x04
    public static let scroll: UInt8 = 0x05
    public static let gesture: UInt8 = 0x06
    public static let clipboard: UInt8 = 0x07
    public static let heartbeat: UInt8 = 0x08
    public static let latencyPing: UInt8 = 0x09
    public static let latencyPong: UInt8 = 0x0A
    public static let identify: UInt8 = 0x10
    public static let identifyResponse: UInt8 = 0x11
    public static let disconnect: UInt8 = 0x20
    public static let fileOffer: UInt8 = 0x21
    public static let fileAck: UInt8 = 0x22
    public static let fileChunk: UInt8 = 0x23
    public static let fileAbort: UInt8 = 0x24
    public static let error: UInt8 = 0xFF
}

public enum FGProtocolConstants {
    public static let protocolVersion: UInt8 = 1
    public static let frameVersion: UInt8 = 1
    public static let keyMapVersion = "1.0"
    public static let maxPayloadLen = 255
    public static let headerSize = 12
    public static let hmacSize = 8
    public static let defaultPort: UInt16 = 24801

    /// 剪贴板单条上限（协议 §5.3：255 × 248）。
    public static let maxClipboardSize = 63_240
    public static let maxFileSize = 16 << 20          // 16 MiB
    public static let maxFileChunk = 249              // MaxPayloadLen - 头部
    public static let maxFileNameLen = 212
    public static let fileOfferTimeoutSeconds = 30
    public static let fileIdleTimeoutSeconds = 10
    public static let clipboardTimeoutSeconds = 5

    /// 延迟分级（协议 §9.2 / heartbeat.go）。
    public static let latencyGoodMs = 2.0
    public static let latencyWarnMs = 4.0
    /// 连续超阈值告警（UI 告警阈值默认值）。
    public static let latencyAlertThresholdMs = 5.0
    public static let latencyAlertConsecutive = 3

    public static let heartbeatIntervalSeconds = 2.0
    public static let heartbeatTimeoutCount = 3

    /// 重连（§10.1）。
    public static let reconnectMaxAttempts = 5
    public static let reconnectInitBackoffMs = 200.0
    public static let reconnectMaxBackoffMs = 5_000.0
    public static let reconnectJitter = 0.2
}

/// 修饰键位掩码（协议 §6.2，与 protocol/keymap.go 同值）。
public enum FGModifier {
    public static let leftCtrl: UInt8 = 0x01
    public static let leftShift: UInt8 = 0x02
    public static let leftAlt: UInt8 = 0x04
    public static let leftMeta: UInt8 = 0x08
    public static let rightCtrl: UInt8 = 0x10
    public static let rightShift: UInt8 = 0x20
    public static let rightAlt: UInt8 = 0x40
    public static let rightMeta: UInt8 = 0x80

    public static let ctrl: UInt8 = leftCtrl | rightCtrl
    public static let shift: UInt8 = leftShift | rightShift
    public static let alt: UInt8 = leftAlt | rightAlt
    public static let meta: UInt8 = leftMeta | rightMeta
}

public enum FGPlatform: UInt8, CaseIterable, Sendable {
    case macOS = 0x01
    case windows = 0x02
    case linux = 0x03
    case android = 0x04
    case iOS = 0x05

    public var displayName: String {
        switch self {
        case .macOS: "macOS"
        case .windows: "Windows"
        case .linux: "Linux"
        case .android: "Android"
        case .iOS: "iOS/iPadOS"
        }
    }
}

public struct FGCapabilities: OptionSet, Sendable {
    public let rawValue: UInt8
    public init(rawValue: UInt8) { self.rawValue = rawValue }

    public static let nearLink = FGCapabilities(rawValue: 0x01)
    public static let directLink = FGCapabilities(rawValue: 0x02)
    public static let wiFiDirect = FGCapabilities(rawValue: 0x04)
    public static let dtls13 = FGCapabilities(rawValue: 0x08)
    public static let clipboard = FGCapabilities(rawValue: 0x10)
    public static let fileTransfer = FGCapabilities(rawValue: 0x20)
}

/// fg_latency_latest / ClassifyLatency 的 0/1/2 分级。
public enum FGLatencyQuality: Int, Sendable {
    case good = 0
    case warn = 1
    case bad = 2
}

/// fg_classify_rssi 的 1..4 分级。
public enum FGSignalQuality: Int, Sendable {
    case weak = 1
    case fair = 2
    case good = 3
    case excellent = 4
}
