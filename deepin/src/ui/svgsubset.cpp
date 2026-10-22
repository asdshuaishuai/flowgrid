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

    // 坐标系约定：这里的 atan2 在 y 向下的坐标系里给出的是 **SVG 空间**的角 φ，
    // 而 QPainterPath::arcTo 的角 θ 是 y 向上的（θ = -φ）。起点角和跨度都必须取负，
    // 否则整条弧会镜像到另一侧——早期版本正是栽在这里：wifi 的两条弧被画到了
    // 画布下方（y≈29 > viewBox 24），屏幕上只剩两段竖线。
    const auto angleOf = [&](const QPointF &p) {
        return qRadiansToDegrees(qAtan2((p.y() - center.y()) / ry, (p.x() - center.x()) / rx));
    };

    // SVG 的 sweep 只决定方向：1 = φ 增大。先把 delta 落到该方向的那个模上。
    double delta = angleOf(to) - angleOf(from);
    if (sweep && delta < 0.0) {
        delta += 360.0;
    } else if (!sweep && delta > 0.0) {
        delta -= 360.0;
    }
    // 再按 largeArc 在长弧/短弧之间切换（同一个端点对有两个合法弧）。
    if (largeArc && qAbs(delta) < 180.0) {
        delta += delta >= 0.0 ? 360.0 : -360.0;
    } else if (!largeArc && qAbs(delta) > 180.0) {
        delta -= delta >= 0.0 ? 360.0 : -360.0;
    }
    if (qFuzzyIsNull(delta)) {
        // 起终点重合 = 整圆，取正向一整圈。
        delta = sweep ? 360.0 : -360.0;
    }

    path.arcTo(QRectF(center.x() - rx, center.y() - ry, rx * 2.0, ry * 2.0),
               -angleOf(from), -delta);
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
    // S/s 与 T/t 各自要反射**自己那族**的前一个控制点，按 SVG 规范不能混：
    //   · S/s 反射的是上一个**三次**曲线（C/c/S/s）的第二个控制点；
    //   · T/t 反射的是上一个**二次**曲线（Q/q/T/t）的控制点。
    // 前一个命令是另一族时，控制点不存在，按「控制点 = 当前点」处理。
    //
    // 原先两族共用一个 lastCubicCtrl：S 跟在 Q 后面会把二次曲线的控制点
    // 当成三次的来反射，T 跟在 C 后面也一样——画出来的曲线是歪的，而且
    // 歪得毫无规律，很难看出是哪儿错了。
    QPointF lastCubicCtrl(0, 0);
    bool haveCubicCtrl = false;
    QPointF lastQuadCtrl(0, 0);
    bool haveQuadCtrl = false;

    // SVG 允许省略重复出现的命令字母（如 "L1 1 2 2" 等价于 "L1 1 L2 2"）。
    // 所以每次循环要么读到新命令，要么沿用上一个命令——但 path 必须以命令
    // 开头，否则直接结束而不是拿上一条残值去画。
    bool first = true;
    while (true) {
        sc.skipSeparators();
        // 本轮起点：必须在**取命令字母之前**记。
        //
        // Z/z 不带参数、闭合子路径后什么也不消费。起点若记在取完字母之后，
        // 「本轮没有消费任何输入」这条对**显式**的 z 也成立——路径会在第一个
        // z 处被截断，后面那些子路径（一个图标里常有三四段）全被丢掉，
        // 而且一路没有任何报错：图标画得出来，只是缺了几笔。
        //
        // 记在取字母之前：显式的 z 至少消费了那个字母（位置前进 1），不会
        // 触发兜底；只有**隐式重复**的 z 才真的什么都没消费，那时兜底生效，
        // 防止在字符串末尾空转到死——设计稿里几乎每个闭合图标都以 z 结尾。
        const int before = sc.position();

        QChar next;
        const bool explicitCmd = sc.peekCommand(&next);
        if (explicitCmd) {
            cmd = sc.takeCommand();
        } else if (first) {
            break;
        }

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
            haveQuadCtrl = false;
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
            haveQuadCtrl = false;
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
            haveQuadCtrl = false;
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
            haveQuadCtrl = false;
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
            haveQuadCtrl = false;
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
            haveQuadCtrl = false;
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
            haveQuadCtrl = false;
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
            lastQuadCtrl = c;
            haveQuadCtrl = true;
            haveCubicCtrl = false;
            cur = end;
            break;
        }
        case 'T': {
            double x, y;
            if (!sc.number(&x) || !sc.number(&y)) {
                return path;
            }
            // T 反射的是上一个**二次**曲线的控制点；前一个命令是 C/S 时不存在，
            // 按「控制点 = 当前点」处理。
            QPointF c = haveQuadCtrl
                             ? QPointF(2 * cur.x() - lastQuadCtrl.x(), 2 * cur.y() - lastQuadCtrl.y())
                             : cur;
            QPointF end(rel ? cur.x() + x : x, rel ? cur.y() + y : y);
            path.quadTo(c, end);
            lastQuadCtrl = c;
            haveQuadCtrl = true;
            haveCubicCtrl = false;
            cur = end;
            break;
        }
        case 'Z': {
            path.closeSubpath();
            cur = subpathStart;
            haveCubicCtrl = false;
            haveQuadCtrl = false;
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

Icon parse(const QString &fragment)
{
    Icon out;

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

        // fill="1" 是「底衬」层（屏幕内容、卡片面板这类大面积形体），
        // fill="2" 是「实体」层（三角形、圆点这类形体本身就是内容的元素）。
        // 两者必须分开：纯形体按 18% 铺出来等于没画，浅色主题下直接消失。
        // 取不到属性时按描边处理——图标表里绝大多数元素确实是描边。
        const QString fillAttr = attr(tag, QStringLiteral("fill"));
        const int layer = fillAttr == QLatin1String("2")   ? 2
                          : fillAttr == QLatin1String("1") ? 1
                                                           : 0;
        QPainterPath &target = layer == 2 ? out.solid : (layer == 1 ? out.fill : out.stroke);

        if (tag.startsWith(QLatin1String("path"))) {
            target.addPath(parsePathData(attr(tag, QStringLiteral("d"))));
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
                target.addRoundedRect(QRectF(x, y, w, h), rx, rx);
            } else {
                target.addRect(QRectF(x, y, w, h));
            }
        } else if (tag.startsWith(QLatin1String("circle"))) {
            const double cx = attrNum(tag, QStringLiteral("cx"), 0.0);
            const double cy = attrNum(tag, QStringLiteral("cy"), 0.0);
            const double r = attrNum(tag, QStringLiteral("r"), 0.0);
            if (r > 0.0) {
                target.addEllipse(QPointF(cx, cy), r, r);
            }
        } else if (tag.startsWith(QLatin1String("ellipse"))) {
            const double cx = attrNum(tag, QStringLiteral("cx"), 0.0);
            const double cy = attrNum(tag, QStringLiteral("cy"), 0.0);
            const double rx = attrNum(tag, QStringLiteral("rx"), 0.0);
            const double ry = attrNum(tag, QStringLiteral("ry"), 0.0);
            if (rx > 0.0 && ry > 0.0) {
                target.addEllipse(QPointF(cx, cy), rx, ry);
            }
        }
    }
    return out;
}

} // namespace fg::svg
