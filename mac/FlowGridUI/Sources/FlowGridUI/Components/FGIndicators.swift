import FlowGridCore
import SwiftUI

// MARK: - 徽章（18 高，11/600，pill）

public struct FGBadge: View {
    public enum Kind: Sendable {
        case accent
        case neutral
        case ok
        case warn
        case danger
        case idle
    }

    private let text: String
    private let kind: Kind

    public init(_ text: String, kind: Kind = .accent) {
        self.text = text
        self.kind = kind
    }

    public var body: some View {
        Text(text)
            .font(.system(size: 11, weight: .semibold))
            .tracking(11 * 0.012)
            .foregroundStyle(foreground)
            .padding(.horizontal, 7)
            .frame(height: 18)
            .background(background)
            .clipShape(Capsule())
            .fixedSize()
    }

    private var foreground: Color {
        switch kind {
        case .accent: FGColor.accentText
        case .neutral: FGColor.text2
        case .ok: FGColor.okText
        case .warn: FGColor.warnText
        case .danger: FGColor.dangerText
        case .idle: FGColor.idleText
        }
    }

    private var background: Color {
        switch kind {
        case .accent: FGColor.accentSoft
        case .neutral: FGColor.surface3
        case .ok: FGColor.okSoft
        case .warn: FGColor.warnSoft
        case .danger: FGColor.dangerSoft
        case .idle: FGColor.idleSoft
        }
    }
}

// MARK: - 状态点（7px；pulse 2s）

public struct FGStatusDot: View {
    public enum Kind: Sendable {
        case ok
        case warn
        case danger
        case idle
    }

    private let kind: Kind
    private let pulsing: Bool
    @State private var dimmed = false

    public init(kind: Kind = .ok, pulsing: Bool = false) {
        self.kind = kind
        self.pulsing = pulsing
    }

    public var body: some View {
        Circle()
            .fill(color)
            .frame(width: 7, height: 7)
            .opacity(pulsing && dimmed ? 0.4 : 1)
            .onAppear {
                guard pulsing, !FGMotion.reduceMotion else { return }
                withAnimation(.easeInOut(duration: FGMotion.pulseDuration / 2).repeatForever(autoreverses: true)) {
                    dimmed = true
                }
            }
    }

    private var color: Color {
        switch kind {
        case .ok: FGColor.okSolid
        case .warn: FGColor.warnSolid
        case .danger: FGColor.dangerSolid
        case .idle: FGColor.idleSolid
        }
    }
}

// MARK: - 状态胶囊（20 高，ok-soft 底，tabular）

public struct FGStatusPill: View {
    private let text: String
    private let dot: FGStatusDot.Kind?
    private let pulsing: Bool

    public init(_ text: String, dot: FGStatusDot.Kind? = .ok, pulsing: Bool = false) {
        self.text = text
        self.dot = dot
        self.pulsing = pulsing
    }

    public var body: some View {
        HStack(spacing: 6) {
            if let dot {
                FGStatusDot(kind: dot, pulsing: pulsing)
            }
            Text(text)
                .font(.system(size: 11, weight: .semibold))
                .monospacedDigit()
                .foregroundStyle(FGColor.okText)
        }
        .padding(.horizontal, FGSpacing.s2)
        .frame(height: 20)
        .background(FGColor.okSoft)
        .clipShape(Capsule())
        .fixedSize()
    }
}

// MARK: - 延迟胶囊（min-width 56，12/700，tabular；good/warn/bad/idle）

public struct FGLatPill: View {
    private let ms: Double?
    private let tier: LatencyTier

    public init(ms: Double?, tier: LatencyTier) {
        self.ms = ms
        self.tier = tier
    }

    public var body: some View {
        let colors = FGColor.tier(tier)
        Text(ms.map { $0.latencyText } ?? "—")
            .font(.system(size: 12, weight: .bold))
            .monospacedDigit()
            .foregroundStyle(colors.text)
            .triggerNumericTransition(ms ?? 0)
            .frame(minWidth: 56)
            .padding(.vertical, 4)
            .padding(.horizontal, FGSpacing.s2)
            .background(colors.soft)
            .clipShape(Capsule())
            .animation(FGMotion.toast, value: tier)
    }
}

private extension View {
    @ViewBuilder
    func triggerNumericTransition(_ value: Double) -> some View {
        if FGMotion.reduceMotion {
            self
        } else {
            contentTransition(.numericText(value: value))
                .animation(FGMotion.toast, value: value)
        }
    }
}

// MARK: - 分级计算（协议 §9.2 阈值经 FlowGridCore.LatencyTier）

public extension LatencyTier {
    /// 从实况毫秒值直接分级（离线传 nil）。
    static func from(ms: Double?) -> LatencyTier {
        guard let ms else { return .idle }
        return classify(ms: ms)
    }
}
