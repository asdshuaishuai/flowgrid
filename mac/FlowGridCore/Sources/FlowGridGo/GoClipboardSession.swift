import CFlowGrid
import Foundation

/// 剪贴板会话（协议 §5.3）：发送分段、接收重组/去重/换代/超时由 Go 库执行；
/// 重组完成的条目在 Go 侧按到达顺序排队，`takePending()` 逐条取走（上限 200 条）。
public final class GoClipboardSession: @unchecked Sendable {
    public struct Received: Equatable, Sendable {
        public let mime: String
        public let data: Data
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

    deinit {
        fg_clipboard_session_free(handle)
    }
}
