import Foundation

/// 设置持久化：`~/Library/Application Support/FlowGrid/settings.yaml`（PROTOCOL §13.2）。
/// 扁平 key: value 的极简 YAML（无第三方依赖）；解析失败按默认值回落，不阻塞启动。
public struct SettingsStore: Sendable {
    public let url: URL

    public init(url: URL? = nil) {
        if let url {
            self.url = url
        } else {
            let base = FileManager.default.homeDirectoryForCurrentUser
                .appendingPathComponent("Library/Application Support/FlowGrid", isDirectory: true)
            self.url = base.appendingPathComponent("settings.yaml")
        }
    }

    public func load() -> AppSettings {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else {
            return AppSettings()
        }
        var settings = AppSettings()
        for line in text.split(separator: "\n") {
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            guard !trimmed.isEmpty, !trimmed.hasPrefix("#"), let colon = trimmed.firstIndex(of: ":") else { continue }
            let key = trimmed[..<colon].trimmingCharacters(in: .whitespaces)
            let value = trimmed[trimmed.index(after: colon)...].trimmingCharacters(in: .whitespaces)
            settings.apply(key: key, value: value)
        }
        return settings
    }

    public func save(_ settings: AppSettings) {
        try? FileManager.default.createDirectory(
            at: url.deletingLastPathComponent(), withIntermediateDirectories: true
        )
        try? settings.yamlText.write(to: url, atomically: true, encoding: .utf8)
    }
}

extension AppSettings {
    /// 扁平 YAML 文本（字符串值加引号，避免特殊字符破坏解析）。
    var yamlText: String {
        var lines = ["# FlowGrid settings（自动生成，可手工编辑）"]
        lines.append("preferredLink: \(preferredLink.rawValue)")
        lines.append("autoRoute: \(autoRoute)")
        lines.append("launchAtLogin: \(launchAtLogin)")
        lines.append("keymapEnabled: \(keymapEnabled)")
        lines.append("mouseSmoothing: \(mouseSmoothing)")
        lines.append("edgeSwitchThresholdPx: \(edgeSwitchThresholdPx)")
        lines.append("keyRepeatFollowsTarget: \(keyRepeatFollowsTarget)")
        lines.append("sendRawScanCodes: \(sendRawScanCodes)")
        lines.append("clipboardSync: \(clipboardSync)")
        lines.append("clipboardSyncImages: \(clipboardSyncImages)")
        lines.append("ignorePasswordManagers: \(ignorePasswordManagers)")
        lines.append("autoWriteLocal: \(autoWriteLocal)")
        lines.append("clipboardHistoryLimit: \(clipboardHistoryLimit.rawValue)")
        lines.append("excludedAppsCount: \(excludedAppsCount)")
        lines.append("fileAutoAccept: \(fileAutoAccept)")
        lines.append("fileAskBeforeReceive: \(fileAskBeforeReceive)")
        lines.append("clearRecordsOnQuit: \(clearRecordsOnQuit)")
        lines.append("receiveDirectory: \"\(receiveDirectory)\"")
        lines.append("requireConfirmNewDevice: \(requireConfirmNewDevice)")
        lines.append("latencyAlertThresholdMs: \(latencyAlertThresholdMs)")
        lines.append("latencyAlertConsecutive: \(latencyAlertConsecutive)")
        lines.append("autoSwitchOnAlert: \(autoSwitchOnAlert)")
        lines.append("localPairingCode: \"\(localPairingCode)\"")
        return lines.joined(separator: "\n") + "\n"
    }

    mutating func apply(key: String, value rawValue: String) {
        let value = rawValue.trimmingCharacters(in: CharacterSet(charactersIn: "\"'"))
        switch key {
        case "preferredLink":
            if let parsed = PreferredLink(rawValue: value) { preferredLink = parsed }
        case "autoRoute": autoRoute = value == "true"
        case "launchAtLogin": launchAtLogin = value == "true"
        case "keymapEnabled": keymapEnabled = value == "true"
        case "mouseSmoothing": mouseSmoothing = value == "true"
        case "edgeSwitchThresholdPx":
            if let parsed = Int(value) { edgeSwitchThresholdPx = parsed }
        case "keyRepeatFollowsTarget": keyRepeatFollowsTarget = value == "true"
        case "sendRawScanCodes": sendRawScanCodes = value == "true"
        case "clipboardSync": clipboardSync = value == "true"
        case "clipboardSyncImages": clipboardSyncImages = value == "true"
        case "ignorePasswordManagers": ignorePasswordManagers = value == "true"
        case "autoWriteLocal": autoWriteLocal = value == "true"
        case "clipboardHistoryLimit":
            if let parsed = Int(value), let limit = ClipboardHistoryLimit(rawValue: parsed) {
                clipboardHistoryLimit = limit
            }
        case "excludedAppsCount":
            if let parsed = Int(value) { excludedAppsCount = parsed }
        case "fileAutoAccept": fileAutoAccept = value == "true"
        case "fileAskBeforeReceive": fileAskBeforeReceive = value == "true"
        case "clearRecordsOnQuit": clearRecordsOnQuit = value == "true"
        case "receiveDirectory":
            if !value.isEmpty { receiveDirectory = value }
        case "requireConfirmNewDevice": requireConfirmNewDevice = value == "true"
        case "latencyAlertThresholdMs":
            if let parsed = Double(value) { latencyAlertThresholdMs = parsed }
        case "latencyAlertConsecutive":
            if let parsed = Int(value) { latencyAlertConsecutive = parsed }
        case "autoSwitchOnAlert": autoSwitchOnAlert = value == "true"
        case "localPairingCode":
            let digits = value.filter(\.isNumber)
            if digits.count == 4 { localPairingCode = digits }
        default:
            break
        }
    }
}
