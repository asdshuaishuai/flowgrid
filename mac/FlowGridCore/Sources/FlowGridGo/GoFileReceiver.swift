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

    deinit {
        fg_file_receiver_free(handle)
    }
}
