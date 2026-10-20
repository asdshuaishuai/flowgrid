import CoreGraphics
import Foundation
import SwiftUI

/// 图标元素：24×24 视口内的一条路径；`filled` 表示原样填充（否则按 1.75 描边转轮廓）。
/// 解析完成后不可变；`Path` 为值语义，跨线程共享安全。
public struct IconElement: @unchecked Sendable {
    public let path: Path
    public let filled: Bool

    public init(path: Path, filled: Bool) {
        self.path = path
        self.filled = filled
    }
}

/// 设计稿图标集的 SVG 子集解析器：path 全命令（含椭圆弧）+ rect/circle/ellipse。
/// 输入是 app.js 里的内联 SVG 片段（已知来源、规范格式），输出 24 网格路径。
public enum SVGFragmentParser {

    public enum ParseError: Error {
        case malformedElement(String)
        case malformedPath(String)
    }

    /// 解析一段 SVG 片段（可含多个 <path>/<rect>/<circle>/<ellipse>）。
    public static func parse(_ fragment: String) throws -> [IconElement] {
        var elements: [IconElement] = []
        var scanner = Substring(fragment)
        while let tagStart = scanner.firstIndex(of: "<") {
            let rest = scanner[scanner.index(after: tagStart)...]
            guard let tagEnd = rest.firstIndex(of: ">") else { throw ParseError.malformedElement(fragment) }
            let tag = String(rest[..<tagEnd])
            scanner = rest[rest.index(after: tagEnd)...]
            if tag.hasPrefix("/") { continue }

            let name = tag.prefix { !$0.isWhitespace && $0 != "/" }
            let attrs = parseAttributes(String(tag.dropFirst(name.count)))
            switch name {
            case "path":
                guard let d = attrs["d"] else { throw ParseError.malformedElement(tag) }
                let path = try parsePath(d)
                elements.append(IconElement(path: path, filled: attrs["fill"] != nil && attrs["fill"] != "none"))
            case "rect":
                let x = number(attrs["x"]) ?? 0
                let y = number(attrs["y"]) ?? 0
                let w = number(attrs["width"]) ?? 0
                let h = number(attrs["height"]) ?? 0
                let rx = number(attrs["rx"]) ?? number(attrs["ry"]) ?? 0
                let rect = CGRect(x: x, y: y, width: w, height: h)
                let path = rx > 0
                    ? Path(roundedRect: rect, cornerRadius: rx)
                    : Path(rect)
                elements.append(IconElement(path: path, filled: attrs["fill"] != nil && attrs["fill"] != "none"))
            case "circle":
                let cx = number(attrs["cx"]) ?? 0
                let cy = number(attrs["cy"]) ?? 0
                let r = number(attrs["r"]) ?? 0
                elements.append(IconElement(
                    path: Path(ellipseIn: CGRect(x: cx - r, y: cy - r, width: 2 * r, height: 2 * r)),
                    filled: false
                ))
            case "ellipse":
                let cx = number(attrs["cx"]) ?? 0
                let cy = number(attrs["cy"]) ?? 0
                let rx = number(attrs["rx"]) ?? 0
                let ry = number(attrs["ry"]) ?? 0
                elements.append(IconElement(
                    path: Path(ellipseIn: CGRect(x: cx - rx, y: cy - ry, width: 2 * rx, height: 2 * ry)),
                    filled: false
                ))
            default:
                break
            }
        }
        return elements
    }

    // MARK: - 属性

    private static func parseAttributes(_ text: String) -> [String: String] {
        var result: [String: String] = [:]
        var scanner = Substring(text)
        while let eq = scanner.firstIndex(of: "=") {
            let name = scanner[..<eq].trimmingCharacters(in: .whitespaces)
            let after = scanner[scanner.index(after: eq)...]
            guard let quote = after.first, quote == "\"" || quote == "'" else { break }
            let valueStart = after.index(after: after.startIndex)
            guard let valueEnd = after[valueStart...].firstIndex(of: quote) else { break }
            result[name] = String(after[valueStart..<valueEnd])
            scanner = after[after.index(after: valueEnd)...]
        }
        return result
    }

    private static func number(_ text: String?) -> CGFloat? {
        guard let text, let v = Double(text) else { return nil }
        return CGFloat(v)
    }

    // MARK: - path d 解析

    static func parsePath(_ d: String) throws -> Path {
        var parser = PathDataParser(d)
        return try parser.parse()
    }

    struct PathDataParser {
        private let scalars: [Character]
        private var index = 0
        private var path = Path()
        private var current = CGPoint.zero
        private var subpathStart = CGPoint.zero
        private var lastCubicControl: CGPoint?
        private var lastQuadControl: CGPoint?

        init(_ d: String) {
            scalars = Array(d)
        }

