import FlowGridCore
import SwiftUI

/// 托盘浮层（300px，菜单栏主入口）。
/// 交互逻辑：当前设备 = 正在接收键鼠的一方；快速切换 = 本机 ⇄ 对端（真实接管）。
public struct TrayView: View {
    @Bindable private var model: AppModel

    public init(model: AppModel) {
        self._model = Bindable(model)
    }

    public var body: some View {
        VStack(spacing: 0) {
            head
            VStack(spacing: 0) {
                activeDeviceGroup
                separator
                quickSwitchGroup
                separator
                connectionsGroup
                separator
                dataSyncGroup
                separator
                togglesGroup
                separator
                navGroup
            }
            bottomBar
        }
        .frame(width: FGGeometry.trayWidth)
        .fgToastHost(model.toast, bottomInset: 20)
        .fgSwitchOverlayHost(model.switchOverlay)
    }

    // MARK: - 头部

    private var head: some View {
        HStack(spacing: FGSpacing.s3) {
            HStack(spacing: 6) {
                FGIcon("link", size: 16, color: FGColor.text1)
                Text("FlowGrid")
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundStyle(FGColor.text1)
            }
            Spacer(minLength: 0)
            if model.connectionStatus.isConnected {
                let latency = model.primaryLatency
                FGLatPill(ms: latency.ms, tier: latency.tier)
            } else {
                FGBadge("未连接", kind: .neutral)
            }
        }
        .padding(.vertical, 10)
        .padding(.horizontal, FGSpacing.s3)
        .overlay(alignment: .bottom) {
            Rectangle().fill(FGColor.line1).frame(height: 1)
        }
    }

    // MARK: - 当前设备

