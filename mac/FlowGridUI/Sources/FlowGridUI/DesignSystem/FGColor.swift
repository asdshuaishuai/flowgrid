import AppKit
import FlowGridCore
import SwiftUI

/// 三层令牌的 Swift 承载：与 design/unified/tokens.css 逐值对应。
/// L1 灰阶仅作推导原料；组件层只消费 L2 语义值。
public enum FGColor {

    // MARK: - 动态色构造

    private static func dynamic(_ light: NSColor, _ dark: NSColor) -> Color {
        Color(nsColor: NSColor(name: nil) { appearance in
            appearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua ? dark : light
        })
    }

    private static func rgb(_ r: Int, _ g: Int, _ b: Int, alpha: CGFloat = 1) -> NSColor {
        NSColor(srgbRed: CGFloat(r) / 255, green: CGFloat(g) / 255, blue: CGFloat(b) / 255, alpha: alpha)
    }

    private static func rgb(_ t: (Int, Int, Int), alpha: CGFloat = 1) -> NSColor {
        rgb(t.0, t.1, t.2, alpha: alpha)
    }

    /// color-mix(in srgb, base p%, other (100-p)%)。
    private static func mix(_ base: (Int, Int, Int), with other: (Int, Int, Int), ratio: Double) -> NSColor {
        let r = Int((Double(base.0) * ratio + Double(other.0) * (1 - ratio)).rounded())
        let g = Int((Double(base.1) * ratio + Double(other.1) * (1 - ratio)).rounded())
        let b = Int((Double(base.2) * ratio + Double(other.2) * (1 - ratio)).rounded())
        return rgb(r, g, b)
    }

    /// 语义色族：soft/line 用原三元组 + 分主题 alpha（tokens.css 原样）。
    private struct Family {
        let triplet: (Int, Int, Int)
        let lightSolid: (Int, Int, Int)
        let darkSolid: (Int, Int, Int)
        let lightText: (Int, Int, Int)
        let darkText: (Int, Int, Int)
        let lightSoft: CGFloat
        let darkSoft: CGFloat
        let lightLine: CGFloat
        let darkLine: CGFloat

        var solid: Color { FGColor.dynamic(FGColor.rgb(lightSolid), FGColor.rgb(darkSolid)) }
        var text: Color { FGColor.dynamic(FGColor.rgb(lightText), FGColor.rgb(darkText)) }
        var soft: Color { FGColor.dynamic(FGColor.rgb(triplet, alpha: lightSoft), FGColor.rgb(triplet, alpha: darkSoft)) }
        var line: Color { FGColor.dynamic(FGColor.rgb(triplet, alpha: lightLine), FGColor.rgb(triplet, alpha: darkLine)) }
    }

    // MARK: - 表面与边框

    private static let gray50: (Int, Int, Int) = (247, 248, 250)
    private static let gray100: (Int, Int, Int) = (240, 242, 245)

    public static let canvas = dynamic(rgb(234, 237, 242), rgb(16, 18, 21))
    public static let surface = dynamic(rgb(255, 255, 255), rgb(30, 33, 39))
    public static let surface2 = dynamic(rgb(gray50), rgb(37, 41, 50))
    public static let surface3 = dynamic(rgb(gray100), rgb(45, 50, 60))
    public static let surfaceHi = dynamic(rgb(16, 24, 40, alpha: 0.038), rgb(255, 255, 255, alpha: 0.055))
    public static let surfacePress = dynamic(rgb(16, 24, 40, alpha: 0.075), rgb(255, 255, 255, alpha: 0.095))

    public static let line1 = dynamic(rgb(16, 24, 40, alpha: 0.07), rgb(255, 255, 255, alpha: 0.07))
    public static let line2 = dynamic(rgb(16, 24, 40, alpha: 0.12), rgb(255, 255, 255, alpha: 0.13))
    public static let line3 = dynamic(rgb(16, 24, 40, alpha: 0.22), rgb(255, 255, 255, alpha: 0.22))
    public static let hairline = dynamic(rgb(16, 24, 40, alpha: 0.055), rgb(255, 255, 255, alpha: 0.05))
    /// 浮板顶部 1px 内高光。
    public static let edgeHi = dynamic(rgb(255, 255, 255, alpha: 0.75), rgb(255, 255, 255, alpha: 0.07))

    // MARK: - 文字

    public static let text1 = dynamic(rgb(23, 27, 33), rgb(233, 237, 242))
    public static let text2 = dynamic(rgb(95, 104, 117), rgb(162, 170, 182))
    public static let text3 = dynamic(rgb(139, 147, 160), rgb(110, 119, 131))