        private var isAtEnd: Bool { index >= scalars.count }

        private mutating func skipSeparators() {
            while index < scalars.count, scalars[index] == " " || scalars[index] == "," || scalars[index] == "\n" || scalars[index] == "\t" {
                index += 1
            }
        }

        private mutating func nextCommand() -> Character? {
            skipSeparators()
            guard index < scalars.count, scalars[index].isLetter else { return nil }
            defer { index += 1 }
            return scalars[index]
        }

        /// 数字扫描：支持 1e-5、-.5、小数点；"-" 自带分隔含义。
        private mutating func scanNumber() -> CGFloat? {
            skipSeparators()
            var text = ""
            if index < scalars.count, scalars[index] == "+" || scalars[index] == "-" {
                text.append(scalars[index])
                index += 1
            }
            var sawDot = false
            var sawDigit = false
            loop: while index < scalars.count {
                let c = scalars[index]
                if c.isNumber {
                    text.append(c)
                    index += 1
                    sawDigit = true
                } else if c == "." && !sawDot {
                    text.append(c)
                    index += 1
                    sawDot = true
                } else if c == "e" || c == "E" {
                    text.append(c)
                    index += 1
                    if index < scalars.count, scalars[index] == "+" || scalars[index] == "-" {
                        text.append(scalars[index])
                        index += 1
                    }
                } else {
                    break loop
                }
            }
            guard sawDigit, let value = Double(text) else { return nil }
            return CGFloat(value)
        }

        private mutating func hasNumber() -> Bool {
            skipSeparators()
            guard index < scalars.count else { return false }
            let c = scalars[index]
            return c.isNumber || c == "-" || c == "+" || c == "."
        }

        mutating func parse() throws -> Path {
            var command: Character?
            while true {
                if let next = nextCommand() {
                    command = next
                } else if command == nil || !hasNumber() {
                    break
                }
                guard let cmd = command else { break }
                try execute(cmd)
            }
            return path
        }

        private mutating func point(_ absolute: Bool, relX: CGFloat, relY: CGFloat) -> CGPoint {
            absolute ? CGPoint(x: relX, y: relY) : CGPoint(x: current.x + relX, y: current.y + relY)
        }

        private mutating func requireNumbers(_ n: Int) throws -> [CGFloat] {
            var values: [CGFloat] = []
            for _ in 0..<n {
                guard let v = scanNumber() else { throw ParseError.malformedPath("缺少参数") }
                values.append(v)
            }
            return values
        }

        private mutating func execute(_ cmd: Character) throws {
            let upper = Character(cmd.uppercased())
            let absolute = cmd.isUppercase

            switch upper {
            case "M":
                var first = true
                while hasNumber() {
                    let v = try requireNumbers(2)
                    let p = point(absolute, relX: v[0], relY: v[1])
                    if first {
                        path.move(to: p)
                        subpathStart = p
                        first = false
                    } else {
                        path.addLine(to: p)
                    }
                    current = p
                    lastCubicControl = nil
                    lastQuadControl = nil
                    if !hasNumber() { break }
                }
            case "L":
                let v = try requireNumbers(2)
                let p = point(absolute, relX: v[0], relY: v[1])
                path.addLine(to: p)
                current = p
                lastCubicControl = nil
                lastQuadControl = nil
            case "H":
                let v = try requireNumbers(1)
                let x = absolute ? v[0] : current.x + v[0]
                let p = CGPoint(x: x, y: current.y)
                path.addLine(to: p)
                current = p
                lastCubicControl = nil
                lastQuadControl = nil
            case "V":
                let v = try requireNumbers(1)
                let y = absolute ? v[0] : current.y + v[0]
                let p = CGPoint(x: current.x, y: y)
                path.addLine(to: p)
                current = p
                lastCubicControl = nil
                lastQuadControl = nil
            case "C":
                let v = try requireNumbers(6)
                let c1 = point(absolute, relX: v[0], relY: v[1])
                let c2 = point(absolute, relX: v[2], relY: v[3])
                let end = point(absolute, relX: v[4], relY: v[5])
                path.addCurve(to: end, control1: c1, control2: c2)
                current = end
                lastCubicControl = c2
                lastQuadControl = nil
            case "S":
                let v = try requireNumbers(4)
                let c1 = lastCubicControl.map { CGPoint(x: 2 * current.x - $0.x, y: 2 * current.y - $0.y) } ?? current
                let c2 = point(absolute, relX: v[0], relY: v[1])
                let end = point(absolute, relX: v[2], relY: v[3])
                path.addCurve(to: end, control1: c1, control2: c2)
                current = end
                lastCubicControl = c2
                lastQuadControl = nil
            case "Q":
                let v = try requireNumbers(4)
                let c = point(absolute, relX: v[0], relY: v[1])
                let end = point(absolute, relX: v[2], relY: v[3])
                path.addQuadCurve(to: end, control: c)
                current = end
                lastQuadControl = c
                lastCubicControl = nil
            case "T":
                let v = try requireNumbers(2)
                let c = lastQuadControl.map { CGPoint(x: 2 * current.x - $0.x, y: 2 * current.y - $0.y) } ?? current
                let end = point(absolute, relX: v[0], relY: v[1])
                path.addQuadCurve(to: end, control: c)
                current = end
                lastQuadControl = c
                lastCubicControl = nil
            case "A":
                let v = try requireNumbers(7)
                let end = point(absolute, relX: v[5], relY: v[6])
                appendArc(rx: v[0], ry: v[1], rotationDegrees: v[2], largeArc: v[3] != 0, sweep: v[4] != 0, to: end)
                current = end
                lastCubicControl = nil
                lastQuadControl = nil
            case "Z":
                path.closeSubpath()
                current = subpathStart
                lastCubicControl = nil
                lastQuadControl = nil
            default:
                throw ParseError.malformedPath("未知命令 \(cmd)")
            }
        }

