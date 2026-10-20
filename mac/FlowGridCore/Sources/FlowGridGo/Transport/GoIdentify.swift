import CFlowGrid
import CryptoKit
import FlowGridCore
import Foundation

/// IDENTIFY / IDENTIFY_RESPONSE 载荷封装（协议 §4.2）。
/// 解码在 Swift 侧完成（cgo 未导出 unmarshal，集成文档允许客户端解析这 8 字节头）。
public enum GoIdentify {

    public static func encode(
        version: UInt8 = FGProtocolConstants.protocolVersion,
        platform: FGPlatform,
        role: UInt8,
        capabilities: FGCapabilities,
        seqInit: UInt32,
        name: String
    ) throws -> Data {
        var outLen = 0
        let out: UnsafeMutablePointer<UInt8>? = name.withCString { cName in
            fg_identify_payload_encode(version, platform.rawValue, role, capabilities.rawValue, seqInit, UnsafeMutablePointer(mutating: cName), &outLen)
        }
        guard let out else { throw GoBridgeError.encodeFailed }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    public static func encodeResponse(
        status: UInt8,
        platform: FGPlatform,
        capabilities: FGCapabilities,
        name: String
    ) throws -> Data {
        var outLen = 0
        let out: UnsafeMutablePointer<UInt8>? = name.withCString { cName in
            fg_identify_response_encode(status, platform.rawValue, capabilities.rawValue, UnsafeMutablePointer(mutating: cName), &outLen)
        }
        guard let out else { throw GoBridgeError.encodeFailed }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    // MARK: - 解析（wire：ver(1) platform(1) role(1) caps(1) seqInit(4 BE) name+NUL）

    public struct Identify: Equatable, Sendable {
        public let version: UInt8
        public let platform: FGPlatform
        public let role: UInt8
        public let capabilities: FGCapabilities
        public let seqInit: UInt16
        public let name: String
    }

    public static func parse(_ payload: Data) -> Identify? {
        guard payload.count >= 9, payload[payload.startIndex] == FGProtocolConstants.protocolVersion else { return nil }
        let bytes = [UInt8](payload)
        guard let platform = FGPlatform(rawValue: bytes[1]) else { return nil }
        // Seq Init 为 UInt32 BE（§4.2）；序列号空间为 UInt16，取低 16 位
        let seqInit = UInt16(bytes[6]) << 8 | UInt16(bytes[7])
        var nameBytes = Array(bytes[8...])
        if let nul = nameBytes.firstIndex(of: 0) {
            nameBytes = Array(nameBytes[..<nul])
        }
        return Identify(
            version: bytes[0],
            platform: platform,
            role: bytes[2],
            capabilities: FGCapabilities(rawValue: bytes[3]),
            seqInit: seqInit,
            name: String(decoding: nameBytes, as: UTF8.self)
        )
    }

    public struct Response: Equatable, Sendable {
        public let status: UInt8
        public let platform: FGPlatform
        public let capabilities: FGCapabilities
        public let name: String
    }

    public static func parseResponse(_ payload: Data) -> Response? {
        guard payload.count >= 3 else { return nil }
        let bytes = [UInt8](payload)
        guard let platform = FGPlatform(rawValue: bytes[1]) else { return nil }
        var nameBytes = Array(bytes[3...])
        if let nul = nameBytes.firstIndex(of: 0) {
            nameBytes = Array(nameBytes[..<nul])
        }
        return Response(
            status: bytes[0],
            platform: platform,
            capabilities: FGCapabilities(rawValue: bytes[2]),
            name: String(decoding: nameBytes, as: UTF8.self)
        )
    }
}

/// MVP 会话密钥：配对码派生 HMAC key。
/// 注：协议正路为 DTLS/配对阶段的 ECDH（§4.3）；明文 TCP DirectLink 上线前作为
/// 临时完整性密钥（§8.3 HMAC 仍启用），迁移到 DTLS 后由 ECDH 替换。
enum SessionKey {
    static func derive(code: String) -> Data {
        let normalized = code.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        return Data(SHA256.hash(data: Data("FlowGrid-mvp-session|\(normalized)".utf8)))
    }
}
