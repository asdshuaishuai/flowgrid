import AppKit
import SwiftUI

/// 五档阴影（tokens.css 第 0 节原值；深色主题整套加深）。
public struct FGShadowStyle: Sendable {
    public let color: Color
    public let blur: CGFloat
    public let offsetY: CGFloat

    public init(color: Color, blur: CGFloat, offsetY: CGFloat) {
        self.color = color
        self.blur = blur
        self.offsetY = offsetY
    }

    /// SwiftUI 的 radius 与 CSS blur 近似 2:1。
    public var radius: CGFloat { blur / 2 }
}

public enum FGShadow {
    private static func dynamic(_ light: (CGFloat, CGFloat, CGFloat, CGFloat), _ dark: (CGFloat, CGFloat, CGFloat, CGFloat)) -> Color {
        Color(nsColor: NSColor(name: nil) { appearance in
            let c = appearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua ? dark : light
            return NSColor(srgbRed: c.0 / 255, green: c.1 / 255, blue: c.2 / 255, alpha: c.3)
        })
    }

    public static let panel = FGShadowStyle(
        color: dynamic((24, 30, 44, 0.14), (0, 0, 0, 0.48)), blur: 42, offsetY: 18
    )
    public static let tray = FGShadowStyle(
        color: dynamic((15, 23, 42, 0.16), (0, 0, 0, 0.44)), blur: 32, offsetY: 16
    )
    public static let pop = FGShadowStyle(
        color: dynamic((15, 23, 42, 0.22), (0, 0, 0, 0.52)), blur: 26, offsetY: 10
    )
    public static let bar = FGShadowStyle(
        color: dynamic((15, 23, 42, 0.18), (0, 0, 0, 0.40)), blur: 3, offsetY: 1
    )
    public static let lift = FGShadowStyle(
        color: dynamic((16, 24, 40, 0.08), (0, 0, 0, 0.30)), blur: 10, offsetY: 2
    )
}

public extension View {
    func fgShadow(_ style: FGShadowStyle) -> some View {
        shadow(color: style.color, radius: style.radius, x: 0, y: style.offsetY)
    }
}
