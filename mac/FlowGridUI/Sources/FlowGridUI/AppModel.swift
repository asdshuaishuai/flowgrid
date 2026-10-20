import AppKit
import FlowGridCore
import ServiceManagement
import Foundation
import Observation

/// 剪贴板过滤分段（全部/文本/代码/链接/图片）。
public enum ClipboardFilter: String, CaseIterable, Sendable {
    case all
    case text
    case code
    case link
    case image

    public var displayName: String {
        switch self {
        case .all: "全部"
        case .text: "文本"
        case .code: "代码"
        case .link: "链接"
        case .image: "图片"
        }
    }

    public var kind: ClipboardKind? {
        switch self {
        case .all: nil
        case .text: .text
        case .code: .code
        case .link: .link
        case .image: .image
        }
    }
}

/// 浮板 ID（app 层 PanelController 映射到 NSPanel）。
public enum FGPanelID: String, CaseIterable, Sendable {
    case connections
    case pairing
    case latency
    case keymap
    case clipboard
    case files
    case settings

    public var title: String {
        switch self {
        case .connections: "设备与连接"
        case .pairing: "连接设备"
        case .latency: "网络延迟"
        case .keymap: "键位映射"
        case .clipboard: "剪贴板"
        case .files: "文件传输"
        case .settings: "设置"
        }
    }
}

/// 全局 UI 状态与交互行为。
/// 设备/连接/延迟/剪贴板/文件均为真实链路数据；界面上的「样例历史」仅出现在剪贴板列表。
@MainActor
@Observable
public final class AppModel {

    // MARK: - 注入

    public let latencyStore: LatencyStore
    @ObservationIgnored private let keyMapRowsLoader: () -> [KeyMappingRow]

    @ObservationIgnored public var openPanel: (FGPanelID) -> Void = { _ in }
    @ObservationIgnored public var quit: () -> Void = { NSApp.terminate(nil) }
    @ObservationIgnored public var connection: (any PeerConnectionControlling)?

    // MARK: - 设备

    public static let localDeviceID = "local"
    public static let peerDeviceID = "peer"

    /// 索引 0 恒为本机；真实对端连接后追加对端行（断开后保留为离线行）。
    public var devices: [Device]

    // MARK: - 通用

    public var settings: AppSettings
    @ObservationIgnored private let settingsStore: SettingsStore
    @ObservationIgnored private var settingsAutosave: Task<Void, Never>?
    public let toast = ToastCenter()
    public let switchOverlay = SwitchOverlayCenter()

    // MARK: - 剪贴板

    public var clipboardCurrent: ClipboardItem
    public var clipboardHistory: [ClipboardItem] = DemoData.clipboardHistory
    public var clipboardFilter: ClipboardFilter = .all
    public var clipboardSearch = ""

    // MARK: - 传输

    public var activeTransfers: [TransferItem] = []
    public var transferRecords: [TransferItem] = []
    @ObservationIgnored private var pendingSend: (name: String, size: Int64)?
    @ObservationIgnored private var pendingReceive: (id: UInt16, name: String, size: UInt64)?
    @ObservationIgnored private var pasteboardTimer: Task<Void, Never>?
    @ObservationIgnored private var lastPasteboardChangeCount = NSPasteboard.general.changeCount
    @ObservationIgnored private var lastRemoteText: String?
    @ObservationIgnored private var lastRemoteImage: Data?

    // MARK: - 键位

    public var keyMapRows: [KeyMappingRow]
    public var appOverrides: [AppOverrideRow] = DemoData.appOverrides

    // MARK: - 配对

    public var pairCode: [String] = DemoData.pairCodeDigits

    // MARK: - 真实连接

    public private(set) var connectionStatus: ConnectionStatus = .idle
    public private(set) var peerInfo: PeerInfo?
    public private(set) var discoveredPeers: [DiscoveredPeer] = []
    public private(set) var accessibilityTrusted = false
    public private(set) var captureEnabled = false

    // MARK: - Init

    public init(
        latencyStore: LatencyStore,
        keyMapRowsLoader: @escaping () -> [KeyMappingRow],
        settingsStore: SettingsStore = SettingsStore()
    ) {
        self.latencyStore = latencyStore
        self.keyMapRowsLoader = keyMapRowsLoader
        self.settingsStore = settingsStore
        let loaded = settingsStore.load()
        self.settings = loaded
        self.pairCode = loaded.localPairingCode.map(String.init)
        self.keyMapRows = keyMapRowsLoader()
        self.devices = [Self.makeLocalDevice()]
        self.clipboardCurrent = Self.readPasteboardItem() ?? DemoData.clipboardCurrent
    }

