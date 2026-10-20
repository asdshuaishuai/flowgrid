import Observation
import SwiftUI

/// 切换设备遮罩：全屏 overlay(0.15) + 居中提示（scale .8→1，900ms 停留）。
@MainActor
@Observable
public final class SwitchOverlayCenter {
    public private(set) var deviceName: String?
    private var hideTask: Task<Void, Never>?

    public init() {}

    public func show(_ deviceName: String) {
        hideTask?.cancel()
        self.deviceName = deviceName
        hideTask = Task { [weak self] in
            try? await Task.sleep(for: FGMotion.switchDwell)
            guard !Task.isCancelled else { return }
            self?.deviceName = nil
        }
    }
}

public struct FGSwitchOverlayHost: ViewModifier {
    let center: SwitchOverlayCenter

    public init(center: SwitchOverlayCenter) {
        self.center = center
    }

    public func body(content: Content) -> some View {
        content
            .overlay {
                if let name = center.deviceName {
                    ZStack {
                        FGColor.overlay
                            .transition(.opacity)
                        HStack(spacing: FGSpacing.s2) {
                            FGIcon("link", size: 16, color: FGColor.toastFg)
                            Text("已切换到 \(name)")
                                .font(.system(size: 15, weight: .semibold))
                                .foregroundStyle(FGColor.toastFg)
                        }
                        .padding(.vertical, FGSpacing.s3)
                        .padding(.horizontal, FGSpacing.s5)
                        .background(FGColor.toastBg)
                        .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
                        .fgShadow(FGShadow.pop)
                        .transition(.scale(scale: 0.8).combined(with: .opacity))
                    }
                    .ignoresSafeArea()
                }
            }
            .animation(FGMotion.toast, value: center.deviceName)
    }
}

public extension View {
    /// 挂在浮板根视图：切换设备时的全屏遮罩 + 居中提示。
    func fgSwitchOverlayHost(_ center: SwitchOverlayCenter) -> some View {
        modifier(FGSwitchOverlayHost(center: center))
    }
}
