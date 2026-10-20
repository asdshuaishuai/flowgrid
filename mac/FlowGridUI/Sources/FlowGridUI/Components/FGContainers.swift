import SwiftUI

// MARK: - 折叠区（radius 10，surface-2 底；chevron rotate -90 200ms）

public struct FGSection<Content: View>: View {
    private let icon: String?
    private let title: String
    private let badge: (text: String, kind: FGBadge.Kind)?
    private let initiallyExpanded: Bool
    private let content: Content
    @State private var expanded: Bool

    public init(
        _ title: String,
        icon: String? = nil,
        badge: (text: String, kind: FGBadge.Kind)? = nil,
        initiallyExpanded: Bool = true,
        @ViewBuilder content: () -> Content
    ) {
        self.icon = icon
        self.title = title
        self.badge = badge
        self.initiallyExpanded = initiallyExpanded
        self.content = content()
        self._expanded = State(initialValue: initiallyExpanded)
    }

    public var body: some View {
        VStack(spacing: 0) {
            Button {
                withAnimation(FGMotion.chevron) { expanded.toggle() }
            } label: {
                HStack(spacing: FGSpacing.s3) {
                    if let icon {
                        FGIcon(icon, size: 16, color: FGColor.text1)
                    }
                    Text(title)
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(FGColor.text1)
                    if let badge {
                        FGBadge(badge.text, kind: badge.kind)
                    }
                    Spacer(minLength: 0)
                    FGIcon("chevronDown", size: 16, color: FGColor.text3)
                        .rotationEffect(.degrees(expanded ? 0 : -90))
                }
                .padding(.vertical, 9)
                .padding(.horizontal, FGSpacing.s3)
                .contentShape(Rectangle())
            }
            .buttonStyle(SectionHeadStyle())

            if expanded {
                VStack(spacing: 0) {
                    Rectangle().fill(FGColor.hairline).frame(height: 1)
                    content
                }
                .transition(.opacity.combined(with: .move(edge: .top)))
            }
        }
        .background(FGColor.surface2)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous)
                .strokeBorder(FGColor.line2, lineWidth: 1)
        )
    }

    private struct SectionHeadStyle: ButtonStyle {
        func makeBody(configuration: Configuration) -> some View {
            HeadBody(configuration: configuration)
        }

        private struct HeadBody: View {
            let configuration: ButtonStyle.Configuration
            @State private var hovering = false

            var body: some View {
                configuration.label
                    .background(hovering ? FGColor.surfaceHi : Color.clear)
                    .onHover { hovering = $0 }
                    .animation(FGMotion.state, value: hovering)
            }
        }
    }
}

// MARK: - 列表（radius 10 卡片；行 10/12 + 底部 hairline；分组标题）

public struct FGList<Content: View>: View {
    private let content: Content

    public init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }

    public var body: some View {
        VStack(spacing: 0) {
            content
        }
        .background(FGColor.surface)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous)
                .strokeBorder(FGColor.line2, lineWidth: 1)
        )
    }
}

public struct FGListRow<Trailing: View>: View {
    private let label: String
    private let hint: String?
    private let trailing: Trailing
    @State private var hovering = false

    public init(_ label: String, hint: String? = nil, @ViewBuilder trailing: () -> Trailing) {
        self.label = label
        self.hint = hint
        self.trailing = trailing()
    }

    public var body: some View {
        HStack(spacing: FGSpacing.s3) {
            VStack(alignment: .leading, spacing: 2) {
                Text(label)
                    .font(.system(size: 12))
                    .foregroundStyle(FGColor.text1)
                if let hint {
                    Text(hint)
                        .font(.system(size: 11))
                        .foregroundStyle(FGColor.text3)
                }
            }
            Spacer(minLength: 0)
            trailing
        }
        .padding(.vertical, 10)
        .padding(.horizontal, FGSpacing.s3)
        .background(hovering ? FGColor.surfaceHi : Color.clear)
        .overlay(alignment: .bottom) {
            Rectangle().fill(FGColor.hairline).frame(height: 1)
        }
        .onHover { hovering = $0 }
    }
}

public struct FGListGroupTitle: View {
    private let text: String

    public init(_ text: String) {
        self.text = text
    }

    public var body: some View {
        Text(text)
            .font(.system(size: 11, weight: .semibold))
            .tracking(11 * 0.04)
            .foregroundStyle(FGColor.text3)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.top, FGSpacing.s2)
            .padding(.bottom, 6)
            .padding(.horizontal, FGSpacing.s3)
            .background(FGColor.surface2)
            .overlay(alignment: .bottom) {
                Rectangle().fill(FGColor.hairline).frame(height: 1)
            }
    }
}

// MARK: - 提示条（10/12，radius 10）

public struct FGBanner: View {
    public enum Kind: Sendable {
        case accent
        case warn
        case danger
        case ok
        case neutral

