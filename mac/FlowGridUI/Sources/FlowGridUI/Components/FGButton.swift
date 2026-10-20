import SwiftUI

/// 按钮：28px 高（sm 24），radius 6，12/600（sm 11/600）。
/// 变体：primary（accent）/ secondary（surface+线框）/ danger / ghost。
public struct FGButton: View {
    public enum Kind: Sendable {
        case primary
        case secondary
        case danger
        case ghost
    }

    private let title: String?
    private let icon: String?
    private let kind: Kind
    private let small: Bool
    private let action: () -> Void

    public init(_ title: String, icon: String? = nil, kind: Kind = .primary, small: Bool = false, action: @escaping () -> Void) {
        self.title = title
        self.icon = icon
        self.kind = kind
        self.small = small
        self.action = action
    }

    public var body: some View {
        Button(action: action) {
            HStack(spacing: 6) {
                if let icon {
                    FGIcon(icon, size: small ? 14 : 16)
                }
                if let title {
                    Text(title)
                }
            }
        }
        .buttonStyle(FGButtonStyle(kind: kind, small: small))
    }
}

public struct FGButtonStyle: ButtonStyle {
    let kind: FGButton.Kind
    let small: Bool

    public func makeBody(configuration: Configuration) -> some View {
        StyleBody(configuration: configuration, kind: kind, small: small)
    }

    private struct StyleBody: View {
        let configuration: ButtonStyle.Configuration
        let kind: FGButton.Kind
        let small: Bool
        @State private var hovering = false
        @Environment(\.isEnabled) private var isEnabled

        var body: some View {
            configuration.label
                .font(.system(size: small ? 11 : 12, weight: .semibold))
                .padding(.horizontal, small ? FGSpacing.s2 : FGSpacing.s3)
                .frame(height: small ? 24 : FGGeometry.controlHeight)
                .foregroundStyle(foreground)
                .background(background)
                .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
                .overlay(border)
                .offset(y: configuration.isPressed ? 1 : 0)
                .opacity(isEnabled ? 1 : 0.45)
                .animation(FGMotion.state, value: configuration.isPressed)
                .animation(FGMotion.state, value: hovering)
                .onHover { hovering = $0 }
        }

        private var foreground: Color {
            switch kind {
            case .primary, .danger: FGColor.onAccent
            case .secondary: FGColor.text1
            case .ghost: hovering ? FGColor.text1 : FGColor.text2
            }
        }

        @ViewBuilder private var background: some View {
            switch kind {
            case .primary:
                if configuration.isPressed {
                    FGColor.accentPress
                } else if hovering {
                    FGColor.accentHover
                } else {
                    FGColor.accent
                }
            case .danger:
                if hovering || configuration.isPressed { FGColor.dangerHover } else { FGColor.dangerSolid }
            case .secondary:
                if configuration.isPressed {
                    FGColor.surfacePress
                } else if hovering {
                    FGColor.surfaceHi
                } else {
                    FGColor.surface
                }
            case .ghost:
                if configuration.isPressed {
                    FGColor.surfacePress
                } else if hovering {
                    FGColor.surfaceHi
                } else {
                    Color.clear
                }
            }
        }

        @ViewBuilder private var border: some View {
            switch kind {
            case .secondary:
                RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous)
                    .strokeBorder(hovering ? FGColor.line3 : FGColor.line2, lineWidth: 1)
            default:
                EmptyView()
            }
        }
    }
}

/// 图标按钮：26×26，text-3 → hover text-1 + surface-hi；按下 surface-press；带悬停提示。
public struct FGIconButton: View {
    private let icon: String
    private let size: CGFloat
    private let tint: Color?
    private let help: String?
    private let action: () -> Void
    @State private var hovering = false

    public init(_ icon: String, size: CGFloat = 16, tint: Color? = nil, help: String? = nil, action: @escaping () -> Void) {
        self.icon = icon
        self.size = size
        self.tint = tint
        self.help = help
        self.action = action
    }

    public var body: some View {
        Button(action: action) {
            FGIcon(icon, size: size, color: tint ?? (hovering ? FGColor.text1 : FGColor.text3))
                .frame(width: FGGeometry.iconButtonSize, height: FGGeometry.iconButtonSize)
        }
        .buttonStyle(IconButtonBackgroundStyle(hovered: hovering))
        .onHover { hovering = $0 }
        .fgHelp(help)
    }

    private struct IconButtonBackgroundStyle: ButtonStyle {
        let hovered: Bool

        func makeBody(configuration: Configuration) -> some View {
            configuration.label
                .background(
                    configuration.isPressed
                        ? FGColor.surfacePress
                        : (hovered ? FGColor.surfaceHi : Color.clear)
                )
                .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
                .animation(FGMotion.state, value: configuration.isPressed)
        }
    }
}
