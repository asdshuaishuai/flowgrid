import CoreGraphics

/// 窗口几何常量（tokens.css 第 0 节，逐像素搬自上一版，不得改动）。
public enum FGGeometry {
    public static let panelWidth: CGFloat = 520
    public static let trayWidth: CGFloat = 300
    public static let titlebarHeight: CGFloat = 34
    public static let contentPadding: CGFloat = 14
    /// 浮板最大高度 = 屏幕高 − 48。
    public static let panelMaxHeightInset: CGFloat = 48
    public static let controlHeight: CGFloat = 28
    public static let iconButtonSize: CGFloat = 26
    public static let switchWidth: CGFloat = 36
    public static let switchHeight: CGFloat = 20
    public static let switchKnob: CGFloat = 16
}

/// 间距刻度（4 的倍数）。
public enum FGSpacing {
    public static let s1: CGFloat = 4
    public static let s2: CGFloat = 8
    public static let s3: CGFloat = 12
    public static let s4: CGFloat = 16
    public static let s5: CGFloat = 20
    public static let s6: CGFloat = 24
    public static let s8: CGFloat = 32
}

/// 圆角：6 控件 / 10 卡片 / 12 浮板 / pill。
public enum FGRadius {
    public static let control: CGFloat = 6
    public static let card: CGFloat = 10
    public static let surface: CGFloat = 12
    public static let pill: CGFloat = 999
    /// 分段控件内钮（ui.css 中的 4px，非令牌档位）。
    public static let segItem: CGFloat = 4
}
