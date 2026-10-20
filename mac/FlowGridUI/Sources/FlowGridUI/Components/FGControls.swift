import SwiftUI

// MARK: - 开关（36×20，滑块 16，on = okSolid）

public struct FGSwitch: View {
    @Binding private var isOn: Bool

    public init(isOn: Binding<Bool>) {
        self._isOn = isOn
    }

    public var body: some View {
        Button {
            withAnimation(FGMotion.switchTrack) { isOn.toggle() }
        } label: {
            ZStack(alignment: .leading) {
                Capsule()
                    .fill(isOn ? FGColor.okSolid : FGColor.surface3)
                    .frame(width: FGGeometry.switchWidth, height: FGGeometry.switchHeight)
                Circle()
                    .fill(.white)
                    .frame(width: FGGeometry.switchKnob, height: FGGeometry.switchKnob)
                    .overlay(Circle().strokeBorder(Color.black.opacity(0.08), lineWidth: 0.5))
                    .shadow(color: FGShadow.bar.color, radius: 1.5, x: 0, y: 1)
                    .offset(x: isOn ? FGGeometry.switchWidth - FGGeometry.switchKnob - 2 : 2)
            }
        }
        .buttonStyle(.plain)
    }
}

// MARK: - 输入框（28 高，radius 6，聚焦 accent 描边 + 3px soft 环）

public struct FGInput: View {
    private let placeholder: String
    @Binding private var text: String
    private let width: CGFloat?
    @FocusState private var focused: Bool

    public init(_ placeholder: String, text: Binding<String>, width: CGFloat? = nil) {
        self.placeholder = placeholder
        self._text = text
        self.width = width
    }

    public var body: some View {
        TextField(placeholder, text: $text)
            .textFieldStyle(.plain)
            .font(.system(size: 12))
            .padding(.horizontal, FGSpacing.s2)
            .frame(height: FGGeometry.controlHeight)
            .frame(width: width)
            .background(FGColor.surface)
            .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous)
                    .strokeBorder(focused ? FGColor.accent : FGColor.line2, lineWidth: 1)
            )
            .background {
                if focused {
                    RoundedRectangle(cornerRadius: FGRadius.control + 3, style: .continuous)
                        .strokeBorder(FGColor.accentSoft, lineWidth: 3)
                        .padding(-3)
                }
            }
            .focused($focused)
            .animation(FGMotion.state, value: focused)
    }
}

// MARK: - 数字输入（28 高，与 FGInput 同款，绑定 Int）

public struct FGNumberInput: View {
    @Binding private var value: Int
    private let width: CGFloat
    @FocusState private var focused: Bool
    @State private var text: String = ""

    public init(value: Binding<Int>, width: CGFloat = 64) {
        self._value = value
        self.width = width
    }

    public var body: some View {
        TextField("", text: $text)
            .textFieldStyle(.plain)
            .font(.system(size: 12))
            .multilineTextAlignment(.center)
            .padding(.horizontal, FGSpacing.s2)
            .frame(height: FGGeometry.controlHeight)
            .frame(width: width)
            .background(FGColor.surface)
            .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous)
                    .strokeBorder(focused ? FGColor.accent : FGColor.line2, lineWidth: 1)
            )
            .background {
                if focused {
                    RoundedRectangle(cornerRadius: FGRadius.control + 3, style: .continuous)
                        .strokeBorder(FGColor.accentSoft, lineWidth: 3)
                        .padding(-3)
                }
            }
            .focused($focused)
            .onAppear { text = String(value) }
            .onChange(of: text) { _, newValue in
                if let parsed = Int(newValue.trimmingCharacters(in: .whitespaces)) {
                    value = parsed
                }
            }
            .onChange(of: value) { _, newValue in
                if Int(text) != newValue {
                    text = String(newValue)
                }
            }
            .animation(FGMotion.state, value: focused)
    }
}

// MARK: - 下拉（28 高，右侧 chevron，点击弹菜单）

public struct FGSelect<Value: Hashable>: View {
    private let options: [(value: Value, label: String)]
    @Binding private var selection: Value
    private let width: CGFloat?
    @State private var hovering = false

