import FlowGridCore
import SwiftUI

/// 剪贴板（520px 浮板）。结构对应 clipboard.html。
public struct ClipboardView: View {
    @Bindable private var model: AppModel
    private let onClose: () -> Void

    public init(model: AppModel, onClose: @escaping () -> Void) {
        self._model = Bindable(model)
        self.onClose = onClose
    }

    public var body: some View {
        FGPanel(
            title: "剪贴板",
            onClose: onClose,
            rightSlot: AnyView(FGStatusPill("同步中", dot: .ok, pulsing: true))
        ) {
            previewCard
            filterRow
            historyList
            settingsSection
        } footer: {
            FGPanelFooter {
                FGFooterNote(icon: "shield", text: "端到端加密 · 不落盘")
            } trailing: {
                HStack(spacing: FGSpacing.s3) {
                    FGButton("清空历史", icon: "trash", kind: .secondary, small: true) {
                        model.clearClipboardHistory()
                    }
                    FGButton("关闭", kind: .ghost, small: true) {
                        onClose()
                    }
                }
            }
        }
        .fgToastHost(model.toast)
    }

    // MARK: - 当前剪贴板预览

    private var previewCard: some View {
        let item = model.clipboardCurrent
        return FGPreviewCard {
            HStack(spacing: FGSpacing.s3) {
                HStack(spacing: FGSpacing.s3) {
                    FGIcon(item.kind.iconName, size: 12, color: FGColor.text2)
                        .frame(width: 18, height: 18)
                        .background(FGColor.surface3)
                        .clipShape(RoundedRectangle(cornerRadius: 4, style: .continuous))
                    Text("当前剪贴板")
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(FGColor.text1)
                    FGBadge(item.mime, kind: .neutral)
                }
                Spacer(minLength: 0)
                if let from = item.fromDevice {
                    HStack(spacing: FGSpacing.s2) {
                        FGIcon("laptop", size: 14, color: FGColor.text3)
                        Text("来自 \(from)")
                            .font(.system(size: 11))
                            .foregroundStyle(FGColor.text3)
                    }
                }
            }
        } body: {
            Text(item.preview)
                .font(.system(size: 12, design: .monospaced))
                .foregroundStyle(FGColor.text1)
                .textSelection(.enabled)
        } foot: {
            HStack(spacing: FGSpacing.s3) {
                HStack(spacing: FGSpacing.s1) {
                    FGIcon("info", size: 14, color: FGColor.text3)
                    Text(frameDescription(item))
                        .font(.system(size: 11))
                        .foregroundStyle(FGColor.text3)
                }
                Spacer(minLength: 0)
                HStack(spacing: FGSpacing.s2) {
                    FGButton("写入本地", icon: "copy", kind: .ghost, small: true) {
                        model.copyToLocal(item)
                    }
                    FGButton("推送", icon: "arrowRight", kind: .secondary, small: true) {
                        model.pushClipboard()
                    }
                }
            }
        }
    }

    private func frameDescription(_ item: ClipboardItem) -> String {
        if item.isSingleFrame {
            return "\(item.sizeText) · 单帧发送 · 未分段"
        }
        return "\(item.sizeText) · 分段发送 · \(item.segmentCount) 段"
    }

    // MARK: - 过滤 + 搜索

    private var filterRow: some View {
        HStack(spacing: FGSpacing.s3) {
            FGSegmented(
                options: ClipboardFilter.allCases.map { ($0, $0.displayName) },
                selection: $model.clipboardFilter
            )
            Spacer(minLength: 0)
            FGInput("搜索历史", text: $model.clipboardSearch, width: 150)
        }
    }

    // MARK: - 历史列表

    private var historyList: some View {
        VStack(spacing: 0) {
            FGListGroupTitle("同步历史 · 保留最近 \(model.settings.clipboardHistoryLimit.rawValue) 条")
            ForEach(model.filteredClipboard) { item in
                historyRow(item)
            }
        }
        .background(FGColor.surface)
        .clipShape(RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: FGRadius.card, style: .continuous)
                .strokeBorder(FGColor.line2, lineWidth: 1)
        )
    }

    private func historyRow(_ item: ClipboardItem) -> some View {
        HStack(spacing: FGSpacing.s3) {
            HStack(spacing: FGSpacing.s3) {
                FGIcon(item.oversize ? "warn" : item.kind.iconName, size: 12, color: item.oversize ? FGColor.warnText : FGColor.text2)
                    .frame(width: 18, height: 18)
                    .background(item.oversize ? FGColor.warnSoft : FGColor.surface3)
                    .clipShape(RoundedRectangle(cornerRadius: 4, style: .continuous))
                VStack(alignment: .leading, spacing: 2) {
                    Text(item.preview)
                        .font(.system(size: 12))
                        .foregroundStyle(FGColor.text1)
                        .lineLimit(1)
                        .truncationMode(.middle)
                    Text(hintText(item))
                        .font(.system(size: 11))
                        .foregroundStyle(FGColor.text3)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            HStack(spacing: FGSpacing.s2) {
                if item.oversize {
                    FGBadge("超限", kind: .warn)
                }
                Text(item.ageSeconds.ageText)
                    .font(.system(size: 11))
                    .monospacedDigit()
                    .foregroundStyle(FGColor.text3)
                if !item.oversize {
                    FGIconButton("copy", size: 14, help: "写入本地剪贴板") {
                        model.copyToLocal(item)
                    }
                }
            }
        }
        .padding(.vertical, 10)
        .padding(.horizontal, FGSpacing.s3)
        .overlay(alignment: .bottom) {
            Rectangle().fill(FGColor.hairline).frame(height: 1)
        }
    }

    private func hintText(_ item: ClipboardItem) -> String {
        if item.oversize {
            return "\(item.sizeText) · 超出单条上限，未同步"
        }
        var parts: [String] = []
        if let from = item.fromDevice {
            parts.append("\(from) → 本机")
        }
        parts.append(item.sizeText)
        parts.append(item.mime)
        if !item.isSingleFrame {
            parts.append("\(item.segmentCount) 段")
        }
        return parts.joined(separator: " · ")
    }

    // MARK: - 同步设置

    private var settingsSection: some View {
        FGSection("同步设置", icon: "settings", badge: (text: "4 项", kind: .neutral)) {
            VStack(spacing: FGSpacing.s3) {
                toggleRow("跨端同步", keyPath: \.clipboardSync, label: "跨端同步")
                toggleRow("同步图片", keyPath: \.clipboardSyncImages, label: "同步图片")
                toggleRow("忽略密码管理器内容", keyPath: \.ignorePasswordManagers, label: "忽略密码管理器内容")
                toggleRow("接收后自动写入本地", keyPath: \.autoWriteLocal, label: "接收后自动写入本地")
                FGBanner(
                    "单条上限 63,240 B（协议 §5.3 分段上限），超出不同步；5 秒内未收齐则整份丢弃；同一时刻仅允许一路传输。",
                    kind: .neutral
                )
            }
            .padding(FGSpacing.s3)
        }
    }

    private func toggleRow(_ title: String, keyPath: WritableKeyPath<AppSettings, Bool>, label: String) -> some View {
        HStack(spacing: FGSpacing.s3) {
            Text(title)
                .font(.system(size: 12))
                .foregroundStyle(FGColor.text1)
            Spacer(minLength: 0)
            FGSwitch(isOn: Binding(
                get: { model.settings[keyPath: keyPath] },
                set: { _ in model.toggleSetting(keyPath, label: label) }
            ))
        }
    }
}
