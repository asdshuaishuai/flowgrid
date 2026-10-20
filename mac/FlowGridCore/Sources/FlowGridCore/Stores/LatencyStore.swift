import Foundation
import Observation

/// 各设备链路延迟的集中状态（UI 唯一读点）。
/// 运行时由 DirectLink 会话把真实 ping/pong 样本喂入（PeerSession）；
/// 测试可手工 apply 验证 UI 逻辑。
@MainActor
@Observable
public final class LatencyStore {
    public struct Entry: Equatable, Sendable {
        public var ms: Double
        public var tier: LatencyTier
        public var lossPercent: Double?
    }

    public private(set) var byDevice: [String: Entry] = [:]
    /// 每设备最近 12 个采样（图表柱）。
    public private(set) var history: [String: [Double]] = [:]

    public init() {}

    public func apply(deviceID: String, ms: Double, tier: LatencyTier? = nil, lossPercent: Double? = nil) {
        let resolvedTier = tier ?? LatencyTier.classify(ms: ms)
        let previousLoss = byDevice[deviceID]?.lossPercent
        byDevice[deviceID] = Entry(ms: ms, tier: resolvedTier, lossPercent: lossPercent ?? previousLoss)

        var samples = history[deviceID] ?? []
        samples.append(ms)
        if samples.count > 12 {
            samples.removeFirst(samples.count - 12)
        }
        history[deviceID] = samples
    }

    public func entry(_ deviceID: String) -> Entry? {
        byDevice[deviceID]
    }

    public func remove(deviceID: String) {
        byDevice[deviceID] = nil
        history[deviceID] = nil
    }

    public func mean(_ deviceID: String) -> Double? {
        guard let samples = history[deviceID], !samples.isEmpty else { return nil }
        return samples.reduce(0, +) / Double(samples.count)
    }

    /// 图表用：固定 12 柱，不足前补 0（UI 画空柱）。
    public func chartSamples(_ deviceID: String) -> [Double] {
        let samples = history[deviceID] ?? []
        if samples.count >= 12 { return Array(samples.suffix(12)) }
        return Array(repeating: 0, count: 12 - samples.count) + samples
    }
}
