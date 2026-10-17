#include "icons.h"
#include "core/theme.h"
#include "svgsubset.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QStringList>

namespace fg::icons {
namespace {

// ── DDE 图标语言 ──────────────────────────────────────────────────────
//
// 这一套不再是「描边 + 端点圆头」的通用线性图标。三条规矩贯穿全表：
//
// 1. 圆角优先。容器类形体一律 rx=2.2~3，端点圆头圆角。deepin 那一整套图标
//    几乎找不到尖角，这是它和任何 Web 图标库最直观的区别。
// 2. 两层结构。fill="1" 是淡底、fill="2" 是实体，其余是描边，描边最后压上去。
//    纯描边在深色浮板上整排图标会发虚，打一层底之后每枚都像贴了一枚徽章。
// 3. 内部从简。有几枚图标会缩到 12px（列表行首、键帽提示），一格一格的细节
//    在那个尺寸下只会糊成一团，所以键盘只留空格条、警告只留一条感叹号。
//
// 什么时候描边、什么时候只铺底，这条得守住，否则表一多就散了：
//
//   · 「面」——屏幕里的一片画面、格子视图的一块格、机身本身。只铺淡底不描边，
//     描了边它就变成一个盒子，不再像亮着的屏。monitor / laptop / tablet /
//     phone / grid 都属这一类。
//   · 「轮廓」——物件的边界。必须描边，否则浅色主题下整个图标化掉。
//     folder / file / 剪贴板板身 / 图片外框 / 盾牌 / 镜片都属这一类。
//   · 「实体」——形体本身就是内容的那一小块（三角、圆点、机身 home 键），
//     用 fill="2"，按 18% 铺等于没铺。
//
// 网格固定 24×24；视觉安全区约 2~22，主体占 17~19，比通用图标库收半格——
// 线性图标铺满画布看着挤，DDE 图标留白更多。

// 描边宽度。比常见的 1.8 略收：填充层已经补了视觉重量，描边再粗在 12px 下就糊。
constexpr qreal kStrokeWidth = 1.7;

// 填充层相对描边色的透明度。不写死颜色，而是从调用方给的色值压出来——
// 换主题色、换状态色时这一层自动跟着走。
//
// 两档不能合并。底衬（0.18）铺的是「屏幕里的画面」「卡片的面」，要的就是若隐
// 若现；实体（0.55）铺的是「三角形」「圆点」这种形体本身就是内容的元素，
// 按 18% 铺出来在浅色主题下等于没画——第一版就是这样，play / more / grip
// 三枚图标几乎整个消失。
constexpr qreal kFillRatio = 0.18;
constexpr qreal kFillSolid = 0.55;

const QHash<QString, QString> &paths()
{
    static const QHash<QString, QString> table = {
        // ── 方向与操作 ──────────────────────────────────────────────
        {QStringLiteral("chevronDown"),
         QStringLiteral("<path d=\"M6.9 9.7 12 14.8l5.1-5.1\"/>")},
        {QStringLiteral("chevronRight"),
         QStringLiteral("<path d=\"M9.7 6.9 14.8 12l-5.1 5.1\"/>")},
        {QStringLiteral("check"), QStringLiteral("<path d=\"M5.4 12.7 9.9 17.2l8.7-10.4\"/>")},
        {QStringLiteral("close"), QStringLiteral("<path d=\"M6.7 6.7 17.3 17.3M17.3 6.7 6.7 17.3\"/>")},
        {QStringLiteral("plus"), QStringLiteral("<path d=\"M12 6.6v10.8M6.6 12h10.8\"/>")},
        {QStringLiteral("minus"), QStringLiteral("<path d=\"M6.8 12h10.4\"/>")},
        {QStringLiteral("arrowRight"),
         QStringLiteral("<path d=\"M4.6 12h13.2M12.3 6.3 18.3 12l-6 5.7\"/>")},

        // 刷新：环缺口处接一个箭头尖。之前这里是一块实心三角角标，缩到 10px
        // 就糊成一个点，方向也看不出来；开口箭头是线语汇，和描边层是一家人。
        {QStringLiteral("refresh"),
         QStringLiteral("<path d=\"M17.4 6.6A8 8 0 1 1 6.6 17.4\"/>"
                        "<path d=\"M17.7 3.3 18.8 7.8 14.3 6.7\"/>")},
        {QStringLiteral("power"),
         QStringLiteral("<path d=\"M12 3.3v7.4\"/>"
                        "<path d=\"M6.3 6.3a5.7 5.7 0 1 0 11.4 0\"/>")},

        // ── 设备 ──────────────────────────────────────────────────
        // 设备：后屏是台显示器（有底座才认得出是屏），前头一块手机压住它右下角。
        // 两个形体必须真的叠上，否则读成「左边一块右边一块」，不像远近关系。
        {QStringLiteral("devices"),
         QStringLiteral("<rect x=\"2.2\" y=\"3.8\" width=\"13\" height=\"9.6\" rx=\"2.4\"/>"
                        "<rect x=\"2.2\" y=\"3.8\" width=\"13\" height=\"9.6\" rx=\"2.4\""
                        " fill=\"1\"/>"
                        "<path d=\"M8.7 13.4v2.2M6 15.6h5.4\"/>"
                        "<rect x=\"13.4\" y=\"9.4\" width=\"8.4\" height=\"11\" rx=\"2.2\"/>"
                        "<circle cx=\"17.6\" cy=\"17.8\" r=\"1.2\" fill=\"2\"/>")},
        // 键盘：只留一条空格键。四个小圆点在 12px 下会连成一条灰线。
        {QStringLiteral("keyboard"),
         QStringLiteral("<rect x=\"2.4\" y=\"5.8\" width=\"19.2\" height=\"12.4\" rx=\"3\"/>"
                        "<rect x=\"7.6\" y=\"13.4\" width=\"8.8\" height=\"2.6\" rx=\"1.3\""
                        " fill=\"1\"/>")},
        {QStringLiteral("monitor"),
         QStringLiteral("<rect x=\"2.4\" y=\"3.6\" width=\"19.2\" height=\"13\" rx=\"2.6\""
                        " fill=\"1\"/>"
                        "<path d=\"M12 16.6v3.4M8.4 20.4h7.2\"/>")},
        {QStringLiteral("laptop"),
         QStringLiteral("<rect x=\"3.6\" y=\"5\" width=\"16.8\" height=\"11.2\" rx=\"2.4\""
                        " fill=\"1\"/>"
                        "<rect x=\"2\" y=\"18.2\" width=\"20\" height=\"2.8\" rx=\"1.4\"/>")},
        {QStringLiteral("tablet"),
         QStringLiteral("<rect x=\"4.4\" y=\"2.4\" width=\"15.2\" height=\"19.2\" rx=\"3\""
                        " fill=\"1\"/>"
                        "<circle cx=\"12\" cy=\"18.5\" r=\"1.3\" fill=\"2\"/>")},
        {QStringLiteral("phone"),
         QStringLiteral("<rect x=\"6.4\" y=\"2.4\" width=\"11.2\" height=\"19.2\" rx=\"3\""
                        " fill=\"1\"/>"
                        "<circle cx=\"12\" cy=\"18.5\" r=\"1.3\" fill=\"2\"/>")},

        // ── 网络 ──────────────────────────────────────────────────
        // WiFi：两段弧 + 一个实心点。椭圆弧（ry 小）让弧顶收在画布内，
        // 三段同心弧在这个尺寸下必然贴在一起。
        {QStringLiteral("wifi"),
         QStringLiteral("<path d=\"M4.4 10.2a11 7 0 0 1 15.2 0\"/>"
                        "<path d=\"M8.2 15.4a5.6 4.6 0 0 1 7.6 0\"/>"
                        "<circle cx=\"12\" cy=\"19.2\" r=\"1.8\" fill=\"2\"/>")},
        // 局域网：两个节点 + 一根连线，中点留空表示「可切换」。
        {QStringLiteral("lan"),
         QStringLiteral("<rect x=\"2.4\" y=\"13.2\" width=\"7.2\" height=\"7.2\" rx=\"2.2\""
                        " fill=\"1\"/>"
                        "<rect x=\"14.4\" y=\"3.6\" width=\"7.2\" height=\"7.2\" rx=\"2.2\"/>"
                        "<path d=\"M8.6 15.4 15.4 8.6\"/>")},
        {QStringLiteral("activity"),
         QStringLiteral("<path d=\"M3 12.5h3.5l2.9-7.3 3.9 14.6 2.9-10.4 2 3.1h3.3\"/>")},

        // ── 数据与同步 ────────────────────────────────────────────
        // 剪贴板：板上带两条内容行。不画内容的话 12px 下就是一块空板。
        {QStringLiteral("clipboard"),
         QStringLiteral("<rect x=\"4.4\" y=\"4.8\" width=\"15.2\" height=\"15.8\" rx=\"3\"/>"
                        "<rect x=\"8.6\" y=\"2.6\" width=\"6.8\" height=\"4\" rx=\"2\""
                        " fill=\"2\"/>"
                        "<rect x=\"8\" y=\"11.4\" width=\"8\" height=\"1.7\" rx=\".85\""
                        " fill=\"1\"/>"
                        "<rect x=\"8\" y=\"15.2\" width=\"5.2\" height=\"1.7\" rx=\".85\""
                        " fill=\"1\"/>")},
        {QStringLiteral("file"),
         QStringLiteral("<path d=\"M7 3.4h6.9l5 5v12.2a1.9 1.9 0 0 1-1.9 1.9H7a1.9 1.9"
                        " 0 0 1-1.9-1.9V5.3A1.9 1.9 0 0 1 7 3.4z\"/>"
                        "<path d=\"M13.4 3.6v4.9h5\"/>"
                        "<rect x=\"8\" y=\"12.4\" width=\"7\" height=\"1.6\" rx=\".8\""
                        " fill=\"1\"/>"
                        "<rect x=\"8\" y=\"16\" width=\"4.4\" height=\"1.6\" rx=\".8\""
                        " fill=\"1\"/>")},
        {QStringLiteral("folder"),
         QStringLiteral("<path d=\"M3.4 6.9a2.3 2.3 0 0 1 2.3-2.3h3.5l2.1 2.4h7a2.3"
                        " 2.3 0 0 1 2.3 2.3v8.5a2.3 2.3 0 0 1-2.3 2.3H5.7a2.3"
                        " 2.3 0 0 1-2.3-2.3z\"/>"
                        "<path d=\"M3.4 6.9a2.3 2.3 0 0 1 2.3-2.3h3.5l2.1 2.4h7a2.3"
                        " 2.3 0 0 1 2.3 2.3v8.5a2.3 2.3 0 0 1-2.3 2.3H5.7a2.3"
                        " 2.3 0 0 1-2.3-2.3z\" fill=\"1\"/>")},
        {QStringLiteral("upload"),
         QStringLiteral("<path d=\"M4.4 15v3.4a2 2 0 0 0 2 2h11.2a2 2 0 0 0 2-2V15\"/>"
                        "<path d=\"M12 16.2V3.8M7.4 8.4 12 3.8l4.6 4.6\"/>")},
        {QStringLiteral("download"),
         QStringLiteral("<path d=\"M4.4 15v3.4a2 2 0 0 0 2 2h11.2a2 2 0 0 0 2-2V15\"/>"
                        "<path d=\"M12 3.8v12.4M7.4 11.6 12 16.2l4.6-4.6\"/>")},
        {QStringLiteral("trash"),
         QStringLiteral("<path d=\"M4.2 6.4h15.6\"/>"
                        "<path d=\"M9.4 6.4V4.8a1.7 1.7 0 0 1 1.7-1.7h1.8a1.7 1.7 0 0 1"
                        " 1.7 1.7v1.6\"/>"
                        "<path d=\"M6.3 6.4l1 12.8a1.9 1.9 0 0 0 1.9 1.8h5.6a1.9 1.9 0"
                        " 0 0 0 1.9-1.8l1-12.8\"/>"
                        "<path d=\"M10.2 10.2v6.6M13.8 10.2v6.6\"/>")},
        {QStringLiteral("edit"),
         QStringLiteral("<path d=\"M15.9 4.1a2.3 2.3 0 0 1 3.3 3.3L7.6 18.9 3 20.2"
                        "l1.3-4.6z\"/>"
                        "<path d=\"M4.2 20.6h15.6\"/>")},
        {QStringLiteral("copy"),
         QStringLiteral("<rect x=\"8.8\" y=\"8.8\" width=\"11.8\" height=\"11.8\" rx=\"2.6\""
                        " fill=\"1\"/>"
                        "<rect x=\"3.4\" y=\"3.4\" width=\"11.8\" height=\"11.8\" rx=\"2.6\"/>")},
        {QStringLiteral("image"),
         QStringLiteral("<rect x=\"3.2\" y=\"4.4\" width=\"17.6\" height=\"15.2\" rx=\"2.8\"/>"
                        "<rect x=\"3.2\" y=\"4.4\" width=\"17.6\" height=\"15.2\" rx=\"2.8\""
                        " fill=\"1\"/>"
                        "<circle cx=\"8.2\" cy=\"8.8\" r=\"1.9\" fill=\"2\"/>"
                        "<path d=\"M4.6 18.2 9.9 12.2 13.4 15.8 16.4 12.7 20.2 18.2z\"/>")},
        {QStringLiteral("type"), QStringLiteral("<path d=\"M5.4 4.8h13.2M12 4.8v14.6M8.8 19.4h6.4\"/>")},
        {QStringLiteral("code"), QStringLiteral("<path d=\"M8.8 8.2 4.2 12l4.6 3.8M15.2 8.2 19.8 12l-4.6 3.8\"/>")},
        {QStringLiteral("search"),
         QStringLiteral("<circle cx=\"10.8\" cy=\"10.8\" r=\"6.2\"/>"
                        "<circle cx=\"10.8\" cy=\"10.8\" r=\"6.2\" fill=\"1\"/>"
                        "<path d=\"M15.3 15.3 20 20\"/>")},

        // ── 系统与状态 ────────────────────────────────────────────
        // 齿轮：外环 + 八根放射齿。通用图标库那条几十段的复合路径在 12px 下
        // 会糊成一坨黑，放射齿既清楚又是 deepin 那种几何感。
        {QStringLiteral("settings"),
         QStringLiteral("<circle cx=\"12\" cy=\"12\" r=\"7.2\"/>"
                        "<circle cx=\"12\" cy=\"12\" r=\"3.1\" fill=\"2\"/>"
                        "<path d=\"M19.2 12h2.4M12 19.2v2.4M4.8 12H2.4M12 4.8V2.4"
                        "M17.3 17.3l1.7 1.7M6.7 17.3 5 19M17.3 6.7 19 5M6.7 6.7 5 5\"/>")},
        {QStringLiteral("more"),
         QStringLiteral("<circle cx=\"5.6\" cy=\"12\" r=\"1.75\" fill=\"2\"/>"
                        "<circle cx=\"12\" cy=\"12\" r=\"1.75\" fill=\"2\"/>"
                        "<circle cx=\"18.4\" cy=\"12\" r=\"1.75\" fill=\"2\"/>")},
        {QStringLiteral("info"),
         QStringLiteral("<circle cx=\"12\" cy=\"12\" r=\"9.2\"/>"
                        "<path d=\"M12 11.2v5.4\"/>"
                        "<circle cx=\"12\" cy=\"8\" r=\"1.2\" fill=\"2\"/>")},
        // 警告：三角形铺一层淡底。不铺的话那根感叹号是悬空的一竖，12px 下直接
        // 看不见——它得有个面才立得住。
        {QStringLiteral("warn"),
         QStringLiteral("<path d=\"M12 3.8 21.4 20.2H2.6z\"/>"
                        "<path d=\"M12 3.8 21.4 20.2H2.6z\" fill=\"1\"/>"
                        "<path d=\"M12 9.4v4.6\"/>"
                        "<circle cx=\"12\" cy=\"16.9\" r=\"1.2\" fill=\"2\"/>")},
        {QStringLiteral("shield"),
         QStringLiteral("<path d=\"M12 2.8 19.4 5.6v6.2c0 4.6-3.1 7.7-7.4 8.4"
                        "-4.3-.7-7.4-3.8-7.4-8.4V5.6z\"/>"
                        "<path d=\"M12 2.8 19.4 5.6v6.2c0 4.6-3.1 7.7-7.4 8.4"
                        "-4.3-.7-7.4-3.8-7.4-8.4V5.6z\" fill=\"1\"/>"
                        "<path d=\"M8.9 12.1 11 14.2l4.3-4.3\"/>")},
        {QStringLiteral("grid"),
         QStringLiteral("<rect x=\"3.6\" y=\"3.6\" width=\"6.8\" height=\"6.8\" rx=\"2.2\""
                        " fill=\"1\"/>"
                        "<rect x=\"13.6\" y=\"3.6\" width=\"6.8\" height=\"6.8\" rx=\"2.2\""
                        " fill=\"1\"/>"
                        "<rect x=\"3.6\" y=\"13.6\" width=\"6.8\" height=\"6.8\" rx=\"2.2\""
                        " fill=\"1\"/>"
                        "<rect x=\"13.6\" y=\"13.6\" width=\"6.8\" height=\"6.8\" rx=\"2.2\""
                        " fill=\"1\"/>")},
        {QStringLiteral("link"),
         QStringLiteral("<path d=\"M10.4 13.6a4.5 4.5 0 0 0 6.8.45l2.4-2.4a4.5 4.5 0"
                        " 0 0-6.36-6.36l-1.38 1.38\"/>"
                        "<path d=\"M13.6 10.4a4.5 4.5 0 0 0-6.8-.45l-2.4 2.4a4.5 4.5"
                        " 0 0 0 6.36 6.36l1.38-1.38\"/>")},
        {QStringLiteral("grip"),
         QStringLiteral("<circle cx=\"9.6\" cy=\"7\" r=\"1.7\" fill=\"2\"/>"
                        "<circle cx=\"14.4\" cy=\"7\" r=\"1.7\" fill=\"2\"/>"
                        "<circle cx=\"9.6\" cy=\"12\" r=\"1.7\" fill=\"2\"/>"
                        "<circle cx=\"14.4\" cy=\"12\" r=\"1.7\" fill=\"2\"/>"
                        "<circle cx=\"9.6\" cy=\"17\" r=\"1.7\" fill=\"2\"/>"
                        "<circle cx=\"14.4\" cy=\"17\" r=\"1.7\" fill=\"2\"/>")},
        {QStringLiteral("play"),
         QStringLiteral("<path d=\"M8.6 5.9a.9.9 0 0 1 1.35-.78l8.1 5.4a.9.9 0 0 1 0"
                        " 1.56l-8.1 5.4a.9.9 0 0 1-1.35-.78z\" fill=\"2\"/>")},
    };
    return table;
}

} // namespace

bool exists(const QString &name)
{
    return paths().contains(name);
}

QStringList names()
{
    QStringList out = paths().keys();
    out.sort();
    return out;
}

QIcon icon(const QString &name, int size, const QColor &color)
{
    const auto it = paths().constFind(name);
    if (it == paths().constEnd() || size <= 0) {
        return {};
    }
    const QColor stroke = color.isValid() ? color : Theme::instance().palette().text1;

    // 24 网格 → 目标像素；2 倍渲染再交给 Qt 缩放，高分屏下 1.7 描边才不糊。
    const qreal dpr = 2.0;
    const qreal edge = size * dpr;
    const qreal scale = edge / 24.0;

    QPixmap pm(QSize(qRound(edge), qRound(edge)));
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(scale, scale);
        const svg::Icon g = svg::parse(it.value());

        // 两层填充依次铺：底衬在下、实体在上，都从当前色压透明度，不引入第二个
        // 颜色。派生而不是写死，是为了用户换主题色 / 状态色时这两层跟着走，
        // 跟 theme.cpp 里派生 line-1/2/3 是同一条理由。
        const auto paintFill = [&p, stroke](const QPainterPath &path, qreal ratio) {
            if (path.isEmpty()) {
                return;
            }
            QColor fill = stroke;
            fill.setAlphaF(fill.alphaF() * ratio);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawPath(path);
        };
        paintFill(g.fill, kFillRatio);
        paintFill(g.solid, kFillSolid);

        QPen pen(stroke);
        pen.setWidthF(kStrokeWidth);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(g.stroke);
    }
    pm.setDevicePixelRatio(dpr);

    QIcon result;
    result.addPixmap(pm);
    return result;
}

} // namespace fg::icons
