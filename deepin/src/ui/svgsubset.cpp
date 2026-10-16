#include "svgsubset.h"

#include <QRectF>
#include <QRegularExpression>
#include <QStringList>
#include <QtMath>

namespace fg::svg {
namespace {

// ── 数字扫描 ──────────────────────────────────────────────────────────
// SVG 的实数允许省略小数点（".5"）、允许正负号、允许指数（"1e-3"）；
// 弧线的两个标志位还可能紧贴写成 "a1 1 0 011 1"，所以标志单独按 0/1 读。
class Scanner {
public:
    explicit Scanner(const QString &s) : s_(s) {}

    int position() const { return i_; }
    bool atEnd() const { return i_ >= s_.size(); }

    void skipSeparators()
    {
        while (i_ < s_.size() && (s_.at(i_).isSpace() || s_.at(i_) == QLatin1Char(','))) {
            ++i_;
        }
    }

    bool peekCommand(QChar *out) const
    {
        if (i_ >= s_.size()) {
            return false;
        }
        const QChar c = s_.at(i_);
        if (c.isLetter()) {
            *out = c;
            return true;
        }
        return false;
    }

    QChar takeCommand()
    {
        return s_.at(i_++);
    }

    bool number(double *out)
    {
        skipSeparators();
        const int start = i_;
        if (i_ < s_.size() && (s_.at(i_) == QLatin1Char('+') || s_.at(i_) == QLatin1Char('-'))) {
            ++i_;
        }
        while (i_ < s_.size() && s_.at(i_).isDigit()) {
            ++i_;
        }
        if (i_ < s_.size() && s_.at(i_) == QLatin1Char('.')) {
            ++i_;
            while (i_ < s_.size() && s_.at(i_).isDigit()) {
                ++i_;
            }
        }
        if (i_ < s_.size() && (s_.at(i_) == QLatin1Char('e') || s_.at(i_) == QLatin1Char('E'))) {
            const int save = i_;
            ++i_;
            if (i_ < s_.size() && (s_.at(i_) == QLatin1Char('+') || s_.at(i_) == QLatin1Char('-'))) {
                ++i_;
            }
            if (i_ < s_.size() && s_.at(i_).isDigit()) {
                while (i_ < s_.size() && s_.at(i_).isDigit()) {
                    ++i_;
                }
            } else {
                i_ = save;  // "e" 不是指数，回退
            }
        }
        if (i_ == start) {
            return false;
        }
        *out = s_.mid(start, i_ - start).toDouble();
        return true;
    }

    bool flag(bool *out)
    {
        skipSeparators();
        if (i_ >= s_.size() || (s_.at(i_) != QLatin1Char('0') && s_.at(i_) != QLatin1Char('1'))) {
            return false;
        }
        *out = (s_.at(i_) == QLatin1Char('1'));
        ++i_;
        return true;
    }

private:
    QString s_;
    int i_ = 0;
};

// SVG 弧线的 rx/ry 为 0 时按直线处理（F.6.2）。
void appendArc(QPainterPath &path, const QPointF &from, double rx, double ry, double xRotDeg,
               bool largeArc, bool sweep, const QPointF &to)
{
    if (rx == 0.0 || ry == 0.0) {
        path.lineTo(to);
        return;
    }
    rx = qAbs(rx);
    ry = qAbs(ry);

    const double phi = qDegreesToRadians(xRotDeg);
    const double cosPhi = qCos(phi);
    const double sinPhi = qSin(phi);

    // F.6.5：换算到弧所在的单位圆坐标系。
    const double dx2 = (from.x() - to.x()) / 2.0;
    const double dy2 = (from.y() - to.y()) / 2.0;
    const double x1p = cosPhi * dx2 + sinPhi * dy2;
    const double y1p = -sinPhi * dx2 + cosPhi * dy2;

    // 半径过小则等比放大到刚好能连上两端点。
    const double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0) {
        const double k = qSqrt(lambda);
        rx *= k;
        ry *= k;
    }

