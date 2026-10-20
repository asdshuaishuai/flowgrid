import SwiftUI

public extension View {
    /// 条件帮助标签（macOS 悬停提示）。
    @ViewBuilder
    func fgHelp(_ text: String?) -> some View {
        if let text {
            self.help(text)
        } else {
            self
        }
    }

    /// 数值滚动过渡（延迟读数/胶囊）：值变化时数字纵向平滑切换。
    @ViewBuilder
    func fgNumericTransition(value: Double) -> some View {
        self
            .contentTransition(.numericText(value: value))
            .animation(FGMotion.toast, value: value)
    }
}
