import Foundation

/// 延迟分级（协议 §9.2）：<2ms 好 / 2–4ms 警告 / >4ms 差；离线为 idle。
public enum LatencyTier: String, Sendable {
    case good
    case warn
    case bad
    case idle

    public static func classify(ms: Double) -> LatencyTier {
        if ms < FGProtocolConstants.latencyGoodMs { return .good }
        if ms < FGProtocolConstants.latencyWarnMs { return .warn }
        return .bad
    }

    public init(quality: FGLatencyQuality) {
        switch quality {
        case .good: self = .good
        case .warn: self = .warn
        case .bad: self = .bad
        }
    }

    /// 与设计稿 lat--good/warn/bad/idle 类一致的展示标签。
    public var displayText: String {
        switch self {
        case .good: "好"
        case .warn: "警告"
        case .bad: "差"
        case .idle: "离线"
        }
    }
}

public extension Double {
    /// 设计稿统一格式：一位小数 + "ms"。
    var latencyText: String { String(format: "%.1fms", self) }
}