    const double sign = (largeArc != sweep) ? 1.0 : -1.0;
    const double num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    const double den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    const double coef = (den == 0.0) ? 0.0 : sign * qSqrt(qMax(0.0, num / den));
    const double cxp = coef * rx * y1p / ry;
    const double cyp = -coef * ry * x1p / rx;

    const QPointF center(cosPhi * cxp - sinPhi * cyp + (from.x() + to.x()) / 2.0,
                         sinPhi * cxp + cosPhi * cyp + (from.y() + to.y()) / 2.0);

    const auto angleOf = [&](const QPointF &p) {
        return qRadiansToDegrees(qAtan2((p.y() - center.y()) / ry, (p.x() - center.x()) / rx));
    };
    const double theta1 = angleOf(from);
    const double delta = angleOf(to) - theta1;

    // sweep 只决定方向；顺时针扫描时 delta 取模到 (-180, 180]。
    double sweepAngle = sweep ? delta : -delta;
    while (sweepAngle <= -180.0) {
        sweepAngle += 360.0;
    }
    while (sweepAngle > 180.0) {
        sweepAngle -= 360.0;
    }

    path.arcTo(QRectF(center.x() - rx, center.y() - ry, rx * 2.0, ry * 2.0), theta1, sweepAngle);
    // arcMoveTo/arcTo 之后 Qt 把当前点留在弧终点，但为了后续相对指令不出错，
    // 显式回到目标点。
    const auto last = path.currentPosition();
    if (qAbs(last.x() - to.x()) > 1e-9 || qAbs(last.y() - to.y()) > 1e-9) {
        path.lineTo(to);
    }
}

} // namespace

