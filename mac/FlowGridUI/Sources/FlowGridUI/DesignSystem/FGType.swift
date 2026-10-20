import SwiftUI

/// 排版阶梯（tokens.css 第 4 节，四平台一致）。
/// 行高用 lineSpacing 近似（SwiftUI 无直接 line-height）：仅多行文本生效。
public struct FGTextStyle: Sendable {
    public let size: CGFloat
    public let weight: Font.Weight
    public let lineHeight: CGFloat
    public let trackingEm: CGFloat

    public var tracking: CGFloat { size * trackingEm }

    public func font(mono: Bool = false) -> Font {
        mono ? .system(size: size, weight: weight, design: .monospaced) : .system(size: size, weight: weight)
    }

    /// SwiftUI 默认行高约 1.2×字号，差额用 lineSpacing 补齐。
    public var lineSpacing: CGFloat { max(0, lineHeight - size * 1.2) }
}

public enum FGType {
    /// 11/16 +0.012em（辅助文字、徽章、快捷键）
    public static func t11(_ weight: Font.Weight = .medium) -> FGTextStyle {
        FGTextStyle(size: 11, weight: weight, lineHeight: 16, trackingEm: 0.012)
    }
    /// 12/18（正文、按钮、列表行）
    public static func t12(_ weight: Font.Weight = .regular) -> FGTextStyle {
        FGTextStyle(size: 12, weight: weight, lineHeight: 18, trackingEm: 0)
    }
    /// 13/20 −0.012em（设备名、标题）
    public static func t13(_ weight: Font.Weight = .semibold) -> FGTextStyle {
        FGTextStyle(size: 13, weight: weight, lineHeight: 20, trackingEm: -0.012)
    }
    /// 15/22 −0.012em（切换提示）
    public static func t15(_ weight: Font.Weight = .semibold) -> FGTextStyle {
        FGTextStyle(size: 15, weight: weight, lineHeight: 22, trackingEm: -0.012)
    }
    /// 20/26 −0.022em（配对码）
    public static func t20(_ weight: Font.Weight = .bold) -> FGTextStyle {
        FGTextStyle(size: 20, weight: weight, lineHeight: 26, trackingEm: -0.022)
    }
    /// 28/34 −0.022em（延迟大读数）
    public static func t28(_ weight: Font.Weight = .bold) -> FGTextStyle {
        FGTextStyle(size: 28, weight: weight, lineHeight: 34, trackingEm: -0.022)
    }

    /// 全大写小标题字距。
    public static let capsTrackingEm: CGFloat = 0.04
}

public extension View {
    /// 套用排版阶梯（含字距与行高近似）。
    func fgText(_ style: FGTextStyle, mono: Bool = false) -> some View {
        font(style.font(mono: mono))
            .tracking(style.tracking)
            .lineSpacing(style.lineSpacing)
    }

    /// 小标题：11/600 + 0.04em + 大写（中文场景仅取字距/字重）。
    func fgCapsLabel() -> some View {
        font(.system(size: 11, weight: .semibold))
            .tracking(11 * FGType.capsTrackingEm)
    }
}
