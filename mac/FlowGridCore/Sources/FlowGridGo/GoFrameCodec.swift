import CFlowGrid
import FlowGridCore
import Foundation

/// 帧编解码：fg_frame_encode(_with_hmac) / fg_frame_decode(_with_hmac) / fg_frame_validate。
/// HMAC 尾部 8 字节；空 key 表示不校验（仅限 DTLS/TLS 已加密的通道，见协议 §8.3）。
public enum GoFrameCodec {

    public struct DecodedFrame: Equatable, Sendable {
        public let frameType: UInt8
        public let sequence: UInt16
        public let timestamp: UInt64
        public let payload: Data
    }

    // MARK: - Encode

    public static func encode(frameType: UInt8, sequence: UInt16, timestamp: UInt64, payload: Data = Data()) throws -> Data {
        try encode(frameType: frameType, sequence: sequence, timestamp: timestamp, payload: payload, hmacKey: nil)
    }

    public static func encodeWithHMAC(frameType: UInt8, sequence: UInt16, timestamp: UInt64, payload: Data = Data(), hmacKey: Data) throws -> Data {
        try encode(frameType: frameType, sequence: sequence, timestamp: timestamp, payload: payload, hmacKey: hmacKey)
    }

    private static func encode(frameType: UInt8, sequence: UInt16, timestamp: UInt64, payload: Data, hmacKey: Data?) throws -> Data {
        guard payload.count <= Int(FGProtocolConstants.maxPayloadLen) else {
            throw GoBridgeError.payloadTooLarge(max: Int(FGProtocolConstants.maxPayloadLen))
        }
        var payloadBytes = [UInt8](payload)
        var keyBytes = hmacKey.map { [UInt8]($0) } ?? []
        var outLen = 0

        let out: UnsafeMutablePointer<UInt8>? = payloadBytes.withUnsafeMutableBufferPointer { pbuf in
            keyBytes.withUnsafeMutableBufferPointer { kbuf -> UnsafeMutablePointer<UInt8>? in
                if hmacKey != nil {
                    return fg_frame_encode_with_hmac(
                        frameType, sequence, timestamp,
                        pbuf.baseAddress, UInt8(pbuf.count),
                        kbuf.baseAddress, kbuf.count,
                        &outLen
                    )
                }
                return fg_frame_encode(frameType, sequence, timestamp, pbuf.baseAddress, UInt8(pbuf.count), &outLen)
            }
        }

        guard let out else { throw GoBridgeError.encodeFailed }
        defer { fg_free_buffer(out) }
        guard outLen > 0 else { throw GoBridgeError.encodeFailed }
        return Data(bytes: out, count: outLen)
    }

    // MARK: - Decode

    public static func decode(_ wire: Data) throws -> DecodedFrame {
        try decode(wire, hmacKey: nil)
    }

    public static func decodeWithHMAC(_ wire: Data, hmacKey: Data) throws -> DecodedFrame {
        try decode(wire, hmacKey: hmacKey)
    }

    private static func decode(_ wire: Data, hmacKey: Data?) throws -> DecodedFrame {
        var wireBytes = [UInt8](wire)
        var keyBytes = hmacKey.map { [UInt8]($0) } ?? []
        var outType: UInt8 = 0
        var outSeq: UInt16 = 0
        var outTs: UInt64 = 0
        var outPayloadLen: UInt8 = 0
        var outPayload: UnsafeMutablePointer<UInt8>?

        let rc: Int32 = wireBytes.withUnsafeMutableBufferPointer { wbuf in
            keyBytes.withUnsafeMutableBufferPointer { kbuf -> Int32 in
                if hmacKey != nil {
                    return fg_frame_decode_with_hmac(
                        wbuf.baseAddress, wbuf.count,
                        kbuf.baseAddress, kbuf.count,
                        &outType, &outSeq, &outTs, &outPayloadLen, &outPayload
                    )
                }
                return fg_frame_decode(wbuf.baseAddress, wbuf.count, &outType, &outSeq, &outTs, &outPayloadLen, &outPayload)
            }
        }

        // 失败时也兜底释放（Go 侧失败路径应为 NULL，此为防御）
        let payload = fgConsumeBuffer(outPayload, count: Int(outPayloadLen)) ?? Data()
        guard rc == 0 else { throw GoBridgeError.decodeFailed }
        return DecodedFrame(frameType: outType, sequence: outSeq, timestamp: outTs, payload: payload)
    }

    // MARK: - Validate

    /// 校验帧能否上线路：0 = 通过；否则抛 §12 错误码
    /// （0x11 帧类型不认识 → 整条丢；0x12 载荷长度与 PayloadLen 不符 → 发送侧编错了）。
    public static func validate(frameType: UInt8, sequence: UInt16, timestamp: UInt64, payload: Data = Data()) throws {
        guard payload.count <= Int(FGProtocolConstants.maxPayloadLen) else {
            throw GoBridgeError.payloadTooLarge(max: Int(FGProtocolConstants.maxPayloadLen))
        }
        var payloadBytes = [UInt8](payload)
        let rc: Int32 = payloadBytes.withUnsafeMutableBufferPointer { buf in
            fg_frame_validate(frameType, sequence, timestamp, UInt8(buf.count), buf.baseAddress)
        }
        guard rc == 0 else {
            throw GoProtocolError(code: UInt8(truncatingIfNeeded: rc), operation: "frame.validate")
        }
    }
}
