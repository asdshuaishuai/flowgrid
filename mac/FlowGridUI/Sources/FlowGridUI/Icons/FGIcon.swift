import SwiftUI

/// 设计稿图标：24 网格 / 1.75 描边（按渲染尺寸等比缩放）。
/// 用法：`FGIcon("devices", size: 16, color: FGColor.text3)`。
public struct FGIcon: View {
    private let name: String
    private let size: CGFloat
    private let color: Color?
    private let lineWidth: CGFloat

    public init(_ name: String, size: CGFloat = 16, color: Color? = nil, lineWidth: CGFloat = 1.75) {
        self.name = name
        self.size = size
        self.color = color
        self.lineWidth = lineWidth
    }

    public var body: some View {
        IconShape(elements: IconRegistry.elements(name), lineWidth: lineWidth)
            .fill(color ?? FGColor.text1)
            .frame(width: size, height: size)
    }
}

/// 把图标元素合成为单一可填充路径：描边元素经 strokedPath 转轮廓，
/// 与填充元素一起 fill —— 一个视图层搞定，不需要逐元素分层。
struct IconShape: Shape, @unchecked Sendable {
    let elements: [IconElement]
    let lineWidth: CGFloat

    func path(in rect: CGRect) -> Path {
        var path = Path()
        guard !elements.isEmpty, rect.width > 0 else { return path }
        let scale = min(rect.width, rect.height) / 24
        let transform = CGAffineTransform(scaleX: scale, y: scale)
            .concatenating(CGAffineTransform(translationX: rect.minX, y: rect.minY))
        for element in elements {
            let scaled = element.path.applying(transform)
            if element.filled {
                path.addPath(scaled)
            } else {
                path.addPath(scaled.strokedPath(StrokeStyle(
                    lineWidth: lineWidth * scale,
                    lineCap: .round,
                    lineJoin: .round
                )))
            }
        }
        return path
    }
}
