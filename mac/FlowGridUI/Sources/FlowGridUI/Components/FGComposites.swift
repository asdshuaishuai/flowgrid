import FlowGridCore
import SwiftUI
import UniformTypeIdentifiers

// MARK: - 设备行（grid 42/1fr/auto，pad 12，radius 10；hover 抬升；active/off 变体）

public struct FGDeviceRow<Trailing: View>: View {
    private let device: Device
    private let isCurrent: Bool
    private let meta: String
    private let trailing: Trailing
    @State private var hovering = false

    public init(device: Device, isCurrent: Bool = false, meta: String, @ViewBuilder trailing: () -> Trailing) {
        self.device = device
        self.isCurrent = isCurrent
        self.meta = meta
        self.trailing = trailing()
    }

    public var body: some View {
        HStack(spacing: FGSpacing.s3) {
            Text(device.initials)
                .font(.system(size: 12, weight: .bold))
                .foregroundStyle(FGColor.accentText)
                .frame(width: 42, height: 42)
                .background(FGColor.accentSoft)
                .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))

            VStack(alignment: .leading, spacing: 2) {
                Text(device.name)
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(FGColor.text1)
                Text(meta)
                    .font(.system(size: 11))
                    .foregroundStyle(FGColor.text3)
                    .lineLimit(1)
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            trailing
        }
        .padding(FGSpacing.s3)
        .background(isCurrent ? FGColor.accentSoft : FGColor.surface)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous)
                .strokeBorder(isCurrent ? FGColor.accentLine : (hovering ? FGColor.line3 : FGColor.line2), lineWidth: 1)
        )
        .shadow(color: hovering ? FGShadow.lift.color : .clear, radius: hovering ? 5 : 0, x: 0, y: hovering ? 2 : 0)
        .opacity(device.isOnline ? 1 : 0.6)
        .animation(FGMotion.state, value: hovering)
        .onHover { hovering = $0 }
    }
}

public extension Device {
    /// 色块（快速切换 / 布局网格）：PC accent、MB ok、LX warn、离线 idle。
    var tileColor: Color {
        guard isOnline else { return FGColor.idleSolid }
        switch initials {
        case "MB": return FGColor.okSolid
        case "LX": return FGColor.warnSolid
        default: return FGColor.accent
        }
    }
}

// MARK: - 键位对（mapping 单元格：两键帽 + 说明）

public struct FGKeyPairCell: View {
    private let row: KeyMappingRow
    @State private var hovering = false

    public init(_ row: KeyMappingRow) {
        self.row = row
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            HStack(spacing: 5) {
                FGKeyCap(row.fromKey)
                FGIcon("arrowRight", size: 14, color: FGColor.text3)
                if let toKey = row.toKey {
                    FGKeyCap(toKey)
                } else {
                    FGKeyCap("未设置", ghost: true)
                }
            }
            Text(row.caption)
                .font(.system(size: 11))
                .foregroundStyle(FGColor.text3)
                .lineLimit(1)
        }
        .padding(FGSpacing.s2)
        .background(hovering ? FGColor.surfaceHi : Color.clear)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
        .onHover { hovering = $0 }
    }
}

/// 3 列键位对网格（mapping：gap 8，pad 12）。
public struct FGMappingGrid: View {
    private let rows: [KeyMappingRow]

    public init(_ rows: [KeyMappingRow]) {
        self.rows = rows
    }

    public var body: some View {
        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), alignment: .leading), count: 3), spacing: FGSpacing.s2) {
            ForEach(rows) { row in
                FGKeyPairCell(row)
            }
        }
        .padding(FGSpacing.s3)
    }
}

// MARK: - 柱状图（height 70；柱圆角上；最新一根实色；高度过渡 500ms）

public struct FGLatencyChart: View {
    private let samples: [Double]
    /// 满格对应的毫秒数（相对刻度）。
    private let fullScaleMs: Double
    private let axisStart: String

