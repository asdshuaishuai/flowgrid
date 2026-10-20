import CoreGraphics
import Testing

@testable import FlowGridUI

// MARK: - SVG 解析器

@Test func allRegistryIconsParse() {
    let names = IconRegistry.allNames
    #expect(names.count == 55)
    for name in names {
        let elements = IconRegistry.elements(name)
        #expect(!elements.isEmpty, "图标 \(name) 解析为空")
    }
}

@Test func simplePathCommands() throws {
    let elements = try SVGFragmentParser.parse(#"<path d="M6 9l6 6 6-6"/>"#)
    #expect(elements.count == 1)
    let bounds = elements[0].path.boundingRect
    #expect(bounds.minX == 6)
    #expect(bounds.maxX == 18)
    #expect(bounds.minY == 9)
    #expect(bounds.maxY == 15)
}

@Test func arcToBezierProducesExpectedBounds() throws {
    // wifi 外弧：端点 (5,12.5) → (19,12.5)，r=10，sweep=1
    // 圆心在弦下方 (12, 19.64)，弧顶 = 19.64 − 10 ≈ 9.64，缓和上拱
    let elements = try SVGFragmentParser.parse(#"<path d="M5 12.5a10 10 0 0 1 14 0"/>"#)
    let bounds = elements[0].path.boundingRect
    #expect(bounds.minY > 9.5 && bounds.minY < 9.8, "弧顶 ≈ 9.64，实际 \(bounds.minY)")
    #expect(abs(bounds.minX - 5) < 0.5)
    #expect(abs(bounds.maxX - 19) < 0.5)
    #expect(abs(bounds.maxY - 12.5) < 0.5)
}

@Test func concatenatedNumberPairs() throws {
    // "6-6"：无分隔符的连续数字（负号为分隔）
    let elements = try SVGFragmentParser.parse(#"<path d="M6 9l6 6 6-6"/>"#)
    let bounds = elements[0].path.boundingRect
    #expect(bounds.maxX == 18)
    #expect(bounds.minY == 9)
}

@Test func rectCircleEllipseElements() throws {
    let elements = try SVGFragmentParser.parse(
        #"<rect x="2" y="5" width="13" height="9" rx="1.5"/><circle cx="12" cy="12" r="3"/><ellipse cx="12" cy="6" rx="3.5" ry="3"/>"#
    )
    #expect(elements.count == 3)
    let rectBounds = elements[0].path.boundingRect
    #expect(rectBounds == CGRect(x: 2, y: 5, width: 13, height: 9))
    let circleBounds = elements[1].path.boundingRect
    #expect(abs(circleBounds.width - 6) < 0.01)
}

@Test func filledElementDetection() throws {
    // auto 图标含 fill="currentColor" stroke="none" 的填充元素
    let elements = IconRegistry.elements("auto")
    #expect(elements.contains { $0.filled })
    #expect(elements.contains { !$0.filled })
}

// MARK: - 渲染路径

@Test func iconShapeScalesToRect() {
    let shape = IconShape(elements: IconRegistry.elements("plus"), lineWidth: 1.75)
    let path = shape.path(in: CGRect(x: 0, y: 0, width: 32, height: 32))
    let bounds = path.boundingRect
    // 24 → 32 缩放：路径应落进 32×32
    #expect(bounds.width > 0)
    #expect(bounds.minX >= -1)
    #expect(bounds.maxX <= 33)
}

@Test func unknownIconRendersEmpty() {
    let shape = IconShape(elements: IconRegistry.elements("不存在的图标"), lineWidth: 1.75)
    let path = shape.path(in: CGRect(x: 0, y: 0, width: 16, height: 16))
    #expect(path.isEmpty)
}