        // MARK: - 椭圆弧（SVG 规范 F.6：端点参数化 → 中心参数化 → 三次贝塞尔）

        private mutating func appendArc(rx: CGFloat, ry: CGFloat, rotationDegrees: CGFloat, largeArc: Bool, sweep: Bool, to end: CGPoint) {
            let start = current
            var rx = abs(rx)
            var ry = abs(ry)
            if rx == 0 || ry == 0 || start == end {
                path.addLine(to: end)
                return
            }
            let phi = rotationDegrees * .pi / 180
            let cosPhi = cos(phi)
            let sinPhi = sin(phi)

            // 换算到旋转后的中点坐标
            let dx = (start.x - end.x) / 2
            let dy = (start.y - end.y) / 2
            let x1p = cosPhi * dx + sinPhi * dy
            let y1p = -sinPhi * dx + cosPhi * dy

            // 半径不足时按规范放大
            let lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
            if lambda > 1 {
                let scale = sqrt(lambda)
                rx *= scale
                ry *= scale
            }

            let sign: CGFloat = largeArc != sweep ? 1 : -1
            let numerator = max(0, rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p)
            let denominator = rx * rx * y1p * y1p + ry * ry * x1p * x1p
            let coefficient = denominator == 0 ? 0 : sign * sqrt(numerator / denominator)
            let cxp = coefficient * (rx * y1p / ry)
            let cyp = coefficient * -(ry * x1p / rx)

            let cx = cosPhi * cxp - sinPhi * cyp + (start.x + end.x) / 2
            let cy = sinPhi * cxp + cosPhi * cyp + (start.y + end.y) / 2

            func angle(_ ux: CGFloat, _ uy: CGFloat, _ vx: CGFloat, _ vy: CGFloat) -> CGFloat {
                let dot = ux * vx + uy * vy
                let len = sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy))
                var a = acos(max(-1, min(1, dot / max(len, .leastNonzeroMagnitude))))
                if ux * vy - uy * vx < 0 { a = -a }
                return a
            }

            let theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
            var delta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
            if !sweep && delta > 0 { delta -= 2 * .pi }
            if sweep && delta < 0 { delta += 2 * .pi }

            // 拆成 ≤ π/2 的段，每段三次贝塞尔
            let segments = max(1, Int(ceil(abs(delta) / (.pi / 2))))
            let deltaSegment = delta / CGFloat(segments)
            let k = 4.0 / 3.0 * tan(deltaSegment / 4)

            func arcPoint(_ t: CGFloat) -> CGPoint {
                let x = rx * cos(t)
                let y = ry * sin(t)
                return CGPoint(
                    x: cx + cosPhi * x - sinPhi * y,
                    y: cy + sinPhi * x + cosPhi * y
                )
            }

            func arcDerivative(_ t: CGFloat) -> CGPoint {
                let dxd = -rx * sin(t)
                let dyd = ry * cos(t)
                return CGPoint(
                    x: cosPhi * dxd - sinPhi * dyd,
                    y: sinPhi * dxd + cosPhi * dyd
                )
            }

            var t = theta1
            var from = start
            for _ in 0..<segments {
                let t2 = t + deltaSegment
                let p2 = arcPoint(t2)
                let d1 = arcDerivative(t)
                let d2 = arcDerivative(t2)
                let c1 = CGPoint(x: from.x + k * d1.x, y: from.y + k * d1.y)
                let c2 = CGPoint(x: p2.x - k * d2.x, y: p2.y - k * d2.y)
                path.addCurve(to: p2, control1: c1, control2: c2)
                from = p2
                t = t2
            }
        }
    }
}