    public init(samples: [Double], fullScaleMs: Double = 5.0, axisStart: String = "-24s") {
        self.samples = samples
        self.fullScaleMs = fullScaleMs
        self.axisStart = axisStart
    }

    public var body: some View {
        VStack(spacing: 0) {
            HStack(alignment: .bottom, spacing: 4) {
                ForEach(Array(samples.enumerated()), id: \.offset) { index, value in
                    let fraction = min(max(value / fullScaleMs, 0), 1)
                    let isRecent = index == samples.lastIndex(where: { $0 > 0 })
                    UnevenRoundedRectangle(topLeadingRadius: FGRadius.pill, topTrailingRadius: FGRadius.pill)
                        .fill(isRecent ? FGColor.accent : FGColor.accentSoft)
                        .frame(maxWidth: .infinity, minHeight: 0)
                        .frame(height: fraction * plotHeight)
                        .animation(FGMotion.chart, value: samples)
                }
            }
            .frame(height: plotHeight, alignment: .bottom)
            .padding(.top, FGSpacing.s3)
            .padding(.horizontal, FGSpacing.s3)
            .padding(.bottom, FGSpacing.s2)
            .frame(height: 70, alignment: .bottom)

            HStack {
                Text(axisStart)
                Spacer()
                Text("现在")
            }
            .font(.system(size: 11))
            .foregroundStyle(FGColor.text3)
            .padding(.horizontal, FGSpacing.s3)
            .padding(.bottom, FGSpacing.s3)
        }
    }

    private var plotHeight: CGFloat { 70 - FGSpacing.s3 - FGSpacing.s2 }
}

// MARK: - 剪贴板行（pad 7/8；类型块 18×18；时间；可选徽章与复制钮）

public struct FGClipRow: View {
    private let item: ClipboardItem
    private let onCopy: () -> Void
    @State private var hovering = false

    public init(item: ClipboardItem, onCopy: @escaping () -> Void) {
        self.item = item
        self.onCopy = onCopy
    }

    public var body: some View {
        HStack(spacing: FGSpacing.s2) {
            FGIcon(item.kind.iconName, size: 12, color: FGColor.text2)
                .frame(width: 18, height: 18)
                .background(FGColor.surface3)
                .clipShape(RoundedRectangle(cornerRadius: 4, style: .continuous))

            Text(item.preview)
                .font(.system(size: 11))
                .foregroundStyle(FGColor.text2)
                .lineLimit(1)
                .truncationMode(.middle)
                .frame(maxWidth: .infinity, alignment: .leading)

            if item.oversize {
                FGBadge("超限", kind: .warn)
            }

            Text(item.ageSeconds.ageText)
                .font(.system(size: 11))
                .monospacedDigit()
                .foregroundStyle(FGColor.text3)

            FGIconButton("copy", size: 14, action: onCopy)
        }
        .padding(.vertical, 7)
        .padding(.horizontal, FGSpacing.s2)
        .background(hovering ? FGColor.surfaceHi : Color.clear)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
        .onHover { hovering = $0 }
    }
}

// MARK: - 传输行（icon 28；名称 + pct；meta；进行中含进度条）

public struct FGTransferRow: View {
    private let item: TransferItem
    private let sideAction: (() -> Void)?

    public init(item: TransferItem, sideAction: (() -> Void)? = nil) {
        self.item = item
        self.sideAction = sideAction
    }

    public var body: some View {
        HStack(spacing: FGSpacing.s3) {
            FGIcon(iconName, size: 15, color: iconTint)
                .frame(width: 28, height: 28)
                .background(iconBackground)
                .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))

