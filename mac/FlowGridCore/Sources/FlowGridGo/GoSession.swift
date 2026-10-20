import CFlowGrid
import Foundation

/// 会话出帧接缝：全局「Go 缓存、C 轮询」队列（v1.1 单连接）。
///
/// 客户端在传输就绪时 `setSink(true)`，然后周期调用 `pollOut()` 取走
/// 「帧类型 + 载荷」——序列号/时间戳与 HMAC 由客户端用帧级 API 自行附加
/// （见 docs/PROTOCOL-INTEGRATION.md §8）。未声明 sink 时发送接口会返回
/// notConnected（0x05），不会静默丢帧。
public enum GoSession {

    /// 声明传输就绪（true）/撤下并清空队列（false）。
    public static func setSink(_ ready: Bool) {
        fg_session_set_sink(ready ? 1 : 0)
    }

    public struct OutFrame: Equatable, Sendable {
        public let frameType: UInt8
        public let payload: Data
    }

    /// 取一帧待发；队列空返回 nil。
    public static func pollOut() -> OutFrame? {
        var outType: UInt8 = 0
        var outPayload: UnsafeMutablePointer<UInt8>?
        var outLen = 0
        let rc: Int32 = fg_session_poll_out(&outType, &outPayload, &outLen)
        guard rc == 1 else {
            if let outPayload { fg_free_buffer(outPayload) }
            return nil
        }
        let payload = fgConsumeBuffer(outPayload, count: outLen) ?? Data()
        return OutFrame(frameType: outType, payload: payload)
    }
}