    public init(options: [(value: Value, label: String)], selection: Binding<Value>, width: CGFloat? = nil) {
        self.options = options
        self._selection = selection
        self.width = width
    }

    public var body: some View {
        Menu {
            ForEach(options, id: \.value) { option in
                Button {
                    selection = option.value
                } label: {
                    if option.value == selection {
                        Label(option.label, systemImage: "checkmark")
                    } else {
                        Text(option.label)
                    }
                }
            }
        } label: {
            HStack(spacing: 0) {
                Text(options.first { $0.value == selection }?.label ?? "")
                    .font(.system(size: 12))
                    .foregroundStyle(FGColor.text1)
                    .lineLimit(1)
                Spacer(minLength: FGSpacing.s2)
                FGIcon("chevronDown", size: 14, color: FGColor.text3)
            }
            .padding(.leading, FGSpacing.s2)
            .padding(.trailing, FGSpacing.s1)
            .frame(height: FGGeometry.controlHeight)
            .frame(width: width)
            .background(hovering ? FGColor.surfaceHi : FGColor.surface)
            .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous)
                    .strokeBorder(FGColor.line2, lineWidth: 1)
            )
        }
        .menuStyle(.button)
        .buttonStyle(.plain)
        .menuIndicator(.hidden)
        .fixedSize()
        .onHover { hovering = $0 }
    }
}

// MARK: - 分段控件（track surface-3 / 内钮 24 高 radius 4 / 选中 surface + bar 阴影）

public struct FGSegmented<Value: Hashable>: View {
    private let options: [(value: Value, label: String)]
    @Binding private var selection: Value

    public init(options: [(value: Value, label: String)], selection: Binding<Value>) {
        self.options = options
        self._selection = selection
    }

    public var body: some View {
        HStack(spacing: 2) {
            ForEach(options, id: \.value) { option in
                SegItem(label: option.label, selected: option.value == selection) {
                    withAnimation(FGMotion.state) { selection = option.value }
                }
            }
        }
        .padding(2)
        .background(FGColor.surface3)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
    }

    private struct SegItem: View {
        let label: String
        let selected: Bool
        let action: () -> Void
        @State private var hovering = false

        var body: some View {
            Button(action: action) {
                Text(label)
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundStyle(selected ? FGColor.text1 : (hovering ? FGColor.text1 : FGColor.text2))
                    .padding(.horizontal, 10)
                    .frame(height: 24)
                    .background(selected ? FGColor.surface : Color.clear)
                    .clipShape(RoundedRectangle(cornerRadius: FGRadius.segItem, style: .continuous))
                    .shadow(
                        color: selected ? FGShadow.bar.color : .clear,
                        radius: selected ? 1.5 : 0, x: 0, y: selected ? 1 : 0
                    )
            }
            .buttonStyle(.plain)
            .onHover { hovering = $0 }
            .animation(FGMotion.state, value: hovering)
        }
    }
}

// MARK: - 按键帽（min-width 34，mono 11/600，底部 1px 立体阴影；ghost 虚线）

public struct FGKeyCap: View {
    private let text: String
    private let ghost: Bool

    public init(_ text: String, ghost: Bool = false) {
        self.text = text
        self.ghost = ghost
    }

    public var body: some View {
        Text(text)
            .font(.system(size: 11, weight: .semibold, design: .monospaced))
            .foregroundStyle(ghost ? FGColor.text3 : FGColor.text1)
            .frame(minWidth: 34)
            .padding(.vertical, 5)
            .padding(.horizontal, 7)
            .background(ghost ? Color.clear : FGColor.surface)
            .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous)
                    .strokeBorder(
                        ghost ? FGColor.line2 : FGColor.line2,
                        style: ghost ? StrokeStyle(lineWidth: 1, dash: [3, 3]) : StrokeStyle(lineWidth: 1)
                    )
            )
            .shadow(color: ghost ? .clear : FGColor.line2, radius: 0, x: 0, y: 1)
    }
}