    private var activeDeviceGroup: some View {
        VStack(alignment: .leading, spacing: 0) {
            TrayLabel("当前设备")
            HStack(spacing: FGSpacing.s2) {
                Text(model.currentDevice.initials)
                    .font(.system(size: 11, weight: .bold))
                    .foregroundStyle(FGColor.accentText)
                    .frame(width: 28, height: 28)
                    .background(FGColor.accentSoft)
                    .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
                VStack(alignment: .leading, spacing: 1) {
                    Text(model.currentDevice.name)
                        .font(.system(size: 13, weight: .semibold))
                        .foregroundStyle(FGColor.text1)
                    Text(activeMetaText)
                        .font(.system(size: 11))
                        .foregroundStyle(FGColor.text2)
                }
                Spacer(minLength: 0)
                FGBadge(model.captureEnabled ? "控制中" : "本机", kind: model.captureEnabled ? .ok : .neutral)
            }
            .padding(.vertical, FGSpacing.s2)
            .padding(.horizontal, FGSpacing.s3)
            .background(FGColor.accentSoft)
            .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous)
                    .strokeBorder(FGColor.accentLine, lineWidth: 1)
            )
            .padding(.vertical, FGSpacing.s1)
            .padding(.horizontal, FGSpacing.s2)
        }
        .padding(.vertical, FGSpacing.s1)
    }

    private var activeMetaText: String {
        if model.captureEnabled, let peer = model.peerDevice {
            return "键鼠已发往 \(peer.name)"
        }
        return "键鼠在本机 · ⌘2 切回"
    }

    // MARK: - 快速切换（本机 ⇄ 对端）

    private var quickSwitchGroup: some View {
        VStack(alignment: .leading, spacing: 0) {
            TrayLabel("快速切换")
            HStack(spacing: 6) {
                ForEach(model.quickSwitchDevices, id: \.id) { device in
                    QuickSwitchButton(
                        device: device,
                        shortcut: device.isLocal ? "2" : "1",
                        isCurrent: device.id == model.currentControlledDeviceID
                    ) {
                        model.switchTo(deviceID: device.id)
                    }
                }
                if model.peerDevice?.isOnline != true {
                    HStack(spacing: FGSpacing.s1) {
                        FGIcon("info", size: 12, color: FGColor.text2)
                        Text("连接设备后可切换到对端")
                            .font(.system(size: 11))
                            .foregroundStyle(FGColor.text2)
                    }
                    .frame(maxWidth: .infinity)
                }
            }
            .padding(.vertical, 6)
            .padding(.horizontal, FGSpacing.s3)
        }
        .padding(.vertical, FGSpacing.s1)
    }

    // MARK: - 连接 / 数据与同步 / 导航

    private var connectionsGroup: some View {
        VStack(alignment: .leading, spacing: 0) {
            TrayLabel("连接")
            TrayItem(icon: "plus", title: "连接设备") {
                FGBadge("DirectLink", kind: .neutral)
            } action: {
                model.openPanel(.pairing)
            }
            TrayItem(icon: "devices", title: "设备与连接") {
                Text("\(model.devices.count) 台")
                    .font(.system(size: 11))
                    .monospacedDigit()
                    .foregroundStyle(FGColor.text2)
            } action: {
                model.openPanel(.connections)
            }
            TrayItem(icon: "activity", title: "网络延迟") {
                let latency = model.primaryLatency
                FGLatPill(ms: latency.ms, tier: latency.tier)
            } action: {
                model.openPanel(.latency)
            }
        }
        .padding(.vertical, FGSpacing.s1)
    }

    private var dataSyncGroup: some View {
        VStack(alignment: .leading, spacing: 0) {
            TrayLabel("数据与同步")
            TrayItem(icon: "clipboard", title: "剪贴板") {
                FGBadge("\(model.clipboardHistory.count) 项", kind: .neutral)
            } action: {
                model.openPanel(.clipboard)
            }
            TrayItem(icon: "file", title: "文件传输") {
                FGBadge("\(model.activeTransfers.count) 项", kind: .neutral)
            } action: {
                model.openPanel(.files)
            }
        }
        .padding(.vertical, FGSpacing.s1)
    }

    private var togglesGroup: some View {
        VStack(alignment: .leading, spacing: 0) {
            TrayLabel("快捷开关")
            TrayToggle(label: "键位映射", isOn: Binding(
                get: { model.settings.keymapEnabled },
                set: { _ in model.toggleSetting(\.keymapEnabled, label: "键位映射") }
            ))
            TrayToggle(label: "剪贴板同步", isOn: Binding(
                get: { model.settings.clipboardSync },
                set: { _ in model.toggleSetting(\.clipboardSync, label: "剪贴板同步") }
            ))
            TrayToggle(label: "鼠标平滑", isOn: Binding(
                get: { model.settings.mouseSmoothing },
                set: { _ in model.toggleSetting(\.mouseSmoothing, label: "鼠标平滑") }
            ))
            TrayToggle(label: "自动路由", isOn: Binding(
                get: { model.settings.autoRoute },
                set: { _ in model.toggleSetting(\.autoRoute, label: "自动路由") }
            ))
        }
        .padding(.vertical, FGSpacing.s1)
    }

    private var navGroup: some View {
        VStack(alignment: .leading, spacing: 0) {
            TrayItem(icon: "keyboard", title: "键位映射") {
                TrayKbd("⌘K")
            } action: {
                model.openPanel(.keymap)
            }
        }
        .padding(.vertical, FGSpacing.s1)
    }

    // MARK: - 底部工具条（右下角图标：设置 / 退出）

    private var bottomBar: some View {
        HStack(spacing: FGSpacing.s1) {
            Spacer(minLength: 0)
            TrayIconButton(icon: "settings", help: "设置（⌘,）") {
                model.openPanel(.settings)
            }
            TrayIconButton(icon: "power", help: "退出 FlowGrid（⌘Q）", danger: true) {
                model.quit()
            }
        }
        .padding(.horizontal, FGSpacing.s3)
        .padding(.vertical, 8)
        .overlay(alignment: .top) {
            Rectangle().fill(FGColor.hairline).frame(height: 1)
        }
    }

    private var separator: some View {
        Rectangle()
            .fill(FGColor.hairline)
            .frame(height: 1)
            .padding(.horizontal, FGSpacing.s3)
            .padding(.vertical, 3)
    }
}

// MARK: - 托盘内部构件

private struct TrayLabel: View {
    let text: String

    init(_ text: String) {
        self.text = text
    }

    var body: some View {
        Text(text)
            .font(.system(size: 11, weight: .semibold))
            .tracking(11 * 0.04)
            .foregroundStyle(FGColor.text2)
            .padding(.vertical, 6)
            .padding(.horizontal, FGSpacing.s3)
            .frame(maxWidth: .infinity, alignment: .leading)
    }
}

private struct TrayKbd: View {
    let text: String

    init(_ text: String) {
        self.text = text
    }

    var body: some View {
        Text(text)
            .font(.system(size: 11))
            .foregroundStyle(FGColor.text2)
    }
}

