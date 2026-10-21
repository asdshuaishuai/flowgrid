import CFlowGrid
import Foundation

/// 剪贴板会话（协议 §5.3）：发送分段、接收重组/去重/换代/超时由 Go 库执行；
/// 重组完成的条目在 Go 侧按到达顺序排队，`takePending()` 逐条取走（上限 200 条）。
public final class GoClipboardSession: @unchecked Sendable {
    public struct Received: Equatable, Sendable {
        public let mime: String
        public let data: Data
    }

    /// 只读历史条目（协议 §5.3 会话队列）：独立于 pending 队列的非破坏性视图，
    /// 带单调 seq（客户端去重用）与重组完成的 Unix 毫秒时间戳。
    public struct HistoryEntry: Equatable, Sendable {
        public let mime: String
        public let data: Data
        public let seq: UInt64
        public let receivedAtMs: Int64
    }

    private let handle: Int

    public init() throws {
        let h: Int = fg_clipboard_session_new()
        self.handle = try fgRequireHandle(h)
    }

    /// 发送剪贴板；passes ≥ 2 用于丢包/乱序传输（协议 §5.3，接收侧按索引去重）。
    public func send(mime: String, data: Data, passes: Int = 1) throws {
        var dataBytes = [UInt8](data)
        let rc: Int32 = mime.withCString { m in
            dataBytes.withUnsafeMutableBufferPointer { buf in
                fg_clipboard_send(handle, UnsafeMutablePointer(mutating: m), buf.baseAddress, buf.count, Int32(max(passes, 1)))
            }
        }
        guard rc == 0 else { throw GoProtocolError(code: UInt8(truncatingIfNeeded: rc), operation: "clipboard.send") }
    }

    /// 处理入站 CLIPBOARD 帧载荷（分发的职责在调用方：按 FrameType 路由）。
    public func handleFrame(_ payload: Data) throws {
        var bytes = [UInt8](payload)
        let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
            fg_clipboard_handle_frame(handle, buf.baseAddress, buf.count)
        }
        guard rc == 0 else { throw GoProtocolError(code: UInt8(truncatingIfNeeded: rc), operation: "clipboard.handleFrame") }
    }

    /// 取一条重组完成的剪贴板；队列空（或句柄失效）返回 nil。
    public func takePending() -> Received? {
        var outMime: UnsafeMutablePointer<UInt8>?
        var outMimeLen = 0
        var outData: UnsafeMutablePointer<UInt8>?
        var outDataLen = 0
        let rc: Int32 = fg_clipboard_take_pending(handle, &outMime, &outMimeLen, &outData, &outDataLen)
        guard rc == 1 else {
            if let outMime { fg_free_buffer(outMime) }
            if let outData { fg_free_buffer(outData) }
            return nil
        }
        let mimeData = fgConsumeBuffer(outMime, count: outMimeLen) ?? Data()
        let payloadData = fgConsumeBuffer(outData, count: outDataLen) ?? Data()
        return Received(mime: String(data: mimeData, encoding: .utf8) ?? "", data: payloadData)
    }

    /// pending 队列积压条数（上限 200，超出最旧先丢）。
    public func pendingCount() -> Int {
        Int(fg_clipboard_pending_count(handle))
    }

    /// pending 队列因满被挤掉的累计条数；非 0 表示客户端轮询太慢、确实丢过数据
    /// （只读历史视图不受影响）。
    public func droppedCount() -> Int {
        Int(fg_clipboard_dropped(handle))
    }

    /// 只读历史条数（上限 50，独立于 pending 队列）；0 表示句柄失效或历史为空。
    public func historyCount() -> Int {
        Int(fg_clipboard_history_count(handle))
    }

    /// 按「0 = 最旧」非破坏性读一条历史；越界或句柄失效返回 nil。
    /// 反复读同一索引不影响 pending 队列，也不会消费历史。
    public func history(at index: Int) -> HistoryEntry? {
        var outMime: UnsafeMutablePointer<UInt8>?
        var outMimeLen = 0
        var outData: UnsafeMutablePointer<UInt8>?
        var outDataLen = 0
        var outSeq: UInt64 = 0
        var outReceivedAtMs: Int64 = 0
        let rc: Int32 = fg_clipboard_history_get(
            handle, Int32(index),
            &outMime, &outMimeLen, &outData, &outDataLen,
            &outSeq, &outReceivedAtMs
        )
        guard rc == 1 else {
            if let outMime { fg_free_buffer(outMime) }
            if let outData { fg_free_buffer(outData) }
            return nil
        }
        let mimeData = fgConsumeBuffer(outMime, count: outMimeLen) ?? Data()
        let payloadData = fgConsumeBuffer(outData, count: outDataLen) ?? Data()
        return HistoryEntry(
            mime: String(data: mimeData, encoding: .utf8) ?? "",
            data: payloadData,
            seq: outSeq,
            receivedAtMs: outReceivedAtMs
        )
    }

    /// 清空只读历史（pending 队列保持不动；seq 继续递增，避免旧 UI 快照把新条目
    /// 误认成已见过的序号）。
    public func clearHistory() {
        fg_clipboard_history_clear(handle)
    }

    deinit {
        fg_clipboard_session_free(handle)
    }
}