        var icon: String {
            switch self {
            case .accent, .neutral: "info"
            case .warn, .danger: "warn"
            case .ok: "check"
            }
        }
    }

    private let text: String
    private let kind: Kind

    public init(_ text: String, kind: Kind = .accent) {
        self.text = text
        self.kind = kind
    }

    public var body: some View {
        HStack(alignment: .top, spacing: FGSpacing.s2) {
            FGIcon(kind.icon, size: 16, color: foreground)
                .padding(.top, 1)
            Text(text)
                .font(.system(size: 12))
                .lineSpacing(max(0, 18 - 12 * 1.2))
                .foregroundStyle(foreground)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(.vertical, 10)
        .padding(.horizontal, FGSpacing.s3)
        .background(background)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous)
                .strokeBorder(border, lineWidth: 1)
        )
    }

    private var foreground: Color {
        switch kind {
        case .accent: FGColor.accentText
        case .warn: FGColor.warnText
        case .danger: FGColor.dangerText
        case .ok: FGColor.okText
        case .neutral: FGColor.text2
        }
    }

    private var background: Color {
        switch kind {
        case .accent: FGColor.accentSoft
        case .warn: FGColor.warnSoft
        case .danger: FGColor.dangerSoft
        case .ok: FGColor.okSoft
        case .neutral: FGColor.surface2
        }
    }

    private var border: Color {
        switch kind {
        case .accent: FGColor.accentLine
        case .warn: FGColor.warnLine
        case .danger: FGColor.dangerLine
        case .ok: FGColor.okLine
        case .neutral: FGColor.line2
        }
    }
}

// MARK: - 空态

public struct FGEmptyState: View {
    private let icon: String
    private let title: String
    private let desc: String?

    public init(icon: String, title: String, desc: String? = nil) {
        self.icon = icon
        self.title = title
        self.desc = desc
    }

    public var body: some View {
        VStack(spacing: FGSpacing.s2) {
            FGIcon(icon, size: 18, color: FGColor.text3)
                .frame(width: 40, height: 40)
                .background(FGColor.surface3)
                .clipShape(Circle())
            Text(title)
                .font(.system(size: 13, weight: .semibold))
                .foregroundStyle(FGColor.text1)
            if let desc {
                Text(desc)
                    .font(.system(size: 12))
                    .foregroundStyle(FGColor.text3)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 320)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, FGSpacing.s6)
        .padding(.horizontal, FGSpacing.s4)
    }
}

// MARK: - 进度条（5px pill；fill accent / ok）

public struct FGProgressBar: View {
    private let progress: Double
    private let ok: Bool

    public init(progress: Double, ok: Bool = false) {
        self.progress = progress
        self.ok = ok
    }

    public var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .leading) {
                Capsule().fill(FGColor.surface3)
                Capsule()
                    .fill(ok ? FGColor.okSolid : FGColor.accent)
                    .frame(width: max(0, min(1, progress)) * geo.size.width)
            }
        }
        .frame(height: 5)
        .animation(FGMotion.state, value: progress)
        .animation(FGMotion.state, value: ok)
    }
}

// MARK: - 预览卡（head 7/12 · body mono 12 max-h 92 · foot 6/12）

public struct FGPreviewCard<Head: View, Body: View, Foot: View>: View {
    private let head: Head
    private let bodyContent: Body
    private let foot: Foot

    public init(
        @ViewBuilder head: () -> Head,
        @ViewBuilder body: () -> Body,
        @ViewBuilder foot: () -> Foot
    ) {
        self.head = head()
        self.bodyContent = body()
        self.foot = foot()
    }

    public var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: FGSpacing.s2) {
                head
            }
            .font(.system(size: 11))
            .padding(.vertical, 7)
            .padding(.horizontal, FGSpacing.s3)
            .frame(maxWidth: .infinity)
            .background(FGColor.surface)
            .overlay(alignment: .bottom) {
                Rectangle().fill(FGColor.hairline).frame(height: 1)
            }

            ScrollView(.vertical) {
                HStack {
                    bodyContent
                    Spacer(minLength: 0)
                }
            }
            .frame(maxHeight: 92)
            .padding(FGSpacing.s3)

            HStack(spacing: FGSpacing.s2) {
                foot
            }
            .font(.system(size: 11))
            .foregroundStyle(FGColor.text3)
            .padding(.vertical, 6)
            .padding(.horizontal, FGSpacing.s3)
            .frame(maxWidth: .infinity)
            .background(FGColor.surface)
            .overlay(alignment: .top) {
                Rectangle().fill(FGColor.hairline).frame(height: 1)
            }
        }
        .background(FGColor.surface2)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous)
                .strokeBorder(FGColor.line1, lineWidth: 1)
        )
    }
}
