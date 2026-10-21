import CFlowGrid
import Foundation

/// 文件接收方（协议 §7.4）：offer → 用户 Accept/Reject → 收块 → SHA-256 校验
/// 通过才交付（`takeComplete`）；未应答的 offer 超时作废由库处理。
public final class GoFileReceiver: @unchecked Sendable {
    public struct Offer: Equatable, Sendable {
        public let id: UInt16
        public let name: String
        public let size: UInt64
    }

    public struct Completion: Sendable {
        public let name: String
        public let data: Data?
        /// 0 表示校验通过；否则为 §12 协议错误码（数据不交付）。
        public let errorCode: Int
    }

    /// 入站传输进度的非破坏性快照（可每帧调用，不清终态、不消费文件数据）。
    public struct Progress: Equatable, Sendable {
        /// 0 空闲 / 1 传输中 / 2 完成 / 3 失败（终态由 `takeComplete()` 消费）。
        public let state: Int
        public let receivedBytes: Int
        public let totalBytes: Int
        /// 失败时的 §12 协议错误码；否则 0。
        public let errorCode: Int
    }

    private let handle: Int

    public init() throws {
        let h: Int = fg_file_receiver_new()
        self.handle = try fgRequireHandle(h)
    }

    /// 处理入站 FILE_* 帧；返回该帧是否属于文件传输协议（供分发器决定是否继续往下传）。
    @discardableResult
    public func handleFrame(frameType: UInt8, payload: Data) throws -> Bool {
        var bytes = [UInt8](payload)
        var outHandled: Int32 = 0
        let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
            fg_file_receiver_handle_frame(handle, frameType, buf.baseAddress, buf.count, &outHandled)
        }
        guard rc == 0 else { throw GoProtocolError(code: UInt8(truncatingIfNeeded: rc), operation: "fileReceiver.handleFrame") }
        return outHandled == 1
    }

    /// 取待用户确认的 offer；无则 nil（读取即消费）。
    public func takeOffer() -> Offer? {
        var outID: UInt16 = 0
        var outName: UnsafeMutablePointer<UInt8>?
        var outNameLen = 0
        var outSize: UInt64 = 0
        let rc: Int32 = fg_file_receiver_take_offer(handle, &outID, &outName, &outNameLen, &outSize)
        guard rc == 1 else {
            if let outName { fg_free_buffer(outName) }
            return nil
        }
        let nameData = fgConsumeBuffer(outName, count: outNameLen) ?? Data()
        return Offer(id: outID, name: String(data: nameData, encoding: .utf8) ?? "", size: outSize)
    }

    public func accept(id: UInt16) throws {
        let rc: Int32 = fg_file_receiver_accept(handle, id)
        guard rc == 0 else { throw GoProtocolError(code: UInt8(truncatingIfNeeded: rc), operation: "fileReceiver.accept") }
    }

    /// status 取 FILE_ACK 拒绝档：0x01 busy / 0x02 用户拒绝 / 0x03 超限。
    public func reject(id: UInt16, status: UInt8) throws {
        let rc: Int32 = fg_file_receiver_reject(handle, id, status)
        guard rc == 0 else { throw GoProtocolError(code: UInt8(truncatingIfNeeded: rc), operation: "fileReceiver.reject") }
    }

    /// 取一次终态结果（校验通过交付数据 / 失败带错误码）；无则 nil（读取即消费）。
    public func takeComplete() -> Completion? {
        var outName: UnsafeMutablePointer<UInt8>?
        var outNameLen = 0
        var outData: UnsafeMutablePointer<UInt8>?
        var outDataLen = 0
        var outErr: Int32 = 0
        let rc: Int32 = fg_file_receiver_take_complete(handle, &outName, &outNameLen, &outData, &outDataLen, &outErr)
        guard rc == 1 else {
            if let outName { fg_free_buffer(outName) }
            if let outData { fg_free_buffer(outData) }
            return nil
        }
        let nameData = fgConsumeBuffer(outName, count: outNameLen) ?? Data()
        let payload = fgConsumeBuffer(outData, count: outDataLen)
        return Completion(
            name: String(data: nameData, encoding: .utf8) ?? "",
            data: payload,
            errorCode: Int(outErr)
        )
    }

    /// 非破坏性读取入站传输进度，可每帧调用；句柄失效返回 nil。
    /// 轮询不清终态、不消费待取的文件数据（终态由 `takeComplete()` 取走）。
    public func pollProgress() -> Progress? {
        var outState: Int32 = 0
        var outReceived: Int32 = 0
        var outTotal: Int32 = 0
        var outErr: Int32 = 0
        let rc: Int32 = fg_file_receiver_poll(handle, &outState, &outReceived, &outTotal, &outErr)
        guard rc == 0 else { return nil }
        return Progress(
            state: Int(outState),
            receivedBytes: Int(outReceived),
            totalBytes: Int(outTotal),
            errorCode: Int(outErr)
        )
    }

    /// offer / 完成事件因队列上限被挤掉的累计条数。完成事件丢失会连带丢掉文件
    /// 数据；任一非 0 都说明客户端轮询太慢，应提高轮询频率或先 drain 再处理 UI。
    public func droppedCounts() -> (offers: Int, completes: Int) {
        var outOffers: Int32 = 0
        var outCompletes: Int32 = 0
        fg_file_receiver_dropped(handle, &outOffers, &outCompletes)
        return (Int(outOffers), Int(outCompletes))
    }

    deinit {
        fg_file_receiver_free(handle)
    }
}
