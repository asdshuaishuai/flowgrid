import Foundation

/// 发现来源：mDNS（直连）或 LAN 多播（智能连接）。
public enum DiscoverySource: String, Sendable {
    case mdns = "mDNS"
    case lan = "智能连接"
}

/// 候选设备（§3.2 mDNS 直连发现 / §3.1 智能连接 LAN 私有链发现）。
/// `id` 为稳定标识：mDNS 用 Bonjour service name，智能连接用身份公钥 hex。
public struct DiscoveredPeer: Identifiable, Equatable, Sendable {
    public let id: String
    public let name: String
    public let platform: FGPlatform?
    public let port: UInt16?
    public let source: DiscoverySource
    /// 智能连接对端的指纹（32 hex）；mDNS 对端为 nil。
    public let fingerprint: String?
    /// 智能连接对端是否已信任（私有链）；mDNS 对端为 nil。
    public let trusted: Bool?

    public init(
        id: String,
        name: String,
        platform: FGPlatform?,
        port: UInt16?,
        source: DiscoverySource = .mdns,
        fingerprint: String? = nil,
        trusted: Bool? = nil
    ) {
        self.id = id
        self.name = name
        self.platform = platform
        self.port = port
        self.source = source
        self.fingerprint = fingerprint
        self.trusted = trusted
    }
}

public extension FGPlatform {
    /// mDNS TXT `platform` 字段（§3.2.1：macos / windows / linux）。
    init?(mdnsName: String) {
        switch mdnsName.lowercased() {
        case "macos", "mac": self = .macOS
        case "windows", "win": self = .windows
        case "linux": self = .linux
        case "android": self = .android
        case "ios": self = .iOS
        default: return nil
        }
    }

    var mdnsName: String {
        switch self {
        case .macOS: "macos"
        case .windows: "windows"
        case .linux: "linux"
        case .android: "android"
        case .iOS: "ios"
        }
    }
}
