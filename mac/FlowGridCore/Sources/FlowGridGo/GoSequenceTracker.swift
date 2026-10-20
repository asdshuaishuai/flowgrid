import CFlowGrid
import Foundation

/// 序列号跟踪：首帧须等于 seq_init；gap 前移重同步；重复/过期显式拒绝（收到即丢帧、不回 ERROR）。
public final class GoSequenceTracker: @unchecked Sendable {
    private let handle: Int

    public init(seqInit: UInt32) throws {
        let h: Int = fg_sequence_tracker_new(seqInit)
        self.handle = try fgRequireHandle(h)
    }

    public func next() -> UInt16 {
        fg_sequence_next(handle)
    }

    /// true = 接受；false = gap/重复/过期 —— 调用方丢弃该帧。
    public func check(_ received: UInt16) -> Bool {
        fg_sequence_check(handle, received) == 0
    }

    public func reset(seqInit: UInt32) {
        fg_sequence_reset(handle, seqInit)
    }

    deinit {
        fg_sequence_tracker_free(handle)
    }
}