            VStack(alignment: .leading, spacing: 4) {
                HStack(spacing: FGSpacing.s2) {
                    Text(item.name)
                        .font(.system(size: 12, weight: .medium))
                        .foregroundStyle(FGColor.text1)
                        .lineLimit(1)
                        .truncationMode(.middle)
                    Spacer(minLength: 0)
                    if item.state == .active {
                        Text(item.percentText)
                            .font(.system(size: 11, weight: .semibold, design: .monospaced))
                            .foregroundStyle(FGColor.text2)
                    } else {
                        FGBadge(stateLabel, kind: stateBadgeKind)
                        if let age = item.ageSeconds {
                            Text(age.ageText)
                                .font(.system(size: 11))
                                .monospacedDigit()
                                .foregroundStyle(FGColor.text3)
                        }
                    }
                }
                HStack(spacing: FGSpacing.s2) {
                    Text(metaText)
                        .font(.system(size: 11))
                        .foregroundStyle(FGColor.text3)
                        .lineLimit(1)
                }
                if item.state == .active || item.state == .verifying {
                    FGProgressBar(progress: item.progress, ok: item.state == .verifying)
                }
            }

            if let sideAction, item.state == .active || item.state == .timedOut {
                FGIconButton(item.state == .active ? "close" : "refresh", size: 14, action: sideAction)
            }
        }
        .padding(.vertical, 9)
        .padding(.horizontal, FGSpacing.s3)
        .overlay(alignment: .bottom) {
            Rectangle().fill(FGColor.hairline).frame(height: 1)
        }
    }

    private var metaText: String {
        // 超限记录从未上线：只报大小与原因
        if item.state == .oversize {
            var parts = [item.sizeText]
            if let note = item.note { parts.append(note) }
            return parts.joined(separator: " · ")
        }
        var parts: [String]
        if item.state == .active || item.state == .verifying {
            parts = [item.direction == .sending ? "→" : "←", item.peerName, item.sizeText]
            if let speed = item.speedText { parts.append(speed) }
            if let eta = item.etaText, item.state == .active { parts.append(eta) }
        } else {
            parts = [item.direction.recordLabel, item.peerName, item.sizeText]
            if let note = item.note { parts.append(note) }
        }
        return parts.joined(separator: " · ")
    }

    private var iconName: String {
        switch item.state {
        case .active: item.direction == .sending ? "upload" : "download"
        case .verifying: "spinner"
        case .completed: "check"
        case .rejected: "close"
        case .hashFailed, .oversize, .timedOut: "warn"
        case .aborted: "close"
        }
    }

    private var iconTint: Color {
        switch item.state {
        case .completed: FGColor.okText
        case .verifying: FGColor.accentText
        case .rejected, .timedOut: FGColor.warnText
        case .hashFailed, .oversize, .aborted: FGColor.dangerText
        case .active: FGColor.text2
        }
    }

    private var iconBackground: Color {
        switch item.state {
        case .completed: FGColor.okSoft
        case .verifying: FGColor.accentSoft
        case .rejected, .timedOut: FGColor.warnSoft
        case .hashFailed, .oversize, .aborted: FGColor.dangerSoft
        case .active: FGColor.surface3
        }
    }

    private var stateLabel: String {
        switch item.state {
        case .active: "进行中"
        case .verifying: "校验中"
        case .completed: "完成"
        case .rejected: "已拒绝"
        case .hashFailed: "校验失败"
        case .oversize: "超限"
        case .timedOut: "超时"
        case .aborted: "已取消"
        }
    }

    private var stateBadgeKind: FGBadge.Kind {
        switch item.state {
        case .completed: .ok
        case .verifying, .active: .accent
        case .rejected, .timedOut: .warn
        case .hashFailed, .oversize, .aborted: .danger
        }
    }
}

// MARK: - 拖放区（2px 虚线；hover / 拖入高亮 accent；支持文件拖放）

public struct FGDropZone: View {
    private let title: String
    private let hint: String
    private let onDropFiles: (([URL]) -> Void)?
    private let action: () -> Void
    @State private var hovering = false
    @State private var dropTargeted = false

    public init(
        title: String = "拖入文件，或点击选择",
        hint: String,
        onDropFiles: (([URL]) -> Void)? = nil,
        action: @escaping () -> Void
    ) {
        self.title = title
        self.hint = hint
        self.onDropFiles = onDropFiles
        self.action = action
    }

    private var active: Bool { hovering || dropTargeted }

