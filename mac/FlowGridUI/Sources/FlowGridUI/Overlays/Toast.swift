import Observation
import SwiftUI

/// Toast 中心：1800ms 停留、底部居中、连续触发重放动画（设计稿节奏）。
@MainActor
@Observable
public final class ToastCenter {
    public enum Kind: Sendable {
        case info
        case ok
        case danger

        var icon: String {
            switch self {
            case .info: "info"
            case .ok: "check"
            case .danger: "warn"
            }
        }
    }

    public struct Toast: Identifiable, Equatable, Sendable {
        public let id: UUID
        public let message: String
        public let kind: Kind
    }

    public private(set) var current: Toast?
    private var hideTask: Task<Void, Never>?

    public init() {}

    public func show(_ message: String, kind: Kind = .info) {
        hideTask?.cancel()
        current = Toast(id: UUID(), message: message, kind: kind)
        hideTask = Task { [weak self] in
            try? await Task.sleep(for: FGMotion.toastDwell)
            guard !Task.isCancelled else { return }
            self?.current = nil
        }
    }
}

public struct FGToastView: View {
    let toast: ToastCenter.Toast

    public init(toast: ToastCenter.Toast) {
        self.toast = toast
    }

    public var body: some View {
        HStack(spacing: FGSpacing.s2) {
            FGIcon(toast.kind.icon, size: 16, color: FGColor.toastFg)
            Text(toast.message)
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(FGColor.toastFg)
        }
        .padding(.vertical, FGSpacing.s2)
        .padding(.horizontal, FGSpacing.s4)
        .background(FGColor.toastBg)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
        .fgShadow(FGShadow.pop)
    }
}

public struct FGToastHost: ViewModifier {
    let center: ToastCenter
    let bottomInset: CGFloat

    public init(center: ToastCenter, bottomInset: CGFloat = 56) {
        self.center = center
        self.bottomInset = bottomInset
    }

    public func body(content: Content) -> some View {
        content
            .overlay(alignment: .bottom) {
                if let toast = center.current {
                    FGToastView(toast: toast)
                        .padding(.bottom, bottomInset)
                        .transition(.offset(y: 80).combined(with: .opacity))
                        .id(toast.id)
                }
            }
            .animation(FGMotion.toast, value: center.current)
    }
}

public extension View {
    /// 在浮板底部挂 Toast 层（默认抬到页脚上方；托盘等无页脚界面传 20）。
    func fgToastHost(_ center: ToastCenter, bottomInset: CGFloat = 56) -> some View {
        modifier(FGToastHost(center: center, bottomInset: bottomInset))
    }
}