private struct TrayItem<Trailing: View>: View {
    let icon: String
    let title: String
    let danger: Bool
    let trailing: Trailing
    let action: () -> Void

    init(icon: String, title: String, danger: Bool = false, @ViewBuilder trailing: () -> Trailing, action: @escaping () -> Void) {
        self.icon = icon
        self.title = title
        self.danger = danger
        self.trailing = trailing()
        self.action = action
    }

    var body: some View {
        Button(action: action) {
            HStack(spacing: FGSpacing.s2) {
                FGIcon(icon, size: 16, color: danger ? FGColor.dangerText : FGColor.text2)
                Text(title)
                    .font(.system(size: 12))
                    .foregroundStyle(danger ? FGColor.dangerText : FGColor.text1)
                Spacer(minLength: 0)
                trailing
            }
            .padding(.vertical, 7)
            .padding(.horizontal, FGSpacing.s3)
            .contentShape(Rectangle())
        }
        .buttonStyle(TrayRowStyle(danger: danger))
    }

    private struct TrayRowStyle: ButtonStyle {
        let danger: Bool

        func makeBody(configuration: Configuration) -> some View {
            RowBody(configuration: configuration, danger: danger)
        }

        private struct RowBody: View {
            let configuration: Configuration
            let danger: Bool
            @State private var hovering = false

            var body: some View {
                configuration.label
                    .background(background)
                    .onHover { hovering = $0 }
                    .animation(FGMotion.state, value: hovering)
            }

            private var background: Color {
                if configuration.isPressed {
                    return danger ? FGColor.dangerSoft : FGColor.surfacePress
                }
                if hovering {
                    return danger ? FGColor.dangerSoft : FGColor.surfaceHi
                }
                return .clear
            }
        }
    }
}

private struct TrayToggle: View {
    let label: String
    @Binding var isOn: Bool
    @State private var hovering = false

    init(label: String, isOn: Binding<Bool>) {
        self.label = label
        self._isOn = isOn
    }

    var body: some View {
        HStack(spacing: FGSpacing.s2) {
            Text(label)
                .font(.system(size: 12))
                .foregroundStyle(FGColor.text1)
            Spacer(minLength: 0)
            FGSwitch(isOn: $isOn)
        }
        .padding(.vertical, 7)
        .padding(.horizontal, FGSpacing.s3)
        .background(hovering ? FGColor.surfaceHi : Color.clear)
        .onHover { hovering = $0 }
        .animation(FGMotion.state, value: hovering)
    }
}

private struct TrayIconButton: View {
    let icon: String
    let help: String
    let danger: Bool
    let action: () -> Void
    @State private var hovering = false

    init(icon: String, help: String, danger: Bool = false, action: @escaping () -> Void) {
        self.icon = icon
        self.help = help
        self.danger = danger
        self.action = action
    }

    var body: some View {
        Button(action: action) {
            FGIcon(icon, size: 15, color: danger ? FGColor.dangerText : FGColor.text2)
                .frame(width: 28, height: 28)
                .background(
                    hovering
                        ? (danger ? FGColor.dangerSoft : FGColor.surfaceHi)
                        : Color.clear
                )
                .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
        }
        .buttonStyle(.plain)
        .help(help)
        .onHover { hovering = $0 }
        .animation(FGMotion.state, value: hovering)
    }
}

private struct QuickSwitchButton: View {
    let device: Device
    let shortcut: String
    let isCurrent: Bool
    let action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            VStack(spacing: FGSpacing.s1) {
                Text(device.initials)
                    .font(.system(size: 10, weight: .bold))
                    .foregroundStyle(.white)
                    .frame(width: 24, height: 24)
                    .background(device.tileColor)
                    .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
                Text("⌘\(shortcut)")
                    .font(.system(size: 11))
                    .foregroundStyle(FGColor.text2)
            }
            .frame(maxWidth: .infinity)
            .padding(6)
            .background(isCurrent ? FGColor.accentSoft : (hovering ? FGColor.accentSoft : Color.clear))
            .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous)
                    .strokeBorder(isCurrent || hovering ? FGColor.accent : FGColor.line2, lineWidth: 1)
            )
        }
        .buttonStyle(.plain)
        .help(device.isLocal ? "本机（⌘2）" : "接管 \(device.name)（⌘1）")
        .onHover { hovering = $0 }
        .animation(FGMotion.state, value: hovering)
    }
}
