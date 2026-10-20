// 图标集：DDE 风格自绘的一枚一套 SVG path。
//
// 造型规矩写在 icons.cpp 的表头上，简短版：圆角优先、「淡色块打底 + 实描边
// 压住」两层、内部从简（有几枚会缩到 12px）。不取 DTK 主题图标——那是另一套
// 视觉语言，和本项目的圆角卡片语汇不搭；也不走通用 Web 图标库，那套纯线性
// 在深色浮板上整排发虚。
#ifndef FLOWGRID_ICONS_H
#define FLOWGRID_ICONS_H

#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>

namespace fg::icons {

// 取图标。size 为边长（像素）；color 为描边色，传入无效色则用当前调色板文字色。
// 内部按 2x 渲染再交给 Qt 缩放，避免高分屏下描边发虚。
QIcon icon(const QString &name, int size, const QColor &color);

// 图标是否存在。调用方给的图标名不在表里时它会静默返回空 QIcon，界面上就
// 是一块什么都没有的方块——排查这类问题时先查这里。
bool exists(const QString &name);

// 全部图标名，按字母序。给 --snapshot 的图标总览图用。
QStringList names();

// 应用徽标（托盘 / 窗口图标）。
//
// 形状与颜色都取自内嵌的 resources/icons/flowgrid.svg——那是唯一真相源，
// 改了 SVG 这里自动跟着变，不会出现「装出去的图标和托盘里的不是一枚」。
// 用本项目自己的 SVG 子集渲染器画，不依赖 QtSvg 插件（那台机器上 Qt6Svg
// 只有运行时库、没有开发包，QSvgRenderer 根本用不了）。
//
// 返回空位图表示内嵌资源读不到或画不出来，调用方据此走下一档候选。
QPixmap logoPixmap(int size);

} // namespace fg::icons

#endif // FLOWGRID_ICONS_H
