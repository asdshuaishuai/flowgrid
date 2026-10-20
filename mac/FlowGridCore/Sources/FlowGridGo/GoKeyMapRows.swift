import CFlowGrid
import FlowGridCore
import Foundation

/// 键位映射屏的数据来源：把示例 keymap.yaml 经 `fg_keymap_load_file` 载入
/// 真 `KeyMapTable`，用一组探针走 `Lookup` 得到每行「源 → 目标」字形。
public enum GoKeyMapRows {

    public struct Probe: Sendable {
        public let id: String
        public let from: FGPlatform
        public let to: FGPlatform
        public let fromKey: UInt16
        public let caption: String
    }

    /// 与设计稿「全局」区 5 行对应（第 6 格 ghost 由 UI 静态渲染）。
    public static let defaultProbes: [Probe] = [
        Probe(id: "ctrl-cmd", from: .windows, to: .macOS, fromKey: 0xE0, caption: "复制 / 粘贴"),
        Probe(id: "win-opt", from: .windows, to: .macOS, fromKey: 0xE3, caption: "系统快捷键"),
        Probe(id: "alt-ctrl", from: .windows, to: .macOS, fromKey: 0xE2, caption: "终端应用"),
        Probe(id: "passthru", from: .linux, to: .macOS, fromKey: 0xE0, caption: "直通（不转换）"),
        Probe(id: "cmd-ctrl", from: .macOS, to: .windows, fromKey: 0xE3, caption: "反向（回切 Windows）"),
    ]

    /// 载入 YAML 并生成展示行；表内无匹配的探针渲染为 ghost（未设置）。
    public static func load(path: String, probes: [Probe] = defaultProbes) throws -> [KeyMappingRow] {
        let table = try GoKeyMap(contentsOfFile: path)
        return probes.map { probe in
            let sourceLabel = ModifierGlyph.sourceLabel(forSource: probe.from, keyCode: probe.fromKey)
            let result = try? table.lookup(from: probe.from, to: probe.to, keyCode: probe.fromKey, modifiers: 0)
            guard let result, result.found,
                  let targetLabel = ModifierGlyph.label(forTarget: probe.to, keyCode: result.keyCode)
            else {
                return KeyMappingRow(id: probe.id, fromKey: sourceLabel, toKey: nil, caption: probe.caption)
            }
            return KeyMappingRow(id: probe.id, fromKey: sourceLabel, toKey: targetLabel, caption: probe.caption)
        }
    }
}
