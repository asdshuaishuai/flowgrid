import CFlowGrid
import FlowGridCore
import Foundation

/// 延迟监测：fg_latency_monitor_*（协议 §9.2 状态机，单程延迟 = 半 RTT）。
public final class GoLatencyMonitor: @unchecked Sendable {
    public struct Sample: Equatable, Sendable {
        public let ms: Double
        public let jitter: Double
        public let quality: FGLatencyQuality
    }

    private let handle: Int

    public init(maxSamples: Int) throws {
        let h: Int = fg_latency_monitor_new(Int32(maxSamples))
        self.handle = try fgRequireHandle(h)
    }

    public func recordPing(sendTimeUs: UInt64) {
        fg_latency_record_ping(handle, sendTimeUs)
    }

    /// 返回 nil 表示该 PONG 被拒（时间戳非法/无匹配 ping），丢弃即可。
    @discardableResult
    public func recordPong(pongTimestampUs: UInt64, nowUs: UInt64) -> (ms: Double, jitter: Double)? {
        var ms = 0.0
        var jitter = 0.0
        let rc: Int32 = fg_latency_record_pong(handle, pongTimestampUs, nowUs, &ms, &jitter)
        guard rc == 0 else { return nil }
        return (ms, jitter)
    }

    public func latest() -> Sample? {
        var ms = 0.0
        var jitter = 0.0
        var quality: Int32 = 0
        let rc: Int32 = fg_latency_latest(handle, &ms, &jitter, &quality)
        guard rc == 0, let q = FGLatencyQuality(rawValue: Int(quality)) else { return nil }
        return Sample(ms: ms, jitter: jitter, quality: q)
    }

    public func consecutiveBad() -> Int {
        Int(fg_latency_consecutive_bad(handle))
    }

    public func stats() -> (min: Double, max: Double, avg: Double)? {
        var minV = 0.0
        var maxV = 0.0
        var avgV = 0.0
        let rc: Int32 = fg_latency_stats(handle, &minV, &maxV, &avgV)
        guard rc == 0 else { return nil }
        return (minV, maxV, avgV)
    }

    deinit {
        fg_latency_monitor_free(handle)
    }
}
