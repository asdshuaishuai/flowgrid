import FlowGridCore
import SwiftUI

/// 网络延迟（520px 浮板）。结构对应 latency.html。
public struct LatencyView: View {
    @Bindable private var model: AppModel
    private let onClose: () -> Void

    public init(model: AppModel, onClose: @escaping () -> Void) {
        self._model = Bindable(model)
        self.onClose = onClose
    }

    public var body: some View {
        FGPanel(
            title: "网络延迟",
            onClose: onClose,
            rightSlot: AnyView(FGStatusPill(currentLatencyText, dot: .ok, pulsing: true))
        ) {
            readout
            chartSection
            deviceList
            thresholdSection
        } footer: {
            FGPanelFooter {
                FGFooterNote(icon: "info", text: "每 2 秒采样一次")
            } trailing: {
                HStack(spacing: FGSpacing.s3) {
                    FGButton("导出诊断", icon: "copy", kind: .secondary, small: true) {
                        model.exportDiagnostics()
                    }
                    FGButton("关闭", kind: .ghost, small: true) {
                        onClose()
                    }
                }
            }
        }
        .fgToastHost(model.toast)
    }

    // MARK: - 大读数

    private var primary: (ms: Double?, tier: LatencyTier) {
        model.primaryLatency
    }

    private var currentLatencyText: String {
        primary.ms?.latencyText ?? "—"
    }

    private var readout: some View {
        HStack(alignment: .bottom) {
            VStack(alignment: .leading, spacing: 2) {
                Text("当前延迟")
                    .font(.system(size: 11, weight: .semibold))
                    .tracking(0.3)
                    .foregroundStyle(FGColor.text3)
                Text(currentLatencyText)
                    .font(.system(size: 28, weight: .bold, design: .monospaced))
                    .foregroundStyle(FGColor.tier(primary.tier).text)
                    .fgNumericTransition(value: primary.ms ?? 0)
                    .animation(FGMotion.toast, value: primary.tier)
            }
            Spacer(minLength: 0)
            VStack(alignment: .trailing, spacing: 2) {
                Text("链路")
                    .font(.system(size: 11))
                    .foregroundStyle(FGColor.text3)
                HStack(spacing: FGSpacing.s2) {
                    FGIcon(primaryLinkIcon, size: 16, color: FGColor.text1)
                    Text(primaryLinkName)
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(FGColor.text1)
                }
            }
        }
    }

    private var primaryLinkName: String {
        model.connectionStatus.isConnected ? "DirectLink" : "未连接"
    }

    private var primaryLinkIcon: String {
        model.connectionStatus.isConnected ? "lan" : "wifi"
    }

    // MARK: - 采样柱图

    private var chartSection: some View {
        let deviceID = model.primaryLinkDevice?.id
        let meanText = deviceID.flatMap { model.meanLatencyText(for: $0) }.map { "均值 \($0)" } ?? "均值 —"
        return FGSection("近 60 秒采样", icon: "activity", badge: (text: meanText, kind: .ok)) {
            VStack(spacing: 0) {
                FGLatencyChart(
                    samples: deviceID.map { model.latencyStore.chartSamples($0) } ?? [],
                    axisStart: "-60s"
                )
                if deviceID == nil {
                    FGEmptyHint("连接设备后开始采样")
                }
            }
        }
    }

    // MARK: - 分设备列表

    private var deviceList: some View {
        VStack(spacing: 0) {
            FGListGroupTitle("分设备")
            ForEach(model.devices) { device in
                HStack(spacing: FGSpacing.s3) {
                    VStack(alignment: .leading, spacing: 2) {
                        HStack(spacing: FGSpacing.s2) {
                            FGIcon(iconName(for: device), size: 16, color: FGColor.text2)
                            Text(device.name)
                                .font(.system(size: 12))
                                .foregroundStyle(FGColor.text1)
                        }
                        Text(hint(for: device))
                            .font(.system(size: 11))
                            .foregroundStyle(FGColor.text3)
                    }
                    Spacer(minLength: 0)
                    let latency = model.latency(for: device)
                    FGLatPill(ms: latency.ms, tier: latency.tier)
                }
                .padding(.vertical, 10)
                .padding(.horizontal, FGSpacing.s3)
                .overlay(alignment: .bottom) {
                    Rectangle().fill(FGColor.hairline).frame(height: 1)
                }
            }
        }
        .background(FGColor.surface)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous)
                .strokeBorder(FGColor.line2, lineWidth: 1)
        )
    }

    private func hint(for device: Device) -> String {
        if device.isLocal { return "本机" }
        guard device.isOnline else { return "离线" }
        return "\(device.osVersion) · DirectLink · 已连接"
    }

    private func iconName(for device: Device) -> String {
        switch device.os {
        case .macOS: "laptop"
        case .windows, .linux: "monitor"
        case .deepin: "tablet"
        }
    }

    // MARK: - 告警阈值（折叠，初始收起）

    private var thresholdSection: some View {
        FGSection(
            "告警阈值",
            icon: "settings",
            badge: (text: "\(Int(model.settings.latencyAlertThresholdMs))ms", kind: .neutral),
            initiallyExpanded: false
        ) {
            VStack(spacing: FGSpacing.s3) {
                HStack(spacing: FGSpacing.s3) {
                    Text("告警阈值")
                        .font(.system(size: 12))
                        .foregroundStyle(FGColor.text1)
                    Spacer(minLength: 0)
                    HStack(spacing: FGSpacing.s3) {
                        FGNumberInput(value: Binding(
                            get: { Int(model.settings.latencyAlertThresholdMs) },
                            set: {
                                model.settings.latencyAlertThresholdMs = Double($0)
                                model.applyLatencyAlertSettings()
                            }
                        ))
                        Text("ms")
                            .font(.system(size: 11))
                            .foregroundStyle(FGColor.text3)
                    }
                }
                HStack(spacing: FGSpacing.s3) {
                    Text("连续超阈值次数")
                        .font(.system(size: 12))
                        .foregroundStyle(FGColor.text1)
                    Spacer(minLength: 0)
                    HStack(spacing: FGSpacing.s3) {
                        FGNumberInput(value: Binding(
                            get: { model.settings.latencyAlertConsecutive },
                            set: {
                                model.settings.latencyAlertConsecutive = $0
                                model.applyLatencyAlertSettings()
                            }
                        ))
                        Text("次")
                            .font(.system(size: 11))
                            .foregroundStyle(FGColor.text3)
                    }
                }
                HStack(spacing: FGSpacing.s3) {
                    Text("超阈值时自动切换链路")
                        .font(.system(size: 12))
                        .foregroundStyle(FGColor.text1)
                    Spacer(minLength: 0)
                    FGSwitch(isOn: Binding(
                        get: { model.settings.autoSwitchOnAlert },
                        set: { _ in model.toggleSetting(\.autoSwitchOnAlert, label: "自动切换链路") }
                    ))
                }
            }
            .padding(FGSpacing.s3)
        }
    }
}
