import Foundation

/// 演示/样例数据：仅保留与真实功能并存的「历史样例」与「兜底资源」。
/// 设备、连接、传输记录一律来自真实链路（诚实优先）。
public enum DemoData {

    /// 剪贴板历史样例（真实链路无持久化前，用于呈现列表视觉与预览交互）。
    public static let clipboardHistory: [ClipboardItem] = [
        ClipboardItem(
            id: "clip-1", kind: .text,
            preview: "评审改到周二 15:00，会议室 3F 西侧",
            mime: "text/plain", sizeBytes: 62, ageSeconds: 720
        ),
        ClipboardItem(
            id: "clip-2", kind: .code,
            preview: "fn handle_frame(buf: &[u8]) -> Result<()>",
            mime: "text/plain", sizeBytes: 44, ageSeconds: 1_800
        ),
        ClipboardItem(
            id: "clip-3", kind: .link,
            preview: "https://example.internal/review/4821",
            mime: "text/uri-list", sizeBytes: 38, ageSeconds: 3_600
        ),
        ClipboardItem(
            id: "clip-4", kind: .image,
            preview: "screenshot-2026-10-16.png",
            mime: "image/png", sizeBytes: 41_984, ageSeconds: 7_200
        ),
    ]

    /// 剪贴板空态预览占位。
    public static let clipboardCurrent = ClipboardItem(
        id: "clip-current",
        kind: .text,
        preview: "（本机剪贴板为空）",
        mime: "text/plain",
        sizeBytes: 0,
        ageSeconds: 0
    )

    /// 键位映射兜底行（正常由 GoKeyMapRows 载入真 YAML 驱动）。
    public static let fallbackKeyMapRows: [KeyMappingRow] = [
        KeyMappingRow(id: "ctrl-cmd", fromKey: "Ctrl", toKey: "⌘", caption: "复制 / 粘贴"),
        KeyMappingRow(id: "win-opt", fromKey: "Win", toKey: "⌥", caption: "系统快捷键"),
        KeyMappingRow(id: "alt-ctrl", fromKey: "Alt", toKey: "Ctrl", caption: "终端应用"),
        KeyMappingRow(id: "passthru", fromKey: "Ctrl", toKey: "Ctrl", caption: "直通（不转换）"),
        KeyMappingRow(id: "cmd-ctrl", fromKey: "⌘", toKey: "Ctrl", caption: "反向（回切 Windows）"),
        KeyMappingRow(id: "unset", fromKey: "未设置", toKey: nil, caption: "点击添加"),
    ]

    public static let appOverrides: [AppOverrideRow] = [
        AppOverrideRow(id: "vscode", appName: "Visual Studio Code", note: "Ctrl 直通，禁用 ⌘ 转换", enabled: true),
        AppOverrideRow(id: "game", appName: "游戏（全屏时）", note: "停用全部映射，避免误触", enabled: true),
    ]

    /// 本机配对码默认值（可在连接设备屏刷新/修改）。
    public static let pairCodeDigits: [String] = ["8", "4", "2", "1"]
}
