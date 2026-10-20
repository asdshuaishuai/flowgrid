import FlowGridCore
import SwiftUI

/// 键位映射（520px 浮板）。结构对应 keymap.html；
/// 全局行由真实 KeyMapTable（示例 YAML 经 GoKeyMapRows）驱动。
public struct KeyMapView: View {
    @Bindable private var model: AppModel
    private let onClose: () -> Void

    public init(model: AppModel, onClose: @escaping () -> Void) {
        self._model = Bindable(model)
        self.onClose = onClose
    }

    public var body: some View {
        FGPanel(title: "键位映射", onClose: onClose) {
            headerRow
            globalSection
            overridesSection
            FGBanner("「Win → ⌥」与系统聚焦快捷键冲突，仅在非全屏窗口下生效。", kind: .warn)
            advancedSection
        } footer: {
            FGPanelFooter {
                FGFooterNote(icon: "info", text: "规则按从上到下顺序匹配")
            } trailing: {
                HStack(spacing: FGSpacing.s3) {
                    FGButton("恢复默认", kind: .secondary, small: true) {
                        model.reloadKeyMap()
                    }
                    FGButton("保存", small: true) {
                        model.toast.show("映射已保存（\(model.ruleCount) 条生效）", kind: .ok)
                    }
                }
            }
        }
        .fgToastHost(model.toast)
    }

    // MARK: - 头部

    private var headerRow: some View {
        HStack(spacing: FGSpacing.s3) {
            HStack(spacing: FGSpacing.s3) {
                FGBadge("\(model.ruleCount) 条生效", kind: .accent)
                Text("按应用自动切换")
                    .font(.system(size: 11))
                    .foregroundStyle(FGColor.text3)
            }
            Spacer(minLength: 0)
            FGButton("新增规则", icon: "plus") {
                model.toast.show("已新增映射规则", kind: .ok)
            }
        }
    }

    // MARK: - 全局

    private var globalSection: some View {
        FGSection("全局", icon: "keyboard", badge: (text: "\(model.ruleCount) 条", kind: .accent)) {
            VStack(spacing: 0) {
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), alignment: .leading), count: 3), spacing: FGSpacing.s2) {
                    ForEach(model.keyMapRows.filter { !$0.isGhost }) { row in
                        FGKeyPairCell(row)
                    }
                    GhostKeyPairCell {
                        model.toast.show("已新增映射规则", kind: .ok)
                    }
                }
                .padding(FGSpacing.s3)
            }
        }
    }

    private struct GhostKeyPairCell: View {
        let action: () -> Void
        @State private var hovering = false

        var body: some View {
            Button(action: action) {
                VStack(alignment: .leading, spacing: 5) {
                    HStack(spacing: 5) {
                        FGKeyCap("未设置", ghost: true)
                        FGIcon("arrowRight", size: 14, color: FGColor.text3)
                        FGKeyCap("未设置", ghost: true)
                    }
                    Text("点击添加")
                        .font(.system(size: 11))
                        .foregroundStyle(FGColor.text3)
                }
                .padding(FGSpacing.s2)
                .background(hovering ? FGColor.surfaceHi : Color.clear)
                .clipShape(RoundedRectangle(cornerRadius: FGRadius.control, style: .continuous))
            }
            .buttonStyle(.plain)
            .onHover { hovering = $0 }
        }
    }

    // MARK: - 应用覆盖

    private var overridesSection: some View {
        FGSection("应用覆盖", icon: "monitor", badge: (text: "\(model.appOverrides.count) 条", kind: .neutral)) {
            VStack(spacing: 0) {
                ForEach($model.appOverrides) { $override in
                    HStack(spacing: FGSpacing.s3) {
                        VStack(alignment: .leading, spacing: 2) {
                            HStack(spacing: FGSpacing.s2) {
                                FGIcon(override.id == "game" ? "play" : "code", size: 16, color: FGColor.text2)
                                Text(override.appName)
                                    .font(.system(size: 12))
                                    .foregroundStyle(FGColor.text1)
                            }
                            Text(override.note)
                                .font(.system(size: 11))
                                .foregroundStyle(FGColor.text3)
                        }
                        Spacer(minLength: 0)
                        HStack(spacing: FGSpacing.s3) {
                            FGSwitch(isOn: $override.enabled)
                            FGIconButton("edit", help: "编辑覆盖规则") {
                                model.toast.show("已进入编辑态")
                            }
                        }
                    }
                    .padding(.vertical, 10)
                    .padding(.horizontal, FGSpacing.s3)
                    .overlay(alignment: .bottom) {
                        Rectangle().fill(FGColor.hairline).frame(height: 1)
                    }
                }
            }
        }
    }

    // MARK: - 高级（折叠，初始收起）

    private var advancedSection: some View {
        FGSection("高级", icon: "settings", badge: (text: "2 项", kind: .neutral), initiallyExpanded: false) {
            VStack(spacing: 0) {
                HStack(spacing: FGSpacing.s3) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("按键重复率跟随目标端")
                            .font(.system(size: 12))
                            .foregroundStyle(FGColor.text1)
                        Text("使用被控设备的系统重复设置")
                            .font(.system(size: 11))
                            .foregroundStyle(FGColor.text3)
                    }
                    Spacer(minLength: 0)
                    FGSwitch(isOn: Binding(
                        get: { model.settings.keyRepeatFollowsTarget },
                        set: { _ in model.toggleSetting(\.keyRepeatFollowsTarget, label: "按键重复率跟随") }
                    ))
                }
                .padding(.vertical, 10)
                .padding(.horizontal, FGSpacing.s3)
                .overlay(alignment: .bottom) {
                    Rectangle().fill(FGColor.hairline).frame(height: 1)
                }

                HStack(spacing: FGSpacing.s3) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("发送原始扫描码")
                            .font(.system(size: 12))
                            .foregroundStyle(FGColor.text1)
                        Text("游戏与远程桌面场景更精确")
                            .font(.system(size: 11))
                            .foregroundStyle(FGColor.text3)
                    }
                    Spacer(minLength: 0)
                    FGSwitch(isOn: Binding(
                        get: { model.settings.sendRawScanCodes },
                        set: { _ in model.toggleSetting(\.sendRawScanCodes, label: "发送原始扫描码") }
                    ))
                }
                .padding(.vertical, 10)
                .padding(.horizontal, FGSpacing.s3)
            }
        }
    }
}
