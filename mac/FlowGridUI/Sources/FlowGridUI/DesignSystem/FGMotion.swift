import AppKit
import SwiftUI

/// 动效节奏（tokens.css 第 0 节：.15s 微交互 / .22s 入场 / .3s Toast）。
public enum FGMotion {
    /// 系统「减弱动态效果」：开启时禁用位移/缩放/脉冲类动画。
    public static var reduceMotion: Bool {
        NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
    }

    /// cubic-bezier(.16, 1, .3, 1) —— macOS 入场曲线。
    public static let enter = Animation.timingCurve(0.16, 1, 0.3, 1, duration: 0.22)
    /// cubic-bezier(.4, 0, .2, 1) —— 通用状态过渡。
    public static let state = Animation.timingCurve(0.4, 0, 0.2, 1, duration: 0.15)
    public static let toast = Animation.timingCurve(0.4, 0, 0.2, 1, duration: 0.3)
    public static let chevron = Animation.timingCurve(0.4, 0, 0.2, 1, duration: 0.2)
    public static let chart = Animation.timingCurve(0.4, 0, 0.2, 1, duration: 0.5)
    public static let switchTrack = Animation.timingCurve(0.4, 0, 0.2, 1, duration: 0.2)
    public static let hover = Animation.timingCurve(0.4, 0, 0.2, 1, duration: 0.15)

    /// pulse 2s 无限（状态点）。
    public static let pulseDuration: Double = 2.0
    /// 浮板入场几何偏移。
    public static let enterOffset: CGFloat = -8
    public static let enterScalePanel: CGFloat = 0.97
    public static let enterScaleTray: CGFloat = 0.96
    /// Toast 停留。
    public static let toastDwell: Duration = .milliseconds(1800)
    /// 切换设备遮罩停留。
    public static let switchDwell: Duration = .milliseconds(900)
}
