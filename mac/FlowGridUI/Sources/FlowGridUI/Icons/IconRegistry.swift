import Foundation

/// 图标注册表：与设计稿 app.js 的 P 映射逐条对应（24 网格 / 1.75 描边）。
/// 原始片段解析一次后缓存（Path 值语义、解析后只读）。
public enum IconRegistry {

    private static let raw: [String: String] = [
        "chevronDown": #"<path d="M6 9l6 6 6-6"/>"#,
        "chevronRight": #"<path d="M9 6l6 6-6 6"/>"#,
        "check": #"<path d="M20 6L9 17l-5-5"/>"#,
        "close": #"<path d="M18 6L6 18M6 6l12 12"/>"#,
        "plus": #"<path d="M12 5v14M5 12h14"/>"#,
        "minus": #"<path d="M5 12h14"/>"#,
        "arrowRight": #"<path d="M5 12h14M13 6l6 6-6 6"/>"#,
        "refresh": #"<path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 4v5h-5"/>"#,
        "power": #"<path d="M12 3v9"/><path d="M18.4 6.6a9 9 0 1 1-12.8 0"/>"#,
        "devices": #"<rect x="2" y="5" width="13" height="9" rx="1.5"/><rect x="15" y="9" width="7" height="10" rx="1.5"/><path d="M6 18h5"/>"#,
        "keyboard": #"<rect x="2" y="6" width="20" height="12" rx="2"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M7 14h10"/>"#,
        "clipboard": #"<rect x="6" y="4" width="12" height="4" rx="1"/><path d="M6 6H5a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1V7a1 1 0 0 0-1-1h-1"/>"#,
        "activity": #"<path d="M3 12h4l3-8 4 16 3-8h4"/>"#,
        "settings": #"<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-2.9 1.2v.2a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.6 1.7 1.7 0 0 0-1.9.4l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0-1.2-2.9H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.6-1.1 1.7 1.7 0 0 0-.4-1.9l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.9.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 2.9 1.2l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.9V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>"#,
        "search": #"<circle cx="11" cy="11" r="7"/><path d="M20 20l-3.5-3.5"/>"#,
        "monitor": #"<rect x="2" y="3" width="20" height="13" rx="2"/><path d="M8 20h8M12 16v4"/>"#,
        "laptop": #"<rect x="3" y="5" width="18" height="11" rx="2"/><path d="M2 19h20"/>"#,
        "tablet": #"<rect x="4" y="2" width="16" height="20" rx="2"/><path d="M11 18h2"/>"#,
        "phone": #"<rect x="6" y="2" width="12" height="20" rx="2.5"/><path d="M11 18h2"/>"#,
        "wifi": #"<path d="M5 12.5a10 10 0 0 1 14 0"/><path d="M8.5 16a5 5 0 0 1 7 0"/><path d="M12 19.5h.01"/>"#,
        "lan": #"<path d="M9 15l6-6"/><rect x="2" y="13" width="6" height="6" rx="1"/><rect x="16" y="3" width="6" height="6" rx="1"/>"#,
        "lock": #"<rect x="4" y="10" width="16" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/>"#,
        "unlock": #"<rect x="4" y="10" width="16" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 7.5-2"/>"#,
        "eye": #"<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7-10-7-10-7z"/><circle cx="12" cy="12" r="3"/>"#,
        "trash": #"<path d="M4 7h16M10 11v6M14 11v6"/><path d="M6 7l1 13h10l1-13M9 7V4h6v3"/>"#,
        "edit": #"<path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z"/>"#,
        "more": #"<circle cx="5" cy="12" r="1.2"/><circle cx="12" cy="12" r="1.2"/><circle cx="19" cy="12" r="1.2"/>"#,
        "info": #"<circle cx="12" cy="12" r="9"/><path d="M12 16v-4M12 8h.01"/>"#,
        "warn": #"<path d="M10.3 3.9 2.4 18a2 2 0 0 0 1.7 3h15.8a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/><path d="M12 9v4M12 17h.01"/>"#,
        "file": #"<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5"/>"#,
        "upload": #"<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="M12 3v12"/><path d="M7 8l5-5 5 5"/>"#,
        "download": #"<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="M12 15V3"/><path d="M7 10l5 5 5-5"/>"#,
        "folder": #"<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>"#,
        "copy": #"<rect x="9" y="9" width="12" height="12" rx="2"/><path d="M5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1"/>"#,
        "image": #"<rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="8.5" cy="9.5" r="1.5"/><path d="M21 16l-5-5-9 9"/>"#,
        "type": #"<path d="M4 6V4h16v2M12 4v16M9 20h6"/>"#,
        "code": #"<path d="M8 6l-5 6 5 6M16 6l5 6-5 6"/>"#,
        "grid": #"<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>"#,
        "grip": #"<circle cx="9" cy="6" r="1"/><circle cx="15" cy="6" r="1"/><circle cx="9" cy="12" r="1"/><circle cx="15" cy="12" r="1"/><circle cx="9" cy="18" r="1"/><circle cx="15" cy="18" r="1"/>"#,
        "sun": #"<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>"#,
        "moon": #"<path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/>"#,
        "auto": #"<circle cx="12" cy="12" r="9"/><path d="M12 3v18a9 9 0 0 0 0-18z" fill="currentColor" stroke="none"/>"#,
        "apple": #"<path d="M16.5 12.7c0-2.2 1.8-3.3 1.9-3.4-1-1.5-2.6-1.7-3.2-1.7-1.3-.1-2.6.8-3.3.8-.7 0-1.7-.8-2.8-.8-1.4 0-2.8.9-3.5 2.2-1.5 2.6-.4 6.5 1.1 8.6.7 1 1.6 2.2 2.7 2.2 1.1 0 1.5-.7 2.8-.7 1.3 0 1.6.7 2.7.7 1.1 0 1.9-1.1 2.6-2.1.4-.6.9-1.6 1.3-2.7-1.3-.5-2.3-1.7-2.3-3.1z"/><path d="M14.6 6.3c.6-.7 1-1.7.9-2.7-.9 0-2 .6-2.6 1.3-.6.7-1 1.7-.9 2.6 1 .1 2-.5 2.6-1.2z"/>"#,
        "windows": #"<rect x="3" y="3" width="8" height="8" rx="1"/><rect x="13" y="3" width="8" height="8" rx="1"/><rect x="3" y="13" width="8" height="8" rx="1"/><rect x="13" y="13" width="8" height="8" rx="1"/>"#,
        "linux": #"<ellipse cx="12" cy="6" rx="3.5" ry="3"/><path d="M8.5 9.5C7 11 6 13.5 6 16c0 1.5.8 3 2 4M15.5 9.5C17 11 18 13.5 18 16c0 1.5-.8 3-2 4"/><ellipse cx="9" cy="6" rx="1" ry="1.6"/><ellipse cx="15" cy="6" rx="1" ry="1.6"/><path d="M12 9v4"/>"#,
        "deepin": #"<circle cx="12" cy="12" r="9"/><path d="M8 12h8M12 8v8"/>"#,
        "shield": #"<path d="M12 3l8 3v6c0 5-3.5 8.3-8 9-4.5-.7-8-4-8-9V6z"/><path d="M9 12l2 2 4-4"/>"#,
        "bell": #"<path d="M18 8a6 6 0 1 0-12 0c0 7-3 7-3 7h18s-3 0-3-7"/><path d="M13.7 21a2 2 0 0 1-3.4 0"/>"#,
        "pause": #"<rect x="6" y="5" width="4" height="14" rx="1"/><rect x="14" y="5" width="4" height="14" rx="1"/>"#,
        "play": #"<path d="M7 4l12 8-12 8z"/>"#,
        "spinner": #"<path d="M12 3a9 9 0 1 0 9 9"/>"#,
        "drag": #"<path d="M8 6h.01M8 12h.01M8 18h.01M16 6h.01M16 12h.01M16 18h.01"/>"#,
        "mouse": #"<rect x="6" y="3" width="12" height="18" rx="6"/><path d="M12 7v4"/>"#,
        "link": #"<path d="M10 13a5 5 0 0 0 7.5.5l3-3a5 5 0 0 0-7-7l-1.7 1.7"/><path d="M14 11a5 5 0 0 0-7.5-.5l-3 3a5 5 0 0 0 7 7l1.7-1.7"/>"#,
        "unlink": #"<path d="M18.8 13.5l1.7-1.7a5 5 0 0 0-7-7l-1.7 1.7M5.2 10.5l-1.7 1.7a5 5 0 0 0 7 7l1.7-1.7"/><path d="M3 3l18 18"/>"#,
    ]

    /// 懒解析一次（static let 即 dispatch_once）。
    private static let parsed: [String: [IconElement]] = {
        var result: [String: [IconElement]] = [:]
        for (name, fragment) in raw {
            if let elements = try? SVGFragmentParser.parse(fragment) {
                result[name] = elements
            }
        }
        return result
    }()

    public static func elements(_ name: String) -> [IconElement] {
        parsed[name] ?? []
    }

    public static var allNames: [String] { Array(raw.keys) }
}
