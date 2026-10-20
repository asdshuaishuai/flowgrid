import AppKit
import FlowGridCore
import SwiftUI

/// 连接设备：地址 + 配对码直连（DirectLink）；本机配对码供对端连接本机使用。
public struct PairingView: View {
    @Bindable private var model: AppModel
    private let onClose: () -> Void
    @State private var address = "192.168.1.50:24801"
    @State private var code = ""

    public init(model: AppModel, onClose: @escaping () -> Void) {
        self._model = Bindable(model)
        self.onClose = onClose
    }

    public var body: some View {
        FGPanel(title: "连接设备", onClose: onClose) {
            FGBanner(
                "在两台设备上使用相同的配对码即可建立 DirectLink 链路（TCP 24801，帧级 HMAC 校验）。",
                kind: .accent
            )
            connectCard
            localCodeCard
            FGBanner("自动发现（mDNS）将在后续版本提供；当前请通过 IP 地址连接。", kind: .neutral)
        } footer: {
            FGPanelFooter {
                EmptyView()
            } trailing: {
                FGButton("关闭", kind: .ghost, small: true) {
                    onClose()
                }
            }
        }
        .fgToastHost(model.toast)
    }

    // MARK: - 连接到对端

    private var connectCard: some View {
        VStack(alignment: .leading, spacing: FGSpacing.s3) {
            labeledField("设备地址") {
                FGInput("192.168.1.50:24801", text: $address)
            }
            labeledField("配对码") {
                FGInput("对端「本机配对码」（缺省使用本机码）", text: $code)
            }
            HStack(spacing: FGSpacing.s2) {
                FGButton(model.connectionStatus.isConnected ? "重新连接" : "连接") {
                    model.connectManually(address: address, code: code)
                }
                .disabled(model.connectionStatus.isBusy)
                if model.connectionStatus.isConnected {
                    FGButton("断开", kind: .secondary, small: true) {
                        model.disconnectPeer()
                    }
                }
                Spacer(minLength: 0)
                Text(model.connectionStatus.displayText)
                    .font(.system(size: 11))
                    .foregroundStyle(FGColor.text3)
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

    // MARK: - 本机配对码

    private var localCodeCard: some View {
        VStack(alignment: .leading, spacing: FGSpacing.s3) {
            HStack(spacing: FGSpacing.s2) {
                Text("本机配对码")
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundStyle(FGColor.text1)
                Spacer(minLength: 0)
                Text("对端连接本机时需输入此码")
                    .font(.system(size: 11))
                    .foregroundStyle(FGColor.text3)
            }
            FGPairCode(digits: model.pairCode)
            HStack(spacing: FGSpacing.s2) {
                FGButton("复制", icon: "copy", kind: .secondary, small: true) {
                    let pasteboard = NSPasteboard.general
                    pasteboard.clearContents()
                    pasteboard.setString(model.localPairingCode, forType: .string)
                    model.toast.show("配对码已复制", kind: .ok)
                }
                FGButton("刷新", icon: "refresh", kind: .secondary, small: true) {
                    model.refreshPairCode()
                }
                Spacer(minLength: 0)
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

    private func labeledField<Content: View>(_ label: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(label)
                .font(.system(size: 11, weight: .semibold))
                .foregroundStyle(FGColor.text2)
            content()
        }
    }
}