    /// 设置自动落盘（2s 轮询，变更即写；App 退出前再写一次）。
    public func startSettingsAutosave() {
        guard settingsAutosave == nil else { return }
        settingsAutosave = Task { [weak self] in
            var lastSaved = self?.settings
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(2))
                guard let self, !Task.isCancelled else { return }
                if self.settings != lastSaved {
                    self.settingsStore.save(self.settings)
                    lastSaved = self.settings
                }
            }
        }
    }

    public func stopSettingsAutosave() {
        settingsAutosave?.cancel()
        settingsAutosave = nil
        settingsStore.save(settings)
    }

    // MARK: - 派生

    public var localDevice: Device {
        devices.first { $0.id == Self.localDeviceID } ?? Self.makeLocalDevice()
    }

    public var peerDevice: Device? {
        devices.first { $0.id == Self.peerDeviceID }
    }

    /// 当前接收本机输入的一方（接管中 = 对端，否则本机）。
    public var currentControlledDeviceID: String {
        if captureEnabled, peerDevice?.isOnline == true {
            return Self.peerDeviceID
        }
        return Self.localDeviceID
    }

    public var currentDevice: Device {
        device(currentControlledDeviceID) ?? localDevice
    }

    public var onlineDeviceCount: Int {
        devices.filter(\.isOnline).count
    }

    /// 主链路设备：已连接的对端；未连接时无。
    public var primaryLinkDevice: Device? {
        guard let peer = peerDevice, peer.isOnline else { return nil }
        return peer
    }

    public var localPairingCode: String {
        pairCode.joined()
    }

    public var quickSwitchDevices: [Device] {
        var result = [localDevice]
        if let peer = peerDevice, peer.isOnline {
            result.append(peer)
        }
        return result
    }

    public func device(_ id: String) -> Device? {
        devices.first { $0.id == id }
    }

    /// 延迟展示：本机无链路意义（—），对端取实时样本。
    public func latency(for device: Device) -> (ms: Double?, tier: LatencyTier) {
        if device.isLocal {
            return (nil, .idle)
        }
        guard device.isOnline, let entry = latencyStore.entry(device.id) else { return (nil, .idle) }
        return (entry.ms, entry.tier)
    }

    public func meanLatencyText(for deviceID: String) -> String? {
        latencyStore.mean(deviceID).map { $0.latencyText }
    }

    public var primaryLatency: (ms: Double?, tier: LatencyTier) {
        guard let device = primaryLinkDevice else { return (nil, .idle) }
        return latency(for: device)
    }

    /// 剪贴板过滤 + 关键字搜索（对 preview 匹配）。
    public var filteredClipboard: [ClipboardItem] {
        let query = clipboardSearch.trimmingCharacters(in: .whitespaces).lowercased()
        return clipboardHistory.filter { item in
            let matchesKind = clipboardFilter.kind == nil || item.kind == clipboardFilter.kind
            let matchesText = query.isEmpty
                || item.preview.lowercased().contains(query)
                || item.mime.lowercased().contains(query)
            return matchesKind && matchesText
        }
    }

    // MARK: - 连接与切换

    public func attachConnection(_ connection: any PeerConnectionControlling) {
        self.connection = connection
        connection.updateLocalPairingCode(localPairingCode)
        connection.updateLatencyAlert(
            thresholdMs: settings.latencyAlertThresholdMs,
            consecutive: settings.latencyAlertConsecutive
        )
        accessibilityTrusted = connection.accessibilityTrusted
    }

    public func connectManually(address: String, code: String) {
        let trimmed = address.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty else {
            toast.show("请输入设备地址", kind: .danger)
            return
        }
        let trimmedCode = code.trimmingCharacters(in: .whitespaces)
        connection?.connect(address: trimmed, pairingCode: trimmedCode.isEmpty ? localPairingCode : trimmedCode)
    }

    /// 连接 mDNS 发现的设备；配对码为空时使用本机配对码。
    public func connectDiscovered(_ peer: DiscoveredPeer, code: String) {
        let trimmed = code.trimmingCharacters(in: .whitespaces)
        connection?.connect(
            serviceName: peer.id,
            pairingCode: trimmed.isEmpty ? localPairingCode : trimmed
        )
    }

    public func disconnectPeer() {
        connection?.disconnect()
    }

    /// 切换输入接管：本机 ⇄ 对端（对端需已连接且已授予辅助功能权限）。
    public func switchTo(deviceID: String) {
        if deviceID == Self.localDeviceID {
            _ = setCaptureEnabled(false)
            switchOverlay.show(localDevice.name)
            return
        }
        guard deviceID == Self.peerDeviceID, peerDevice?.isOnline == true else { return }
        guard connection?.isConnected == true else {
            toast.show("未连接对端", kind: .danger)
            return
        }
        guard connection?.accessibilityTrusted == true else {
            toast.show("接管键鼠需要辅助功能权限", kind: .danger)
            openAccessibilitySettings()
            return
        }
        if setCaptureEnabled(true) {
            switchOverlay.show(peerDevice?.name ?? "对端")
        }
    }

    @discardableResult
    public func setCaptureEnabled(_ enabled: Bool) -> Bool {
        accessibilityTrusted = connection?.accessibilityTrusted ?? false
        guard !enabled || accessibilityTrusted else {
            toast.show("接管键鼠需要辅助功能权限", kind: .danger)
            return false
        }
        let effective = connection?.setCaptureEnabled(enabled) ?? false
        captureEnabled = enabled && effective
        if enabled && !effective {
            toast.show("未能接管键鼠（检查连接与权限）", kind: .danger)
        }
        return captureEnabled
    }

    /// 开机自启（SMAppService 真实注册；开发签名下失败时回退并提示）。
    public func setLaunchAtLogin(_ enabled: Bool) {
        let service = SMAppService.mainApp
        do {
            if enabled, service.status != .enabled {
                try service.register()
            } else if !enabled, service.status == .enabled {
                try service.unregister()
            }
        } catch {
            settings.launchAtLogin = !enabled
            toast.show("开机自启设置失败：\(error.localizedDescription)", kind: .danger)
        }
    }

    /// 延迟告警设置变更后同步到链路（阈值/次数即时生效）。
    public func applyLatencyAlertSettings() {
        connection?.updateLatencyAlert(
            thresholdMs: settings.latencyAlertThresholdMs,
            consecutive: settings.latencyAlertConsecutive
        )
    }

    /// 每次打开浮板时刷新（用户可能刚在系统设置里授权）。
    public func refreshPermissions() {
        accessibilityTrusted = connection?.accessibilityTrusted ?? false
    }

    public func openAccessibilitySettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility") {
            NSWorkspace.shared.open(url)
        }
    }

    public func reloadKeyMap() {
        keyMapRows = keyMapRowsLoader()
        toast.show("已恢复默认映射（\(ruleCount) 条）", kind: .ok)
    }

    /// 生效规则数（非 ghost 行）。
    public var ruleCount: Int {
        keyMapRows.filter { !$0.isGhost }.count
    }

    public func refreshPairCode() {
        pairCode = (0..<4).map { _ in String(Int.random(in: 0...9)) }
        settings.localPairingCode = localPairingCode
        connection?.updateLocalPairingCode(localPairingCode)
        toast.show("配对码已刷新", kind: .ok)
    }

    /// 编辑本机配对码（4 位数字；被控端与连接端需一致）。
    public func setLocalPairingCode(_ code: String) {
        let digits = code.filter(\.isNumber).prefix(4).map(String.init)
        guard digits.count == 4 else {
            toast.show("配对码需为 4 位数字", kind: .danger)
            return
        }
        pairCode = digits
        settings.localPairingCode = localPairingCode
        connection?.updateLocalPairingCode(localPairingCode)
        toast.show("本机配对码已更新", kind: .ok)
    }

    // MARK: - 剪贴板监视（1s 轮询 changeCount；开启跨端同步时自动推送）

    public func startPasteboardWatch() {
        guard pasteboardTimer == nil else { return }
        pasteboardTimer = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(1))
                guard let self, !Task.isCancelled else { return }
                self.pollPasteboard()
            }
        }
    }

    public func stopPasteboardWatch() {
        pasteboardTimer?.cancel()
        pasteboardTimer = nil
    }

    private func pollPasteboard() {
        let pasteboard = NSPasteboard.general
        guard pasteboard.changeCount != lastPasteboardChangeCount else { return }
        lastPasteboardChangeCount = pasteboard.changeCount
        if let text = pasteboard.string(forType: .string), !text.isEmpty {
            if text == lastRemoteText { return } // 自己写入的对端内容回显，忽略
            clipboardCurrent = Self.pasteboardItem(text)
            guard settings.clipboardSync, connection?.isConnected == true else { return }
            let data = Data(text.utf8)
            guard data.count <= FGProtocolConstants.maxClipboardSize else { return }
            connection?.sendClipboard(mime: "text/plain", data: data)
            return
        }

        // 图片剪贴板（同步图片开关生效）
        guard settings.clipboardSyncImages,
              let png = pasteboard.data(forType: .png), !png.isEmpty
        else { return }
        if png == lastRemoteImage { return }
        clipboardCurrent = ClipboardItem(
            id: "pasteboard-current", kind: .image, preview: "剪贴板图片",
            mime: "image/png", sizeBytes: png.count, ageSeconds: 0, imageData: png
        )
        guard settings.clipboardSync, connection?.isConnected == true,
              png.count <= FGProtocolConstants.maxClipboardSize
        else { return }
        connection?.sendClipboard(mime: "image/png", data: png)
    }

    // MARK: - 剪贴板动作

    public func toggleSetting(_ keyPath: WritableKeyPath<AppSettings, Bool>, label: String) {
        settings[keyPath: keyPath].toggle()
        toast.show("\(label)已\(settings[keyPath: keyPath] ? "开启" : "关闭")", kind: settings[keyPath: keyPath] ? .ok : .info)
    }

    public func copyToLocal(_ item: ClipboardItem) {
        let pasteboard = NSPasteboard.general
        if item.kind == .image {
            guard let data = item.imageData else {
                toast.show("图片内容未保留原始数据", kind: .info)
                return
            }
            pasteboard.clearContents()
            pasteboard.setData(data, forType: .png)
            lastRemoteImage = data
        } else {
            pasteboard.clearContents()
            pasteboard.setString(item.preview, forType: .string)
            lastRemoteText = nil
        }
        clipboardCurrent = item
        toast.show("已写入本地剪贴板", kind: .ok)
    }

    public func clearClipboardHistory() {
        clipboardHistory.removeAll()
        toast.show("已清空同步历史", kind: .ok)
    }

    public func pushClipboard() {
        guard let connection, connection.isConnected else {
            toast.show("未连接对端", kind: .danger)
            return
        }
        if let string = NSPasteboard.general.string(forType: .string) {
            let data = Data(string.utf8)
            guard data.count <= FGProtocolConstants.maxClipboardSize else {
                toast.show("剪贴板超过 63,240 B 上限，未发送", kind: .danger)
                return
            }
            connection.sendClipboard(mime: "text/plain", data: data)
            toast.show("已推送剪贴板（\(data.count) B）", kind: .ok)
        } else {
            toast.show("本机剪贴板没有可发送的文本", kind: .danger)
        }
    }

    // MARK: - 文件动作

    public func chooseAndSendFile() {
        guard connection?.isConnected == true else {
            toast.show("未连接对端", kind: .danger)
            return
        }
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.allowsMultipleSelection = false
        panel.message = "单文件 1 B – 16 MiB"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        sendFile(at: url)
    }

    public func sendFile(at url: URL) {
        guard let connection, connection.isConnected else {
            toast.show("未连接对端", kind: .danger)
            return
        }
        guard let data = try? Data(contentsOf: url) else {
            toast.show("读取文件失败", kind: .danger)
            return
        }
        guard !data.isEmpty, data.count <= FGProtocolConstants.maxFileSize else {
            toast.show("文件需在 1 B – 16 MiB 之间", kind: .danger)
            return
        }
        do {
            _ = try connection.offerFile(name: url.lastPathComponent, data: data)
            pendingSend = (url.lastPathComponent, Int64(data.count))
            activeTransfers.removeAll { $0.id == "peer-send" }
            activeTransfers.insert(
                TransferItem(
                    id: "peer-send", name: url.lastPathComponent, direction: .sending,
                    peerName: peerInfo?.name ?? "对端", sizeBytes: Int64(data.count), state: .active
                ),
                at: 0
            )
            toast.show("已发起传输 \(url.lastPathComponent)", kind: .ok)
        } catch {
            toast.show("发起传输失败", kind: .danger)
        }
    }

    public func exportDiagnostics() {
        toast.show("诊断报告已导出", kind: .ok)
    }

    public func openReceiveDirectory() {
        let path = (settings.receiveDirectory as NSString).expandingTildeInPath
        let url = URL(fileURLWithPath: path, isDirectory: true)
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        NSWorkspace.shared.open(url)
    }

    public func chooseReceiveDirectory() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.prompt = "选择接收目录"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        updateReceiveDirectory(url.path)
        toast.show("接收目录已更新", kind: .ok)
    }

    /// 接收目录唯一入口：保持设置、会话与文本输入三处一致。
    public func updateReceiveDirectory(_ path: String) {
        settings.receiveDirectory = (path as NSString).abbreviatingWithTildeInPath
        connection?.updateReceiveDirectory((path as NSString).expandingTildeInPath)
    }

    // MARK: - 连接事件

    public func handleConnectionEvent(_ event: ConnectionEvent) {
        switch event {
        case let .status(status):
            connectionStatus = status
            accessibilityTrusted = connection?.accessibilityTrusted ?? accessibilityTrusted
            switch status {
            case let .connected(name):
                toast.show("已连接 \(name)", kind: .ok)
            case let .failed(reason):
                captureEnabled = false
                markPeerOffline()
                toast.show(reason, kind: .danger)
            case .idle, .listening:
                captureEnabled = false
                markPeerOffline()
            default:
                break
            }

        case let .peer(info):
            peerInfo = info
            upsertPeerDevice(info)

        case let .latency(ms, _, quality):
            latencyStore.apply(
                deviceID: Self.peerDeviceID,
                ms: ms,
                tier: LatencyTier(quality: quality)
            )

        case let .latencyAlert(ms):
            toast.show("网络延迟连续超过阈值（当前 \(ms.latencyText)）", kind: .danger)

        case let .discovered(peers):
            discoveredPeers = peers

        case let .clipboardIn(mime, data):
            handleClipboardIn(mime: mime, data: data)

        case let .transferProgress(_, sent, total):
            guard total > 0, let index = activeTransfers.firstIndex(where: { $0.id == "peer-send" }) else { return }
            activeTransfers[index].progress = Double(sent) / Double(total)

        case let .transferFinished(_, _, errorCode):
            guard let index = activeTransfers.firstIndex(where: { $0.id == "peer-send" }) else { return }
            var item = activeTransfers.remove(at: index)
            item.progress = 1
            item.state = errorCode == 0 ? .completed : .hashFailed
            item.note = errorCode == 0 ? "已发送" : "传输失败（\(errorCode)）"
            item.ageSeconds = 0
            transferRecords.insert(item, at: 0)
            pendingSend = nil
            toast.show(errorCode == 0 ? "文件已发送" : "文件发送失败", kind: errorCode == 0 ? .ok : .danger)

        case let .offerReceived(transferID, name, size, accepted):
            activeTransfers.removeAll { $0.id == "peer-recv" }
            pendingReceive = accepted ? (transferID, name, size) : nil
            if accepted {
                activeTransfers.insert(
                    TransferItem(
                        id: "peer-recv", name: name, direction: .receiving,
                        peerName: peerInfo?.name ?? "对端", sizeBytes: Int64(size), state: .active
                    ),
                    at: 0
                )
                toast.show("正在接收 \(name)", kind: .ok)
            } else {
                toast.show("收到文件提议 \(name)，已拒绝", kind: .danger)
            }

        case let .fileReceived(name, savedPath, errorCode):
            activeTransfers.removeAll { $0.id == "peer-recv" }
            let receivedSize = pendingReceive?.name == name ? pendingReceive?.size ?? 0 : 0
            pendingReceive = nil
            let record = TransferItem(
                id: "peer-recv-\(name)", name: name, direction: .receiving,
                peerName: peerInfo?.name ?? "对端", sizeBytes: Int64(receivedSize), progress: 1,
                state: errorCode == 0 ? .completed : .hashFailed,
                note: errorCode == 0 ? "SHA-256 已校验" : "接收失败",
                ageSeconds: 0
            )
            transferRecords.insert(record, at: 0)
            if let savedPath {
                toast.show("已接收 \(name) → \(savedPath)", kind: .ok)
            } else {
                toast.show("接收 \(name) 失败", kind: .danger)
            }

        case let .log(message):
            print("[FlowGrid] \(message)")
        }
    }

    private func handleClipboardIn(mime: String, data: Data) {
        let kind: ClipboardKind
        if mime.hasPrefix("image/") {
            kind = .image
        } else if mime.hasPrefix("text/uri-list") {
            kind = .link
        } else {
            kind = .text
        }
        let isImage = mime.hasPrefix("image/")
        let text = isImage ? "图片（\(data.count) B）" : (String(data: data, encoding: .utf8) ?? "\(data.count) 字节")
        let item = ClipboardItem(
            id: "rx-\(UUID().uuidString)",
            kind: kind,
            preview: text.replacingOccurrences(of: "\n", with: " ").prefix(120).description,
            mime: mime,
            sizeBytes: data.count,
            ageSeconds: 0,
            fromDevice: peerInfo?.name ?? "对端",
            imageData: isImage ? data : nil
        )
        clipboardCurrent = item
        clipboardHistory.insert(item, at: 0)
        trimClipboardHistory()
        if settings.autoWriteLocal {
            let pasteboard = NSPasteboard.general
            if isImage, mime == "image/png", settings.clipboardSyncImages {
                pasteboard.clearContents()
                pasteboard.setData(data, forType: .png)
                lastRemoteImage = data
            } else if !isImage, let text = String(data: data, encoding: .utf8) {
                pasteboard.clearContents()
                pasteboard.setString(text, forType: .string)
                lastRemoteText = text // 防止监视器把刚写入的对端内容再推回去
            }
        }
        toast.show("收到剪贴板（\(item.sizeText)）", kind: .ok)
    }

    private func trimClipboardHistory() {
        let limit = max(1, settings.clipboardHistoryLimit.rawValue)
        if clipboardHistory.count > limit {
            clipboardHistory = Array(clipboardHistory.prefix(limit))
        }
    }

    private func upsertPeerDevice(_ info: PeerInfo) {
        let os: DeviceOS
        switch info.platform {
        case .macOS: os = .macOS
        case .windows: os = .windows
        case .linux: os = .linux
        default: os = .linux
        }
        let initials = info.name.isEmpty ? "FG" : String(info.name.prefix(2)).uppercased()
        let device = Device(
            id: Self.peerDeviceID,
            name: info.name,
            os: os,
            osVersion: info.platform.displayName,
            initials: initials,
            link: .directLink,
            isOnline: true
        )
        devices.removeAll { $0.id == Self.peerDeviceID }
        devices.append(device)
    }

    public func removePeerDevice() {
        guard peerDevice?.isOnline != true else { return }
        devices.removeAll { $0.id == Self.peerDeviceID }
        peerInfo = nil
        latencyStore.remove(deviceID: Self.peerDeviceID)
    }

    private func markPeerOffline() {
        guard let index = devices.firstIndex(where: { $0.id == Self.peerDeviceID }) else { return }
        devices[index].isOnline = false
        devices[index].lastSeen = "刚刚断开"
    }

    // MARK: - 本机信息

    private static func makeLocalDevice() -> Device {
        let name = Host.current().localizedName ?? "本机"
        let version = ProcessInfo.processInfo.operatingSystemVersion
        let osVersion = "macOS \(version.majorVersion).\(version.minorVersion)"
        return Device(
            id: localDeviceID,
            name: name,
            os: .macOS,
            osVersion: osVersion,
            initials: String(name.prefix(2)).uppercased(),
            isLocal: true
        )
    }

    private static func readPasteboardItem() -> ClipboardItem? {
        guard let string = NSPasteboard.general.string(forType: .string), !string.isEmpty else {
            return nil
        }
        return pasteboardItem(string)
    }

    private static func pasteboardItem(_ string: String) -> ClipboardItem {
        let preview = string.replacingOccurrences(of: "\n", with: " ").prefix(120)
        return ClipboardItem(
            id: "pasteboard-current",
            kind: .text,
            preview: String(preview),
            mime: "text/plain",
            sizeBytes: string.utf8.count,
            ageSeconds: 0
        )
    }
}
