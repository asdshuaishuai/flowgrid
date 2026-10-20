import Foundation

/// 设备操作系统族（决定图标与文案）。
public enum DeviceOS: String, CaseIterable, Sendable {
    case macOS
    case windows
    case linux
    case deepin

    /// 图标注册表里的名字（FlowGridUI/Icons）。
    public var iconName: String {
        switch self {
        case .macOS: "apple"
        case .windows: "windows"
        case .linux: "linux"
        case .deepin: "deepin"
        }
    }
}

/// 链路类型（UI 命名：IntelligentLink / DirectLink，与设计稿一致）。
public enum LinkKind: String, Sendable {
    case intelligentLink = "IntelligentLink"
    case directLink = "DirectLink"
}

/// 一台可见设备（本机或对端）。
public struct Device: Identifiable, Equatable, Sendable {
    public var id: String
    public var name: String
    public var os: DeviceOS
    /// OS 展示文案，如 "Windows 11" / "KDE Plasma 6"。
    public var osVersion: String
    /// 快速切换缩写（PC / MB / LX / DT）。
    public var initials: String
    /// 当前链路；本机为 nil。
    public var link: LinkKind?
    /// 快捷键数字（"1" / "2" / "3"）。
    public var shortcut: String?
    public var isLocal: Bool
    public var isOnline: Bool
    /// 离线设备的最近连接描述（"上次连接 12 分钟前"）。
    public var lastSeen: String?
    /// 丢包率（演示值，协议无此字段）。
    public var packetLossPercent: Double?

    public init(
        id: String, name: String, os: DeviceOS, osVersion: String, initials: String,
        link: LinkKind? = nil, shortcut: String? = nil, isLocal: Bool = false,
        isOnline: Bool = true, lastSeen: String? = nil, packetLossPercent: Double? = nil
    ) {
        self.id = id
        self.name = name
        self.os = os
        self.osVersion = osVersion
        self.initials = initials
        self.link = link
        self.shortcut = shortcut
        self.isLocal = isLocal
        self.isOnline = isOnline
        self.lastSeen = lastSeen
        self.packetLossPercent = packetLossPercent
    }
}