    // MARK: - 遮罩与 Toast

    public static let overlay = dynamic(rgb(0, 0, 0, alpha: 0.15), rgb(0, 0, 0, alpha: 0.42))
    public static let toastBg = dynamic(rgb(28, 34, 44, alpha: 0.94), rgb(46, 51, 60, alpha: 0.96))
    public static let toastFg = dynamic(rgb(255, 255, 255), rgb(242, 245, 248))

    // MARK: - 语义色族

    private static let okFamily = Family(
        triplet: (16, 163, 111), lightSolid: (16, 163, 111), darkSolid: (44, 196, 135),
        lightText: (10, 125, 85), darkText: (95, 215, 165),
        lightSoft: 0.12, darkSoft: 0.17, lightLine: 0.28, darkLine: 0.34
    )
    private static let warnFamily = Family(
        triplet: (217, 119, 6), lightSolid: (217, 119, 6), darkSolid: (240, 166, 60),
        lightText: (163, 90, 4), darkText: (245, 192, 120),
        lightSoft: 0.12, darkSoft: 0.17, lightLine: 0.30, darkLine: 0.34
    )
    private static let dangerFamily = Family(
        triplet: (220, 38, 38), lightSolid: (220, 38, 38), darkSolid: (242, 85, 90),
        lightText: (185, 28, 28), darkText: (255, 139, 143),
        lightSoft: 0.11, darkSoft: 0.16, lightLine: 0.28, darkLine: 0.34
    )
    private static let idleFamily = Family(
        triplet: (138, 147, 161), lightSolid: (138, 147, 161), darkSolid: (123, 132, 146),
        lightText: (107, 116, 128), darkText: (154, 163, 176),
        lightSoft: 0.13, darkSoft: 0.18, lightLine: 0.26, darkLine: 0.32
    )

    public static let okSolid = okFamily.solid
    public static let okText = okFamily.text
    public static let okSoft = okFamily.soft
    public static let okLine = okFamily.line

    public static let warnSolid = warnFamily.solid
    public static let warnText = warnFamily.text
    public static let warnSoft = warnFamily.soft
    public static let warnLine = warnFamily.line

    public static let dangerSolid = dangerFamily.solid
    public static let dangerText = dangerFamily.text
    public static let dangerSoft = dangerFamily.soft
    public static let dangerLine = dangerFamily.line

    public static let idleSolid = idleFamily.solid
    public static let idleText = idleFamily.text
    public static let idleSoft = idleFamily.soft
    public static let idleLine = idleFamily.line

    // MARK: - L3 平台：macOS accent（#007aff / #0a84ff）与其推导

    private static let accentTripletLight: (Int, Int, Int) = (0, 122, 255)
    private static let accentTripletDark: (Int, Int, Int) = (10, 132, 255)
    private static let blackTriplet: (Int, Int, Int) = (0, 0, 0)
    private static let whiteTriplet: (Int, Int, Int) = (255, 255, 255)

    public static let accent = dynamic(rgb(accentTripletLight), rgb(accentTripletDark))
    public static let accentText = accent
    public static let accentSoft = dynamic(rgb(accentTripletLight, alpha: 0.12), rgb(accentTripletDark, alpha: 0.12))
    public static let accentLine = dynamic(rgb(accentTripletLight, alpha: 0.30), rgb(accentTripletDark, alpha: 0.30))
    public static let accentHi = dynamic(rgb(accentTripletLight, alpha: 0.18), rgb(accentTripletDark, alpha: 0.18))
    public static let onAccent = dynamic(rgb(whiteTriplet), rgb(whiteTriplet))

    /// 浅色向黑取混，深色向白取混（tokens.css color-mix 原值）。
    public static let accentHover = dynamic(
        mix(accentTripletLight, with: blackTriplet, ratio: 0.91),
        mix(accentTripletDark, with: whiteTriplet, ratio: 0.88)
    )
    public static let accentPress = dynamic(
        mix(accentTripletLight, with: blackTriplet, ratio: 0.84),
        mix(accentTripletDark, with: whiteTriplet, ratio: 0.76)
    )
    public static let dangerHover = dynamic(
        mix(dangerFamily.triplet, with: blackTriplet, ratio: 0.90),
        mix(dangerFamily.triplet, with: blackTriplet, ratio: 0.90)
    )

    // MARK: - 延迟分级 → 软底/文字对

    public static func tier(_ tier: LatencyTier) -> (soft: Color, text: Color) {
        switch tier {
        case .good: (okSoft, okText)
        case .warn: (warnSoft, warnText)
        case .bad: (dangerSoft, dangerText)
        case .idle: (idleSoft, idleText)
        }
    }
}
