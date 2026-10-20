import CFlowGrid
import Foundation

/// 文件发送方（协议 §7.4）：OFFER → 接受后按 ≤249 B 分块 Pump，SHA-256 由库
/// 预先计算；进度轮询读到的 sent/total 是真实字节数（单调递增，补发不倒退）。
public final class GoFileSender: @unchecked Sendable {
    /// 与桥的 fileSender* 状态常量同序。
    public enum State: Int, Sendable {
        case idle = 0
        case active = 1
        case done = 2
        case failed = 3
    }

    public struct Snapshot: Equatable, Sendable {
        public let state: State
        public let sent: Int
        public let total: Int
        /// 0 表示无错误，否则为 §12 协议错误码。
        public let errorCode: Int
    }

    private let handle: Int

    public init() throws {
        let h: Int = fg_file_sender_new()
        self.handle = try fgRequireHandle(h)
    }

    /// 发起传输，返回 TransferID；成功后需 Pump 才会出帧。
    public func offer(name: String, data: Data) throws -> UInt16 {
        var dataBytes = [UInt8](data)
        var outID: UInt16 = 0
        let rc: Int32 = name.withCString { n in
            dataBytes.withUnsafeMutableBufferPointer { buf in
                fg_file_sender_offer(handle, UnsafeMutablePointer(mutating: n), buf.baseAddress, buf.count, &outID)
            }
        }
        guard rc == 0 else { throw GoProtocolError(code: UInt8(truncatingIfNeeded: rc), operation: "fileSender.offer") }
        return outID
    }

    /// 每轮最多发 maxChunks 个分块，返回实际发出数。事件循环里按 tick 调用。
    @discardableResult
    public func pump(maxChunks: Int = 8) throws -> Int {
        var outPumped: Int32 = 0
        let rc: Int32 = fg_file_sender_pump(handle, Int32(maxChunks), &outPumped)
        guard rc == 0 else { throw GoProtocolError(code: UInt8(truncatingIfNeeded: rc), operation: "fileSender.pump") }
        return Int(outPumped)
    }

    /// 处理入站 FILE_ACK / FILE_ABORT。
    public func handleFrame(frameType: UInt8, payload: Data) throws {
        var bytes = [UInt8](payload)
        let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
            fg_file_sender_handle_frame(handle, frameType, buf.baseAddress, buf.count)
        }
        guard rc == 0 else { throw GoProtocolError(code: UInt8(truncatingIfNeeded: rc), operation: "fileSender.handleFrame") }
    }

    /// 读取进度快照；读到 done/failed 后终态被消费（只报一次）。
    public func poll() throws -> Snapshot {
        var outState: Int32 = 0
        var outSent: Int32 = 0
        var outTotal: Int32 = 0
        var outErr: Int32 = 0
        let rc: Int32 = fg_file_sender_poll(handle, &outState, &outSent, &outTotal, &outErr)
        guard rc == 0 else { throw GoBridgeError.handleInvalid }
        return Snapshot(
            state: State(rawValue: Int(outState)) ?? .idle,
            sent: Int(outSent),
            total: Int(outTotal),
            errorCode: Int(outErr)
        )
    }

    deinit {
        fg_file_sender_free(handle)
    }
}