QPainterPath parsePathData(const QString &d)
{
    QPainterPath path;
    Scanner sc(d);

    QChar cmd;
    QPointF cur(0, 0);
    QPointF subpathStart(0, 0);
    // S/s 需要上一个 cubic 的第二个控制点做镜像；C/c 会覆盖它。
    QPointF lastCubicCtrl(0, 0);
    bool haveCubicCtrl = false;

    // SVG 允许省略重复出现的命令字母（如 "L1 1 2 2" 等价于 "L1 1 L2 2"）。
    // 所以每次循环要么读到新命令，要么沿用上一个命令——但 path 必须以命令
    // 开头，否则直接结束而不是拿上一条残值去画。
    bool first = true;
    while (true) {
        sc.skipSeparators();
        QChar next;
        const bool explicitCmd = sc.peekCommand(&next);
        if (explicitCmd) {
            cmd = sc.takeCommand();
        } else if (first) {
            break;
        }
        // 记录本轮起点：Z/z 这类闭合指令不消费任何输入，若允许它们被隐式
        // 重复就会在字符串末尾永远转圈——设计稿里几乎每个闭合图标都以 z
        // 结尾，所以这个兜底不是可选的保险。
        const int before = sc.position();

        const bool rel = cmd.isLower();
        const QChar up = cmd.toUpper();

        auto readPair = [&](QPointF *p) {
            double x = 0;
            double y = 0;
            if (!sc.number(&x) || !sc.number(&y)) {
                return false;
            }
            *p = rel ? QPointF(cur.x() + x, cur.y() + y) : QPointF(x, y);
            return true;
        };

        switch (up.unicode()) {
        case 'M': {
            QPointF p;
            if (!readPair(&p)) {
                return path;
            }
            path.moveTo(p);
            cur = subpathStart = p;
            haveCubicCtrl = false;
            // 后续同命令的坐标对是隐式 lineto。
            cmd = rel ? QLatin1Char('l') : QLatin1Char('L');
            break;
        }
        case 'L': {
            QPointF p;
            if (!readPair(&p)) {
                return path;
            }
            path.lineTo(p);
            cur = p;
            haveCubicCtrl = false;
            break;
        }
        case 'H': {
            double x = 0;
            if (!sc.number(&x)) {
                return path;
            }
            cur = QPointF(rel ? cur.x() + x : x, cur.y());
            path.lineTo(cur);
            haveCubicCtrl = false;
            break;
        }
        case 'V': {
            double y = 0;
            if (!sc.number(&y)) {
                return path;
            }
            cur = QPointF(cur.x(), rel ? cur.y() + y : y);
            path.lineTo(cur);
            haveCubicCtrl = false;
            break;
        }
        case 'C': {
            double x1, y1, x2, y2, x, y;
            if (!sc.number(&x1) || !sc.number(&y1) || !sc.number(&x2) || !sc.number(&y2)
                || !sc.number(&x) || !sc.number(&y)) {
                return path;
            }
            QPointF c1(rel ? cur.x() + x1 : x1, rel ? cur.y() + y1 : y1);
            QPointF c2(rel ? cur.x() + x2 : x2, rel ? cur.y() + y2 : y2);
            QPointF end(rel ? cur.x() + x : x, rel ? cur.y() + y : y);
            path.cubicTo(c1, c2, end);
            lastCubicCtrl = c2;
            haveCubicCtrl = true;
            cur = end;
            break;
        }
        case 'S': {
            double x2, y2, x, y;
            if (!sc.number(&x2) || !sc.number(&y2) || !sc.number(&x) || !sc.number(&y)) {
                return path;
            }
            QPointF c2(rel ? cur.x() + x2 : x2, rel ? cur.y() + y2 : y2);
            QPointF end(rel ? cur.x() + x : x, rel ? cur.y() + y : y);
            // 没有前一个 cubic 时按「控制点=当前点」处理。
            QPointF c1 = haveCubicCtrl
                             ? QPointF(2 * cur.x() - lastCubicCtrl.x(), 2 * cur.y() - lastCubicCtrl.y())
                             : cur;
            path.cubicTo(c1, c2, end);
            lastCubicCtrl = c2;
            haveCubicCtrl = true;
            cur = end;
            break;
        }
        case 'A': {
            double rx, ry, rot, x, y;
            bool largeArc = false;
            bool sweep = false;
            if (!sc.number(&rx) || !sc.number(&ry) || !sc.number(&rot) || !sc.flag(&largeArc)
                || !sc.flag(&sweep) || !sc.number(&x) || !sc.number(&y)) {
                return path;
            }
            QPointF end(rel ? cur.x() + x : x, rel ? cur.y() + y : y);
            appendArc(path, cur, rx, ry, rot, largeArc, sweep, end);
            cur = end;
            haveCubicCtrl = false;
            break;
        }
        case 'Q': {
            // 设计稿图标未用到二次曲线，但 T/q 与 S/s 同理，一并支持以免
            // 后续补图标时静默丢路径。
            double x1, y1, x, y;
            if (!sc.number(&x1) || !sc.number(&y1) || !sc.number(&x) || !sc.number(&y)) {
                return path;
            }
            QPointF c(rel ? cur.x() + x1 : x1, rel ? cur.y() + y1 : y1);
            QPointF end(rel ? cur.x() + x : x, rel ? cur.y() + y : y);
            path.quadTo(c, end);
            lastCubicCtrl = c;
            haveCubicCtrl = true;
            cur = end;
            break;
        }
        case 'T': {
            double x, y;
            if (!sc.number(&x) || !sc.number(&y)) {
                return path;
            }
            QPointF c = haveCubicCtrl
                             ? QPointF(2 * cur.x() - lastCubicCtrl.x(), 2 * cur.y() - lastCubicCtrl.y())
                             : cur;
            QPointF end(rel ? cur.x() + x : x, rel ? cur.y() + y : y);
            path.quadTo(c, end);
            lastCubicCtrl = c;
            haveCubicCtrl = true;
            cur = end;
            break;
        }
        case 'Z': {
            path.closeSubpath();
            cur = subpathStart;
            haveCubicCtrl = false;
            break;
        }
        default:
            // 不支持的命令：立即返回，避免死循环。
            return path;
        }
        if (sc.position() == before) {
            break;  // 本轮没有消费任何输入，继续下去只会空转
        }
        first = false;
    }
    return path;
}

