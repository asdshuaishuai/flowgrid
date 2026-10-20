import Foundation

/// 剪贴板条目类型（对应设计稿过滤分段：全部/文本/代码/链接/图片）。
public enum ClipboardKind: String, CaseIterable, Sendable {
    case text
    case code
    case link
    case image

    public var displayName: String {
        switch self {
        case .text: "文本"
        case .code: "代码"
        case .link: "链接"
        case .image: "图片"
        }
    }

    public var iconName: String {
        switch self {
        case .text: "type"
        case .code: "code"
        case .link: "link"
        case .image: "image"
        }
    }
}

/// 一条剪贴板历史/当前内容。
public struct ClipboardItem: Identifiable, Equatable, Sendable {
    public var id: String
    public var kind: ClipboardKind
    public var preview: String
    public var mime: String
    public var sizeBytes: Int
    /// 距今秒数（12s / 1m / 4m / 9m 由 UI 格式化）。
    public var ageSeconds: Int
    /// 来源设备名（预警 "来自 MacBook Pro"）。
    public var fromDevice: String?
    /// 超出单条上限（63,240 B）未同步。
    public var oversize: Bool
    /// 图片条目的原始数据（≤63,240 B，仅内存持有；用于「写入本地」）。
    public var imageData: Data?

    public init(
        id: String, kind: ClipboardKind, preview: String, mime: String,
        sizeBytes: Int, ageSeconds: Int, fromDevice: String? = nil, oversize: Bool = false,
        imageData: Data? = nil
    ) {
        self.id = id
        self.kind = kind
        self.preview = preview
        self.mime = mime
        self.sizeBytes = sizeBytes
        self.ageSeconds = ageSeconds
        self.fromDevice = fromDevice
        self.oversize = oversize
        self.imageData = imageData
    }

    /// 发送分段数（协议 §5.3 公式：每段 ≤ 255-(7+MIME) 字节；≤1 表示单帧直发）。
    public var segmentCount: Int {
        guard !oversize else { return 0 }
        let budget = Int(FGProtocolConstants.maxPayloadLen) - 7 - mime.utf8.count
        guard budget > 0 else { return 0 }
        return max(1, (sizeBytes + budget - 1) / budget)
    }

    public var isSingleFrame: Bool { segmentCount <= 1 }

    /// 展示用大小（B / KB / MB，一位小数）。
    public var sizeText: String {
        if sizeBytes < 1024 { return "\(sizeBytes) B" }
        if sizeBytes < 1024 * 1024 { return String(format: "%.1f KB", Double(sizeBytes) / 1024) }
        return String(format: "%.1f MB", Double(sizeBytes) / (1024 * 1024))
    }
}

public extension Int {
    /// 相对时间文案：12s / 1m / 4m / 9m / 1h。
    var ageText: String {
        if self < 60 { return "\(self)s" }
        if self < 3600 { return "\(self / 60)m" }
        return "\(self / 3600)h"
    }
}
