// 图标集：直接取自 design/unified/app.js 的 P 表，逐条照搬，不重画。
//
// 设计稿把图标从上一版的 emoji/字符统一成「24 网格 + 1.75 描边 + 圆头圆角」
// 的一套 SVG，并要求四个平台同一套。因此这里不做重绘，只把 path 数据搬进
// C++，运行时包成 QIcon——这样 deepin 端的图标与设计稿逐像素一致，
// 也不会引入 DTK 主题图标里另一套风格。
#ifndef FLOWGRID_ICONS_H
#define FLOWGRID_ICONS_H

#include <QIcon>
#include <QString>

namespace fg::icons {

// 取图标。size 为边长（像素）；color 为描边色，传入无效色则用当前调色板文字色。
// 内部按 2x 渲染再交给 Qt 缩放，避免高分屏下描边发虚。
QIcon icon(const QString &name, int size, const QColor &color);

// 只取本轮三块界面用到的图标；其余（pairing/keymap/files 等屏）后续按需补。
bool exists(const QString &name);

} // namespace fg::icons

#endif // FLOWGRID_ICONS_H
