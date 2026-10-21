import CFlowGrid
import FlowGridCore
import Foundation

/// 智能连接（LAN 私有链）FFI 封装：身份密钥、通告器、监听器、私有链信任、链路选择。
/// 对应 protocol/lanid.go 与 protocol/link.go 的 cgo 导出。
public enum GoLanID {

    // MARK: - 身份密钥（ECDSA P-256，持久）

    /// 持久身份密钥对（签名用；与 §4.3 临时 ECDH 密钥分离）。
    public final class Identity: @unchecked Sendable {
        let handle: Int

        /// 生成新身份密钥对。
        public init() throws {
            let h: Int = fg_lanid_identity_generate()
            self.handle = try fgRequireHandle(h)
        }

        /// 从导出的私钥字节恢复。
        public init(importing data: Data) throws {
            var bytes = [UInt8](data)
            let h: Int = bytes.withUnsafeMutableBufferPointer { buf in
                fg_lanid_identity_import(buf.baseAddress, buf.count)
            }
            self.handle = try fgRequireHandle(h)
        }

        /// 导出私钥（SEC1 EC；客户端负责安全存储）。
        public func exportPrivateKey() throws -> Data {
            var outLen = 0
            let out = fg_lanid_identity_export(handle, &outLen)
            guard let out else { throw GoBridgeError.encodeFailed }
            return fgConsumeBuffer(out, count: outLen) ?? Data()
        }

        /// 身份公钥（65 字节 SEC1 非压缩点）。
        public func publicKey() throws -> Data {
            var outLen = 0
            let out = fg_lanid_identity_public_key(handle, &outLen)
            guard let out else { throw GoBridgeError.encodeFailed }
            return fgConsumeBuffer(out, count: outLen) ?? Data()
        }

        deinit {
            fg_lanid_identity_free(handle)
        }
    }

    // MARK: - 通告器（纯逻辑：Announce 产生字节，客户端经 UDP 多播发送）

    public final class Announcer: @unchecked Sendable {
        private let handle: Int

        public init(identity: Identity, platform: FGPlatform, capabilities: FGCapabilities, port: UInt16, name: String) throws {
            let h: Int = name.withCString { cName in
                fg_lanid_announcer_new(identity.handle, platform.rawValue, capabilities.rawValue, port, UnsafeMutablePointer(mutating: cName))
            }
            self.handle = try fgRequireHandle(h)
        }

        /// 产生一条签名通告字节。
        public func announce() throws -> Data {
            var outLen = 0
            let out = fg_lanid_announcer_announce(handle, &outLen)
            guard let out else { throw GoBridgeError.encodeFailed }
            return fgConsumeBuffer(out, count: outLen) ?? Data()
        }

        deinit {
            fg_lanid_announcer_free(handle)
        }
    }

    // MARK: - 监听器（纯逻辑：Feed 喂字节，Peers 取结果）

    /// 识别到的对端。
    public struct Peer: Equatable, Sendable {
        public let name: String
        public let platform: FGPlatform
        public let capabilities: FGCapabilities
        public let address: Data // 原始 IP 字节（4 = IPv4，16 = IPv6）
        public let port: UInt16
        public let publicKey: Data // 65 字节
        public let fingerprint: String // 32 hex

        /// IP 字符串（展示用）。
        public var addressString: String {
            guard let ip = Self.ipString(address) else { return "?" }
            return ip
        }

        static func ipString(_ bytes: Data) -> String? {
            var buf = [CChar](repeating: 0, count: Int(INET6_ADDRSTRLEN))
            let ok: Bool = bytes.withUnsafeBytes { raw in
                guard let base = raw.baseAddress else { return false }
                if bytes.count == 4 {
                    inet_ntop(AF_INET, base, &buf, socklen_t(INET6_ADDRSTRLEN))
                } else if bytes.count == 16 {
                    inet_ntop(AF_INET6, base, &buf, socklen_t(INET6_ADDRSTRLEN))
                } else {
                    return false
                }
                return true
            }
            guard ok else { return nil }
            return String(decoding: buf.prefix(while: { $0 != 0 }).map { UInt8(bitPattern: $0) }, as: UTF8.self)
        }
    }

    public final class Listener: @unchecked Sendable {
        private let handle: Int

        public init() throws {
            let h: Int = fg_lanid_listener_new()
            self.handle = try fgRequireHandle(h)
        }

        /// 喂入一条收到的通告字节与源 IP（原始字节）；验签失败静默丢弃。
        public func feed(_ data: Data, from address: Data) {
            var dataBytes = [UInt8](data)
            var addrBytes = [UInt8](address)
            dataBytes.withUnsafeMutableBufferPointer { dataBuf in
                addrBytes.withUnsafeMutableBufferPointer { addrBuf in
                    _ = fg_lanid_listener_feed(handle, dataBuf.baseAddress, dataBuf.count, addrBuf.baseAddress, addrBuf.count)
                }
            }
        }

        /// 当前已知对端数量。
        public var peersCount: Int {
            Int(fg_lanid_listener_peers_count(handle))
        }

