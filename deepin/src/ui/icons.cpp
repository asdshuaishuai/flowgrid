#include "icons.h"
#include "core/theme.h"
#include "svgsubset.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>

namespace fg::icons {
namespace {

// 与 app.js 的 P 表逐条一致：key 为图标名，value 为 SVG 子元素。
const QHash<QString, QString> &paths()
{
    static const QHash<QString, QString> table = {
        {QStringLiteral("chevronDown"), QStringLiteral("<path d=\"M6 9l6 6 6-6\"/>")},
        {QStringLiteral("check"), QStringLiteral("<path d=\"M20 6L9 17l-5-5\"/>")},
        {QStringLiteral("close"), QStringLiteral("<path d=\"M18 6L6 18M6 6l12 12\"/>")},
        {QStringLiteral("plus"), QStringLiteral("<path d=\"M12 5v14M5 12h14\"/>")},
        {QStringLiteral("minus"), QStringLiteral("<path d=\"M5 12h14\"/>")},
        {QStringLiteral("arrowRight"), QStringLiteral("<path d=\"M5 12h14M13 6l6 6-6 6\"/>")},
        {QStringLiteral("refresh"), QStringLiteral("<path d=\"M21 12a9 9 0 1 1-3-6.7\"/><path d=\"M21 4v5h-5\"/>")},
        {QStringLiteral("power"), QStringLiteral("<path d=\"M12 3v9\"/><path d=\"M18.4 6.6a9 9 0 1 1-12.8 0\"/>")},
        {QStringLiteral("devices"), QStringLiteral("<rect x=\"2\" y=\"5\" width=\"13\" height=\"9\" rx=\"1.5\"/><rect x=\"15\" y=\"9\" width=\"7\" height=\"10\" rx=\"1.5\"/><path d=\"M6 18h5\"/>")},
        {QStringLiteral("keyboard"), QStringLiteral("<rect x=\"2\" y=\"6\" width=\"20\" height=\"12\" rx=\"2\"/><path d=\"M6 10h.01M10 10h.01M14 10h.01M18 10h.01M7 14h10\"/>")},
        {QStringLiteral("clipboard"), QStringLiteral("<rect x=\"6\" y=\"4\" width=\"12\" height=\"4\" rx=\"1\"/><path d=\"M6 6H5a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1V7a1 1 0 0 0-1-1h-1\"/>")},
        {QStringLiteral("activity"), QStringLiteral("<path d=\"M3 12h4l3-8 4 16 3-8h4\"/>")},
        {QStringLiteral("settings"), QStringLiteral("<circle cx=\"12\" cy=\"12\" r=\"3\"/><path d=\"M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-2.9 1.2v.2a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.6 1.7 1.7 0 0 0-1.9.4l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0-1.2-2.9H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.6-1.1 1.7 1.7 0 0 0-.4-1.9l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.9.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 2.9 1.2l-.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.9V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z\"/>")},
        {QStringLiteral("monitor"), QStringLiteral("<rect x=\"2\" y=\"3\" width=\"20\" height=\"13\" rx=\"2\"/><path d=\"M8 20h8M12 16v4\"/>")},
        {QStringLiteral("laptop"), QStringLiteral("<rect x=\"3\" y=\"5\" width=\"18\" height=\"11\" rx=\"2\"/><path d=\"M2 19h20\"/>")},
        {QStringLiteral("tablet"), QStringLiteral("<rect x=\"4\" y=\"2\" width=\"16\" height=\"20\" rx=\"2\"/><path d=\"M11 18h2\"/>")},
        {QStringLiteral("phone"), QStringLiteral("<rect x=\"6\" y=\"2\" width=\"12\" height=\"20\" rx=\"2.5\"/><path d=\"M11 18h2\"/>")},
        {QStringLiteral("wifi"), QStringLiteral("<path d=\"M5 12.5a10 10 0 0 1 14 0\"/><path d=\"M8.5 16a5 5 0 0 1 7 0\"/><path d=\"M12 19.5h.01\"/>")},
        {QStringLiteral("lan"), QStringLiteral("<path d=\"M9 15l6-6\"/><rect x=\"2\" y=\"13\" width=\"6\" height=\"6\" rx=\"1\"/><rect x=\"16\" y=\"3\" width=\"6\" height=\"6\" rx=\"1\"/>")},
        {QStringLiteral("more"), QStringLiteral("<circle cx=\"5\" cy=\"12\" r=\"1.2\"/><circle cx=\"12\" cy=\"12\" r=\"1.2\"/><circle cx=\"19\" cy=\"12\" r=\"1.2\"/>")},
        {QStringLiteral("info"), QStringLiteral("<circle cx=\"12\" cy=\"12\" r=\"9\"/><path d=\"M12 16v-4M12 8h.01\"/>")},
        {QStringLiteral("warn"), QStringLiteral("<path d=\"M10.3 3.9 2.4 18a2 2 0 0 0 1.7 3h15.8a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z\"/><path d=\"M12 9v4M12 17h.01\"/>")},
        {QStringLiteral("file"), QStringLiteral("<path d=\"M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z\"/><path d=\"M14 3v5h5\"/>")},
        {QStringLiteral("upload"), QStringLiteral("<path d=\"M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4\"/><path d=\"M12 3v12\"/><path d=\"M7 8l5-5 5 5\"/>")},
        {QStringLiteral("download"), QStringLiteral("<path d=\"M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4\"/><path d=\"M12 15V3\"/><path d=\"M7 10l5 5 5-5\"/>")},
        {QStringLiteral("folder"), QStringLiteral("<path d=\"M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z\"/>")},
        {QStringLiteral("trash"), QStringLiteral("<path d=\"M4 7h16M10 11v6M14 11v6\"/><path d=\"M6 7l1 13h10l1-13M9 7V4h6v3\"/>")},
        {QStringLiteral("edit"), QStringLiteral("<path d=\"M12 20h9\"/><path d=\"M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z\"/>")},
        {QStringLiteral("grip"), QStringLiteral("<circle cx=\"9\" cy=\"6\" r=\"1\"/><circle cx=\"15\" cy=\"6\" r=\"1\"/><circle cx=\"9\" cy=\"12\" r=\"1\"/><circle cx=\"15\" cy=\"12\" r=\"1\"/><circle cx=\"9\" cy=\"18\" r=\"1\"/><circle cx=\"15\" cy=\"18\" r=\"1\"/>")},
        {QStringLiteral("play"), QStringLiteral("<path d=\"M7 4l12 8-12 8z\"/>")},
        {QStringLiteral("copy"), QStringLiteral("<rect x=\"9\" y=\"9\" width=\"12\" height=\"12\" rx=\"2\"/><path d=\"M5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1\"/>")},
        {QStringLiteral("image"), QStringLiteral("<rect x=\"3\" y=\"4\" width=\"18\" height=\"16\" rx=\"2\"/><circle cx=\"8.5\" cy=\"9.5\" r=\"1.5\"/><path d=\"M21 16l-5-5-9 9\"/>")},
        {QStringLiteral("type"), QStringLiteral("<path d=\"M4 6V4h16v2M12 4v16M9 20h6\"/>")},
        {QStringLiteral("code"), QStringLiteral("<path d=\"M8 6l-5 6 5 6M16 6l5 6-5 6\"/>")},
        {QStringLiteral("grid"), QStringLiteral("<rect x=\"3\" y=\"3\" width=\"7\" height=\"7\" rx=\"1.5\"/><rect x=\"14\" y=\"3\" width=\"7\" height=\"7\" rx=\"1.5\"/><rect x=\"3\" y=\"14\" width=\"7\" height=\"7\" rx=\"1.5\"/><rect x=\"14\" y=\"14\" width=\"7\" height=\"7\" rx=\"1.5\"/>")},
        {QStringLiteral("link"), QStringLiteral("<path d=\"M10 13a5 5 0 0 0 7.5.5l3-3a5 5 0 0 0-7-7l-1.7 1.7\"/><path d=\"M14 11a5 5 0 0 0-7.5-.5l-3 3a5 5 0 0 0 7 7l1.7-1.7\"/>")},
        {QStringLiteral("shield"), QStringLiteral("<path d=\"M12 3l8 3v6c0 5-3.5 8.3-8 9-4.5-.7-8-4-8-9V6z\"/><path d=\"M9 12l2 2 4-4\"/>")},
    };
    return table;
}

} // namespace

bool exists(const QString &name)
{
    return paths().contains(name);
}

QIcon icon(const QString &name, int size, const QColor &color)
{
    const auto it = paths().constFind(name);
    if (it == paths().constEnd() || size <= 0) {
        return {};
    }
    const QColor stroke = color.isValid() ? color : Theme::instance().palette().text1;

    // 24 网格 → 目标像素；2 倍渲染再交给 Qt 缩放，高分屏下 1.75 描边才不糊。
    const qreal dpr = 2.0;
    const qreal edge = size * dpr;
    const qreal scale = edge / 24.0;

    QPixmap pm(QSize(qRound(edge), qRound(edge)));
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(scale, scale);
        // 描边参数照抄 ui.css 的 .ic：1.75 描边、圆头圆角。
        QPen pen(stroke);
        pen.setWidthF(1.75);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(svg::parse(it.value()));
    }
    pm.setDevicePixelRatio(dpr);

    QIcon result;
    result.addPixmap(pm);
    return result;
}

} // namespace fg::icons