    public var body: some View {
        Button(action: action) {
            VStack(spacing: FGSpacing.s2) {
                FGIcon("upload", size: 20, color: active ? FGColor.accentText : FGColor.text3)
                Text(dropTargeted ? "松开以发送" : title)
                    .font(.system(size: 12, weight: .medium))
                Text(hint)
                    .font(.system(size: 11))
                    .foregroundStyle(FGColor.text3)
            }
            .foregroundStyle(active ? FGColor.accentText : FGColor.text2)
            .frame(maxWidth: .infinity)
            .padding(.vertical, FGSpacing.s5)
            .padding(.horizontal, FGSpacing.s3)
            .background(active ? FGColor.accentSoft : FGColor.surface2)
            .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous)
                    .strokeBorder(active ? FGColor.accent : FGColor.line2, style: StrokeStyle(lineWidth: 2, dash: [5, 4]))
            )
        }
        .buttonStyle(.plain)
        .animation(FGMotion.state, value: active)
        .onHover { hovering = $0 }
        .dropDestination(for: URL.self) { urls, _ in
            guard let onDropFiles else { return false }
            onDropFiles(urls)
            return true
        } isTargeted: { targeted in
            dropTargeted = targeted
        }
    }
}

// MARK: - 向导步骤（20 圆；done check / on accent / 默认线框）

public struct FGSteps: View {
    public enum StepState: Sendable {
        case waiting
        case current
        case done
    }

    private let items: [(label: String, state: StepState)]

    public init(_ items: [(label: String, state: StepState)]) {
        self.items = items
    }

    public var body: some View {
        HStack(spacing: FGSpacing.s2) {
            ForEach(Array(items.enumerated()), id: \.offset) { index, item in
                HStack(spacing: 6) {
                    circle(for: item.state, number: index + 1)
                    Text(item.label)
                        .font(.system(size: 11, weight: item.state == .current ? .semibold : .medium))
                        .foregroundStyle(item.state == .waiting ? FGColor.text3 : FGColor.text1)
                }
                if index != items.count - 1 {
                    Rectangle()
                        .fill(FGColor.line2)
                        .frame(width: 20, height: 1)
                }
            }
            Spacer(minLength: 0)
        }
    }

    @ViewBuilder private func circle(for state: StepState, number: Int) -> some View {
        switch state {
        case .done:
            FGIcon("check", size: 12, color: FGColor.okText)
                .frame(width: 20, height: 20)
                .background(FGColor.okSoft)
                .clipShape(Circle())
                .overlay(Circle().strokeBorder(FGColor.okLine, lineWidth: 1))
        case .current:
            Text("\(number)")
                .font(.system(size: 11, weight: .semibold))
                .foregroundStyle(FGColor.onAccent)
                .frame(width: 20, height: 20)
                .background(FGColor.accent)
                .clipShape(Circle())
        case .waiting:
            Text("\(number)")
                .font(.system(size: 11, weight: .semibold))
                .foregroundStyle(FGColor.text3)
                .frame(width: 20, height: 20)
                .background(FGColor.surface)
                .clipShape(Circle())
                .overlay(Circle().strokeBorder(FGColor.line2, lineWidth: 1))
        }
    }
}

// MARK: - 配对码（4 格 34×42，mono 20/700，虚线容器）

public struct FGPairCode: View {
    private let digits: [String]

    public init(digits: [String]) {
        self.digits = digits
    }

    public var body: some View {
        HStack(spacing: FGSpacing.s2) {
            ForEach(Array(digits.enumerated()), id: \.offset) { _, digit in
                Text(digit)
                    .font(.system(size: 20, weight: .bold, design: .monospaced))
                    .foregroundStyle(FGColor.text1)
                    .frame(width: 34, height: 42)
                    .background(FGColor.surface)
                    .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
                    .shadow(color: FGColor.line1, radius: 0, x: 0, y: 1)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(FGSpacing.s3)
        .background(FGColor.surface2)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous)
                .strokeBorder(FGColor.line2, style: StrokeStyle(lineWidth: 1, dash: [5, 4]))
        )
    }
}