        /// 取第 index 个对端（越界返回 nil）。
        public func peer(at index: Int) -> Peer? {
            var buf = [UInt8](repeating: 0, count: 512)
            let written: Int32 = buf.withUnsafeMutableBufferPointer { b in
                fg_lanid_listener_peer(handle, Int32(index), b.baseAddress, b.count)
            }
            guard written > 0, Int(written) <= buf.count else { return nil }
            return Self.parsePeer(Array(buf[0..<Int(written)]))
        }

        /// 全部对端快照。
        public func peers() -> [Peer] {
            (0..<peersCount).compactMap { peer(at: $0) }
        }

        /// 淘汰超过 maxAge 未更新的对端，返回淘汰数。
        @discardableResult
        public func prune(maxAgeMs: Int) -> Int {
            Int(fg_lanid_listener_prune(handle, Int64(maxAgeMs)))
        }

        deinit {
            fg_lanid_listener_free(handle)
        }

        /// 解析扁平对端结构：NameLen(1)+Name+Platform(1)+Caps(1)+Port(2 BE)+AddrLen(1)+Addr+PubKey(65)+Fingerprint(32)
        static func parsePeer(_ bytes: [UInt8]) -> Peer? {
            var off = 0
            guard off < bytes.count else { return nil }
            let nameLen = Int(bytes[off]); off += 1
            guard off + nameLen <= bytes.count else { return nil }
            let name = String(decoding: bytes[off..<off+nameLen], as: UTF8.self); off += nameLen
            guard off + 4 <= bytes.count else { return nil }
            let platform = bytes[off]; let caps = bytes[off+1]
            let port = UInt16(bytes[off+2]) << 8 | UInt16(bytes[off+3])
            off += 4
            guard off < bytes.count else { return nil }
            let addrLen = Int(bytes[off]); off += 1
            guard off + addrLen <= bytes.count else { return nil }
            let addr = Data(bytes[off..<off+addrLen]); off += addrLen
            guard off + 65 <= bytes.count else { return nil }
            let pubKey = Data(bytes[off..<off+65]); off += 65
            guard off + 32 <= bytes.count else { return nil }
            let fingerprint = String(decoding: bytes[off..<off+32], as: UTF8.self)
            guard let plat = FGPlatform(rawValue: platform) else { return nil }
            return Peer(
                name: name,
                platform: plat,
                capabilities: FGCapabilities(rawValue: caps),
                address: addr,
                port: port,
                publicKey: pubKey,
                fingerprint: fingerprint
            )
        }
    }

    // MARK: - 私有链信任（局域网私有区块链）

    public final class TrustChain: @unchecked Sendable {
        private let handle: Int

        public init(identity: Identity) throws {
            let h: Int = fg_lanid_trust_new(identity.handle)
            self.handle = try fgRequireHandle(h)
        }

        /// 追加信任记录（decision: 0x00 = trust, 0x01 = reject）。
        @discardableResult
        public func append(peerID: Data, decision: UInt8) throws -> Bool {
            var bytes = [UInt8](peerID)
            let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
                fg_lanid_trust_append(handle, buf.baseAddress, buf.count, decision)
            }
            guard rc == 0 else { throw GoBridgeError.operationFailed }
            return true
        }

        /// 对端是否被信任（链上最新决策为 trust）。
        public func isTrusted(peerID: Data) -> Bool {
            var bytes = [UInt8](peerID)
            let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
                fg_lanid_trust_is_trusted(handle, buf.baseAddress, buf.count)
            }
            return rc == 1
        }

        /// 校验整条链完整性（PrevHash + 签名）。
        public func verify() throws {
            let rc: Int32 = fg_lanid_trust_verify(handle)
            guard rc == 0 else { throw GoBridgeError.operationFailed }
        }

        /// 链上记录数。
        public var count: Int {
            Int(fg_lanid_trust_count(handle))
        }

        deinit {
            fg_lanid_trust_free(handle)
        }
    }

    // MARK: - 链路选择

    /// 连接计划（两模型排序）。
    public struct LinkPlan: Equatable, Sendable {
        public let order: [LinkModel]
        public let exhausted: Bool

        public enum LinkModel: String, Sendable {
            case intelligent = "intelligent"
            case direct = "direct"
        }
    }

    /// 根据策略与可用对端生成连接计划。
    /// - Parameters:
    ///   - preferred: 0 = IntelligentLink 优先, 1 = DirectLink 优先, 2 = 仅 IntelligentLink
    ///   - mdnsPeers: mDNS 对端数
    ///   - lanPeers: LAN 识别对端数
    ///   - hasManual: 是否有手动地址
    public static func plan(preferred: Int, mdnsPeers: Int, lanPeers: Int, hasManual: Bool) -> LinkPlan {
        let rc: Int32 = fg_link_plan(UInt8(preferred), Int32(mdnsPeers), Int32(lanPeers), hasManual ? 1 : 0)
        switch rc {
        case 0: return LinkPlan(order: [.intelligent, .direct], exhausted: false)
        case 1: return LinkPlan(order: [.direct, .intelligent], exhausted: false)
        case 2: return LinkPlan(order: [.intelligent], exhausted: false)
        case 3: return LinkPlan(order: [.direct], exhausted: false)
        default: return LinkPlan(order: [], exhausted: true)
        }
    }
}
