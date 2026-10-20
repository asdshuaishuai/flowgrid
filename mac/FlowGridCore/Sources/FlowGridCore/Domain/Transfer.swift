import Foundation

/// 传输方向。
public enum TransferDirection: Sendable {
    case sending
    case receiving

    /// 记录行方向文案：已发送 / 收到。
    public var recordLabel: String {
        switch self {
        case .sending: "已发送"
        case .receiving: "收到"
        }
    }
}

/// 传输终态/过程态，覆盖协议 §7.4 的全部 ACK 结果。
public enum TransferState: Sendable, Equatable {
    case active
    /// 数据收齐、SHA-256 校验中。
    case verifying
    /// 完成（发送：已发送；接收：SHA-256 已校验）。
    case completed
    /// 对端拒绝（ACK busy/user）。
    case rejected
    /// 校验失败（哈希不符，未交付）。
    case hashFailed
    /// 超出 16 MiB 上限。
    case oversize
    /// 10s 无进展判失败。
    case timedOut
    /// 用户中止。
    case aborted

    public var isTerminal: Bool {
        switch self {
        case .active, .verifying: false
        default: true
        }
    }
}

/// 一条传输（进行中或记录）。
public struct TransferItem: Identifiable, Equatable, Sendable {
    public var id: String
    public var name: String
    public var direction: TransferDirection
    public var peerName: String
    public var sizeBytes: Int64
    /// 0...1
    public var progress: Double
    public var speedText: String?
    public var etaSeconds: Double?
    public var state: TransferState
    public var note: String?
    /// 记录行的相对时间（秒）；进行中为 nil。
    public var ageSeconds: Int?

    public init(
        id: String, name: String, direction: TransferDirection, peerName: String,
        sizeBytes: Int64, progress: Double = 0, speedText: String? = nil,
        etaSeconds: Double? = nil, state: TransferState = .active, note: String? = nil,
        ageSeconds: Int? = nil
    ) {
        self.id = id
        self.name = name
        self.direction = direction
        self.peerName = peerName
        self.sizeBytes = sizeBytes
        self.progress = progress
        self.speedText = speedText
        self.etaSeconds = etaSeconds
        self.state = state
        self.note = note
        self.ageSeconds = ageSeconds
    }

    public var sizeText: String {
        let bytes = Double(sizeBytes)
        if bytes < 1024 { return "\(sizeBytes) B" }
        if bytes < 1024 * 1024 { return String(format: "%.0f KB", bytes / 1024) }
        if bytes < 1024 * 1024 * 1024 { return String(format: "%.1f MB", bytes / (1024 * 1024)) }
        return String(format: "%.1f GB", bytes / (1024 * 1024 * 1024))
    }

    public var percentText: String { "\(Int(progress * 100))%" }

    public var etaText: String? {
        guard let etaSeconds, etaSeconds.isFinite, etaSeconds >= 0 else { return nil }
        return "剩余 \(Int(etaSeconds.rounded()))s"
    }
}
