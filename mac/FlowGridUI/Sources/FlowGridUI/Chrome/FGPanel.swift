import AppKit
import SwiftUI

/// 浮板外壳：520px 宽、radius 12、线框 + 顶部 1px 内高光；
/// 标题栏 34px（macOS：红黄绿灯在左 + 标题）；可选固定页脚（不入滚动区）。
public struct FGPanel<Content: View, Footer: View>: View {
    private let title: String
    private let onClose: () -> Void
    private let rightSlot: AnyView?
    private let content: Content
    private let footer: Footer?

    public init(
        title: String,
        onClose: @escaping () -> Void,
        rightSlot: AnyView? = nil,
        @ViewBuilder content: () -> Content,
        @ViewBuilder footer: () -> Footer
    ) {
        self.title = title
        self.onClose = onClose
        self.rightSlot = rightSlot
        self.content = content()
        self.footer = footer()
    }

    public var body: some View {
        VStack(spacing: 0) {
            FGTitlebar(title: title, onClose: onClose, rightSlot: rightSlot)

            ScrollView(.vertical) {
                VStack(spacing: FGSpacing.s3) {
                    content
                }
                .padding(FGGeometry.contentPadding)
                .frame(maxWidth: .infinity, alignment: .top)
            }

            if let footer {
                footer
            }
        }
        .frame(width: FGGeometry.panelWidth)
        .background(FGColor.surface)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.surface, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: FGRadius.surface, style: .continuous)
                .strokeBorder(FGColor.line2, lineWidth: 1)
        )
        .overlay(alignment: .top) {
            UnevenRoundedRectangle(topLeadingRadius: FGRadius.surface, topTrailingRadius: FGRadius.surface)
                .strokeBorder(FGColor.edgeHi, lineWidth: 1)
                .frame(height: FGRadius.surface)
                .allowsHitTesting(false)
        }
    }
}

public extension FGPanel where Footer == EmptyView {
    init(title: String, onClose: @escaping () -> Void, rightSlot: AnyView? = nil, @ViewBuilder content: () -> Content) {
        self.init(title: title, onClose: onClose, rightSlot: rightSlot, content: content, footer: { EmptyView() })
    }
}

/// 浮板页脚（10/14；顶部 hairline，surface-2 底）。
public struct FGPanelFooter<Leading: View, Trailing: View>: View {
    private let leading: Leading
    private let trailing: Trailing

    public init(@ViewBuilder leading: () -> Leading, @ViewBuilder trailing: () -> Trailing) {
        self.leading = leading()
        self.trailing = trailing()
    }

    public var body: some View {
        HStack(spacing: FGSpacing.s3) {
            leading
            Spacer(minLength: 0)
            trailing
        }
        .padding(.vertical, 10)
        .padding(.horizontal, FGGeometry.contentPadding)
        .background(FGColor.surface2)
        .overlay(alignment: .top) {
            Rectangle().fill(FGColor.hairline).frame(height: 1)
        }
    }
}

/// 页脚左侧的图标小注（shield/info + 11px 文案）。
public struct FGFooterNote: View {
    private let icon: String
    private let text: String

    public init(icon: String, text: String) {
        self.icon = icon
        self.text = text
    }

    public var body: some View {
        HStack(spacing: FGSpacing.s1) {
            FGIcon(icon, size: 14, color: FGColor.text3)
            Text(text)
                .font(.system(size: 11))
                .foregroundStyle(FGColor.text3)
        }
    }
}

/// 标题栏：34px；macOS 窗口控件（11px 圆点、gap 7，悬停显示字形）+ 标题在左，右侧槽位。
public struct FGTitlebar: View {
    private let title: String
    private let onClose: () -> Void
    private let rightSlot: AnyView?

    public init(title: String, onClose: @escaping () -> Void, rightSlot: AnyView? = nil) {
        self.title = title
        self.onClose = onClose
        self.rightSlot = rightSlot
    }

    public var body: some View {
        HStack(spacing: FGSpacing.s2) {
            TrafficLights(onClose: onClose)
            Text(title)
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(FGColor.text1)
            Spacer(minLength: FGSpacing.s2)
            if let rightSlot {
                rightSlot
            }
        }
        .padding(.horizontal, FGSpacing.s3)
        .frame(height: FGGeometry.titlebarHeight)
        .background(FGColor.surface2)
        .overlay(alignment: .bottom) {
            Rectangle().fill(FGColor.hairline).frame(height: 1)
        }
    }
}

/// macOS 交通灯：整组悬停时显示 × / − / +，仅关闭可点（浮板无最小化/缩放语义）。
private struct TrafficLights: View {
    let onClose: () -> Void
    @State private var hoveringGroup = false

    var body: some View {
        HStack(spacing: 7) {
            light(
                color: Color(red: 1.0, green: 0.373, blue: 0.341), // #ff5f57
                symbol: "xmark",
                enabled: true,
                help: "关闭"
            ) {
                onClose()
            }
            light(
                color: Color(red: 0.996, green: 0.737, blue: 0.180), // #febc2e
                symbol: "minus",
                enabled: false,
                help: nil
            ) {}
            light(
                color: Color(red: 0.157, green: 0.784, blue: 0.251), // #28c840
                symbol: "plus",
                enabled: false,
                help: nil
            ) {}
        }
        .onHover { hoveringGroup = $0 }
        .animation(FGMotion.state, value: hoveringGroup)
    }

    private func light(
        color: Color,
        symbol: String,
        enabled: Bool,
        help: String?,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            ZStack {
                Circle()
                    .fill(hoveringGroup ? color : color.opacity(0.95))
                    .frame(width: 11, height: 11)
                if hoveringGroup {
                    Image(systemName: symbol)
                        .font(.system(size: 6.5, weight: .bold))
                        .foregroundStyle(Color.black.opacity(enabled ? 0.55 : 0.35))
                        .transition(.opacity)
                }
            }
        }
        .buttonStyle(.plain)
        .allowsHitTesting(enabled)
        .fgHelp(help)
    }
}
