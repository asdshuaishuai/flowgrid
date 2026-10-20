import FlowGridCore
import SwiftUI

/// 设置（520px 浮板）。结构对应 settings.html；
/// 编辑走本地草稿（取消丢弃 / 保存写回）。
public struct SettingsView: View {
    @Bindable private var model: AppModel
    private let onClose: () -> Void
    @State private var draft: AppSettings

    public init(model: AppModel, onClose: @escaping () -> Void) {
        self._model = Bindable(model)
        self.onClose = onClose
        self._draft = State(initialValue: model.settings)
    }

    public var body: some View {
        FGPanel(title: "设置", onClose: onClose) {
            FGBanner("设置即时生效并同步到全部已配对设备，无需重启。", kind: .accent)
            connectionList
            inputList
            clipboardList
            securityList
            FGBanner("关闭「新设备需确认」会让同一网络内的任意设备发起配对请求，仅在可信网络中临时使用。", kind: .warn)
        } footer: {
            FGPanelFooter {
                FGButton("恢复默认", kind: .ghost, small: true) {
                    draft = AppSettings()
                    model.toast.show("已恢复默认设置", kind: .ok)
                }
            } trailing: {
                HStack(spacing: FGSpacing.s3) {
                    FGButton("取消", kind: .secondary, small: true) {
                        onClose()
                    }
                    FGButton("保存", small: true) {
                        let previous = model.settings
                        let directoryChanged = draft.receiveDirectory != previous.receiveDirectory
                        model.settings = draft
                        if directoryChanged {
                            model.updateReceiveDirectory(draft.receiveDirectory)
                        }
                        if draft.launchAtLogin != previous.launchAtLogin {
                            model.setLaunchAtLogin(draft.launchAtLogin)
                        }
                        model.toast.show("设置已保存", kind: .ok)
                    }
                }
            }
        }
        .fgToastHost(model.toast)
    }

    // MARK: - 连接

    private var connectionList: some View {
        FGListCard {
            FGListGroupTitle("连接")
            FGListRow("优先链路", hint: "智能链路（BLE/WiFi Direct）阶段启用；当前为 DirectLink") {
                FGSelect(
                    options: PreferredLink.allCases.map { ($0, $0.rawValue) },
                    selection: $draft.preferredLink
                )
            }
            FGListRow("自动路由", hint: "多链路阶段启用") {
                draftSwitch(\.autoRoute, label: "自动路由")
            }
            FGListRow("开机自启", hint: "登录后托盘常驻") {
                draftSwitch(\.launchAtLogin, label: "开机自启")
            }
        }
    }

    // MARK: - 输入

    private var inputList: some View {
        FGListCard {
            FGListGroupTitle("输入")
            FGListRow("键位映射", hint: "跨平台修饰键转换") {
                FGButton("编辑", kind: .secondary, small: true) {
                    model.openPanel(.keymap)
                }
            }
            FGListRow("鼠标平滑", hint: "高 DPI 平滑（HAL 阶段接入）") {
                draftSwitch(\.mouseSmoothing, label: "鼠标平滑")
            }
            FGListRow("边缘切换阈值", hint: "指针抵达屏幕边缘后触发") {
                HStack(spacing: FGSpacing.s3) {
                    FGNumberInput(value: $draft.edgeSwitchThresholdPx)
                    Text("px")
                        .font(.system(size: 11))
                        .foregroundStyle(FGColor.text3)
                }
            }
            FGListRow("切换热键", hint: "按住后按数字键直切") {
                FGKeyCap("⌘")
            }
        }
    }

    // MARK: - 剪贴板

    private var clipboardList: some View {
        FGListCard {
            FGListGroupTitle("剪贴板")
            FGListRow("跨端同步", hint: "文本与图片，单条上限 63,240 B") {
                draftSwitch(\.clipboardSync, label: "剪贴板同步")
            }
            FGListRow("历史条数", hint: "超出后按时间淘汰") {
                FGSelect(
                    options: ClipboardHistoryLimit.allCases.map { ($0, $0.displayText) },
                    selection: $draft.clipboardHistoryLimit
                )
            }
            FGListRow("排除应用", hint: "密码管理器等窗口内不采集") {
                FGButton("3 个", kind: .secondary, small: true) {
                    model.toast.show("已打开排除列表")
                }
            }
        }
    }

    // MARK: - 安全

    private var securityList: some View {
        FGListCard {
            FGListGroupTitle("安全")
            FGListRow("端到端加密", hint: "DTLS 1.3 · AES-256-GCM") {
                FGBadge("已启用", kind: .ok)
            }
            FGListRow("新设备需确认", hint: "关闭后任何局域网设备可直接配对") {
                draftSwitch(\.requireConfirmNewDevice, label: "新设备确认")
            }
            FGListRow("已授权设备", hint: "可单独撤销信任") {
                FGButton("管理", kind: .secondary, small: true) {
                    model.toast.show("已打开授权管理")
                }
            }
        }
    }

    // MARK: - 草稿开关

    private func draftSwitch(_ keyPath: WritableKeyPath<AppSettings, Bool>, label: String) -> some View {
        FGSwitch(isOn: Binding(
            get: { draft[keyPath: keyPath] },
            set: { newValue in
                draft[keyPath: keyPath] = newValue
                model.toast.show("\(label)已\(newValue ? "开启" : "关闭")", kind: newValue ? .ok : .info)
            }
        ))
    }
}
