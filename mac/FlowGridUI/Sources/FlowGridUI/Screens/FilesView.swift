import FlowGridCore
import SwiftUI

/// 文件传输（520px 浮板）。结构对应 files.html。
public struct FilesView: View {
    @Bindable private var model: AppModel
    private let onClose: () -> Void

    public init(model: AppModel, onClose: @escaping () -> Void) {
        self._model = Bindable(model)
        self.onClose = onClose
    }

    public var body: some View {
        FGPanel(
            title: "文件传输",
            onClose: onClose,
            rightSlot: AnyView(
                FGStatusPill("\(model.activeTransfers.count) 项进行中", dot: .ok, pulsing: !model.activeTransfers.isEmpty)
            )
        ) {
            activeList
            dropZone
            recordsList
            receiveSettingsSection
        } footer: {
            FGPanelFooter {
                FGFooterNote(icon: "shield", text: "SHA-256 校验后交付")
            } trailing: {
                HStack(spacing: FGSpacing.s3) {
                    FGButton("接收目录", icon: "folder", kind: .secondary, small: true) {
                        model.openReceiveDirectory()
                    }
                    FGButton("关闭", kind: .ghost, small: true) {
                        onClose()
                    }
                }
            }
        }
        .fgToastHost(model.toast)
    }

    // MARK: - 进行中

    private var activeList: some View {
        Group {
            if model.activeTransfers.isEmpty {
                FGListCard {
                    FGListGroupTitle("进行中")
                    FGEmptyState(icon: "check", title: "没有进行中的传输", desc: "拖入文件即可发送（单文件 1 B – 16 MiB）")
                }
            } else {
                FGListCard {
                    FGListGroupTitle("进行中")
                    ForEach(model.activeTransfers) { item in
                        FGTransferRow(item: item)
                    }
                }
            }
        }
    }

    private var dropZone: some View {
        FGDropZone(
            hint: "单文件 1 B – 16 MiB · 文件名不含路径分隔符",
            onDropFiles: { urls in
                if let url = urls.first {
                    model.sendFile(at: url)
                }
            }
        ) {
            model.chooseAndSendFile()
        }
    }

    // MARK: - 传输记录

    private var recordsList: some View {
        FGListCard {
            FGListGroupTitle("传输记录")
            if model.transferRecords.isEmpty {
                FGEmptyState(icon: "file", title: "暂无传输记录", desc: "完成的发送与接收会保留在这里")
            } else {
                ForEach(model.transferRecords) { item in
                    FGTransferRow(item: item)
                }
            }
        }
    }

    // MARK: - 接收设置

    private var receiveSettingsSection: some View {
        FGSection("接收设置", icon: "settings", badge: (text: "自动接收已开", kind: .neutral)) {
            VStack(spacing: FGSpacing.s3) {
                toggleRow("自动接收来自已配对设备的文件", keyPath: \.fileAutoAccept, label: "自动接收")
                toggleRow("收到前先询问", keyPath: \.fileAskBeforeReceive, label: "收到前先询问")
                HStack(spacing: FGSpacing.s3) {
                    Text("接收目录")
                        .font(.system(size: 12))
                        .foregroundStyle(FGColor.text1)
                    Spacer(minLength: 0)
                    HStack(spacing: FGSpacing.s2) {
                        FGInput("", text: Binding(
                            get: { model.settings.receiveDirectory },
                            set: { model.updateReceiveDirectory($0) }
                        ), width: 190)
                        FGButton("更改", icon: "folder", kind: .secondary, small: true) {
                            model.chooseReceiveDirectory()
                        }
                    }
                }
                toggleRow("清空传输记录", keyPath: \.clearRecordsOnQuit, label: "退出时清空记录")
                FGBanner(
                    "单文件 1 B – 16 MiB，分块 249 B；提议 30 秒内未确认即失效，传输中 10 秒无进展判失败；校验通过前不写入目标路径；同一方向同时仅允许一路。",
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

/// 带卡片外框的列表容器（列表组标题 + 行，无内边距）。
struct FGListCard<Content: View>: View {
    private let content: Content

    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }

    var body: some View {
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
