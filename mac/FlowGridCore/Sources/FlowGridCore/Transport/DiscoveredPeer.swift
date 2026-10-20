import Foundation

/// mDNS 浏览到的候选设备（§3.2 DirectLink 发现）。
/// `id` 为 Bonjour service name（局域网内唯一），连接时由传输层解析端点。
public struct DiscoveredPeer: Identifiable, Equatable, Sendable {
    public let id: String
    public let name: String
    public let platform: FGPlatform?
    public let port: UInt16?

    public init(id: String, name: String, platform: FGPlatform?, port: UInt16?) {
        self.id = id
        self.name = name
        self.platform = platform
        self.port = port
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
