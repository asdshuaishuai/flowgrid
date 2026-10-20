import Foundation

/// 配对候选设备（添加设备向导「网络中的设备」）。
public struct PairingCandidate: Identifiable, Equatable, Sendable {
    public var id: String
    public var name: String
    public var address: String
    /// RSSI dBm（信号分级经协议 ClassifyRSSI：>-50 极佳 / >-65 好 / >-80 一般 / 其余弱）。
    public var rssi: Int
    /// 发现距今秒数。
    public var discoveredSeconds: Int
    /// 是否运行 FlowGrid（未运行显示离线徽标）。
    public var isAvailable: Bool

    public init(id: String, name: String, address: String, rssi: Int, discoveredSeconds: Int, isAvailable: Bool) {
        self.id = id
        self.name = name
        self.address = address
        self.rssi = rssi
        self.discoveredSeconds = discoveredSeconds
        self.isAvailable = isAvailable
    }
}

public extension FGSignalQuality {
    /// 设计稿信号文案：信号强 / 信号中 / 信号弱（4 档并档展示）。
    var label: String {
        switch self {
        case .excellent, .good: "信号强"
        case .fair: "信号中"
        case .weak: "信号弱"
        }
    }
}
