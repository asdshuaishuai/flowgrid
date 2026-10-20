import FlowGridCore
import SwiftUI

/// 设备与连接：本机 + 真实对端；接管开关与快捷入口。
public struct ConnectionsView: View {
    @Bindable private var model: AppModel
    private let onClose: () -> Void

    public init(model: AppModel, onClose: @escaping () -> Void) {
        self._model = Bindable(model)
        self.onClose = onClose
    }

    public var body: some View {
        FGPanel(title: "设备与连接", onClose: onClose) {
            headerRow
            captureRow
            deviceList
            quickLinks
        } footer: {
            FGPanelFooter {
                FGFooterNote(icon: "shield", text: "DirectLink · 帧级 HMAC 校验")
            } trailing: {
                HStack(spacing: FGSpacing.s3) {
                    FGButton("设置", icon: "settings", kind: .secondary, small: true) {
                        model.openPanel(.settings)
                    }
                    FGButton("关闭", kind: .ghost, small: true) {
                        onClose()
                    }
                }
            }
        }
        .fgToastHost(model.toast)
        .fgSwitchOverlayHost(model.switchOverlay)
    }

    // MARK: - 头部

    private var headerRow: some View {
        HStack(spacing: FGSpacing.s3) {
            FGStatusPill(
                model.connectionStatus.displayText,
                dot: model.connectionStatus.isConnected ? .ok : .idle,
                pulsing: model.connectionStatus.isConnected
            )
            Spacer(minLength: 0)
            FGButton("连接设备", icon: "plus") {
                model.openPanel(.pairing)
            }
        }
    }

    // MARK: - 接管对端键鼠

    @ViewBuilder private var captureRow: some View {
        if model.connectionStatus.isConnected {
            VStack(spacing: FGSpacing.s2) {
                HStack(spacing: FGSpacing.s3) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("接管对端键鼠")
                            .font(.system(size: 12, weight: .semibold))
                            .foregroundStyle(FGColor.text1)
                        Text(model.captureEnabled ? "键鼠事件正发送到对端" : "键鼠仍在本机")
                            .font(.system(size: 11))
                            .foregroundStyle(FGColor.text3)
                    }
                    Spacer(minLength: 0)
                    FGSwitch(isOn: Binding(
                        get: { model.captureEnabled },
                        set: { _ = model.setCaptureEnabled($0) }
                    ))
                }
                if !model.accessibilityTrusted {
                    HStack(spacing: FGSpacing.s2) {
                        FGBanner("需要辅助功能权限才能接管键鼠（隐私与安全性 → 辅助功能）。", kind: .warn)
                        FGButton("去授权", kind: .secondary, small: true) {
                            model.openAccessibilitySettings()
                        }
                    }
                }
            }
            .padding(FGSpacing.s3)
            .background(FGColor.surface2)
            .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous)
                    .strokeBorder(FGColor.line2, lineWidth: 1)
            )
        }
    }

    // MARK: - 设备列表（本机 + 对端）

    private var deviceList: some View {
        VStack(spacing: FGSpacing.s3) {
            ForEach(model.devices) { device in
                FGDeviceRow(
                    device: device,
                    isCurrent: device.id == model.currentControlledDeviceID,
                    meta: meta(for: device)
                ) {
                    HStack(spacing: FGSpacing.s2) {
                        if device.isLocal {
                            FGBadge("本机", kind: .neutral)
                        } else {
                            if model.captureEnabled && device.isOnline {
                                FGBadge("控制中", kind: .ok)
                            }
                            let latency = model.latency(for: device)
                            FGLatPill(ms: latency.ms, tier: latency.tier)
                            if device.isOnline {
                                FGIconButton("unlink", help: "断开连接") {
                                    model.disconnectPeer()
                                }
                            } else {
                                FGIconButton("close", help: "移除离线设备") {
                                    model.removePeerDevice()
                                }
                            }
                        }
                    }
                }
            }
            if model.peerDevice == nil {
                FGEmptyHint("连接设备后，对端会出现在这里")
            }
        }
    }

    private func meta(for device: Device) -> String {
        if device.isLocal {
            return "本机 · \(device.osVersion)"
        }
        guard device.isOnline else {
            return device.lastSeen ?? "已断开"
        }
        return "\(device.osVersion) · DirectLink · 已连接"
    }

    // MARK: - 快捷入口

    private var quickLinks: some View {
        HStack(spacing: FGSpacing.s2) {
            quickLink("键位映射", icon: "keyboard", panel: .keymap)
            quickLink("网络延迟", icon: "activity", panel: .latency)
            quickLink("剪贴板", icon: "clipboard", panel: .clipboard)
            quickLink("文件传输", icon: "file", panel: .files)
            Spacer(minLength: 0)
        }
    }

    private func quickLink(_ title: String, icon: String, panel: FGPanelID) -> some View {
        FGButton(title, icon: icon, kind: .secondary, small: true) {
            model.openPanel(panel)
        }
    }
}

/// 列表中的轻量空态提示（非整页空态）。
struct FGEmptyHint: View {
    let text: String

    init(_ text: String) {
        self.text = text
    }

    var body: some View {
        HStack(spacing: FGSpacing.s1) {
            FGIcon("info", size: 12, color: FGColor.text3)
            Text(text)
                .font(.system(size: 11))
                .foregroundStyle(FGColor.text3)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, FGSpacing.s2)
    }
}