namespace {

QString attr(const QString &tag, const QString &name, bool *ok = nullptr)
{
    // 注意：这里刻意不缓存正则。早期版本把它写成函数内 static，结果只记住了
    // 第一次传入的属性名，之后 x/y/width/r 全部读到 fallback 0，矩形与圆形
    // 被静默丢弃——图标能编译能跑，只是缺了笔画，极难肉眼发现。
    const QRegularExpression re(QStringLiteral("\\b%1\\s*=\\s*\"([^\"]*)\"").arg(name));
    const auto m = re.match(tag);
    if (!m.hasMatch()) {
        if (ok != nullptr) {
            *ok = false;
        }
        return {};
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return m.captured(1);
}

double attrNum(const QString &tag, const QString &name, double fallback)
{
    bool ok = false;
    const QString v = attr(tag, name, &ok);
    return ok ? v.toDouble() : fallback;
}

} // namespace

QPainterPath parse(const QString &fragment)
{
    QPainterPath out;

    // 逐个标签扫描。图标片段是我们自己写的静态表，不含 CDATA / 注释，
    // 所以朴素扫描足够，也不值得为此引入 XML 解析器。
    int i = 0;
    while (true) {
        const int lt = fragment.indexOf(QLatin1Char('<'), i);
        if (lt < 0) {
            break;
        }
        const int gt = fragment.indexOf(QLatin1Char('>'), lt);
        if (gt < 0) {
            break;
        }
        const QString tag = fragment.mid(lt + 1, gt - lt - 1);
        i = gt + 1;

        if (tag.startsWith(QLatin1String("path"))) {
            out.addPath(parsePathData(attr(tag, QStringLiteral("d"))));
        } else if (tag.startsWith(QLatin1String("rect"))) {
            const double x = attrNum(tag, QStringLiteral("x"), 0.0);
            const double y = attrNum(tag, QStringLiteral("y"), 0.0);
            const double w = attrNum(tag, QStringLiteral("width"), 0.0);
            const double h = attrNum(tag, QStringLiteral("height"), 0.0);
            const double rx = attrNum(tag, QStringLiteral("rx"), 0.0);
            if (w <= 0.0 || h <= 0.0) {
                continue;
            }
            if (rx > 0.0) {
                out.addRoundedRect(QRectF(x, y, w, h), rx, rx);
            } else {
                out.addRect(QRectF(x, y, w, h));
            }
        } else if (tag.startsWith(QLatin1String("circle"))) {
            const double cx = attrNum(tag, QStringLiteral("cx"), 0.0);
            const double cy = attrNum(tag, QStringLiteral("cy"), 0.0);
            const double r = attrNum(tag, QStringLiteral("r"), 0.0);
            if (r > 0.0) {
                out.addEllipse(QPointF(cx, cy), r, r);
            }
        } else if (tag.startsWith(QLatin1String("ellipse"))) {
            const double cx = attrNum(tag, QStringLiteral("cx"), 0.0);
            const double cy = attrNum(tag, QStringLiteral("cy"), 0.0);
            const double rx = attrNum(tag, QStringLiteral("rx"), 0.0);
            const double ry = attrNum(tag, QStringLiteral("ry"), 0.0);
            if (rx > 0.0 && ry > 0.0) {
                out.addEllipse(QPointF(cx, cy), rx, ry);
            }
        }
        // fill="currentColor" 的填充型图标（如 app.js 的 auto）不在当前图标表里，
        // 出现时按描边处理即可——不会画错，只是少填一块。
    }
    return out;
}

} // namespace fg::svg
