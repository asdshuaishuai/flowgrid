import CFlowGrid
import Foundation

/// 心跳跟踪：每 2s 调 tick()；3 次未收到心跳判定丢失（回调闩锁仅触发一次/轮，C 侧无回调，由调用方处理返回的 lost）。
public final class GoHeartbeatTracker: @unchecked Sendable {
    private let handle: Int

    public init() throws {
        let h: Int = fg_heartbeat_tracker_new()
        self.handle = try fgRequireHandle(h)
    }

    public func receivedHeartbeat() {
        fg_heartbeat_received(handle)
    }

    /// true = 连接丢失；false = 存活。
    public func tick() -> Bool {
        fg_heartbeat_tick(handle) == 1
    }

    deinit {
        fg_heartbeat_tracker_free(handle)
    }
}
