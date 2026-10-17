// 极小 SVG 子集渲染器。
//
// 为什么自己写：本机的 Qt6Svg 只有运行时库、没有开发包，QSvgRenderer 不可用；
// 而图标是「四平台逐像素一致」的硬要求，把路径串作为唯一真相源直接渲染，
// 比把每条路径手工译成 QPainterPath 更不容易走样。
//
// 只实现图标真正用到的子集：<path> 的 M/L/H/V/C/S/A/Z（含小写相对形式）
// 与 <rect rx> / <circle> / <ellipse>。够用即止，不做通用 SVG 解析器。
#ifndef FLOWGRID_SVG_SUBSET_H
#define FLOWGRID_SVG_SUBSET_H

#include <QPainterPath>

namespace fg::svg {

// 一枚图标的两层几何。
//
// DDE 的图标语言不是纯线性描边，而是「一层很淡的色块打底 + 一层实描边压住」：
// 控制中心、网络、声音那一整套图标都是这个结构，这才认得出是 deepin，而不是
// 任何一套通用 Web 图标库。纯描边的话在深色浮板上整排图标会发虚。
//
// 约定：fill="1" 进底衬层、fill="2" 进实体层、不带 fill 进描边层。填充色由调用方
// 从描边色压透明度派生（见 icons.cpp），所以图标表里不写死任何颜色。
struct Icon {
    QPainterPath stroke;   // 实描边
    QPainterPath fill;     // 底衬：屏幕内容、卡片面这类大面积形体，淡
    QPainterPath solid;    // 实体：文件夹、三角、圆点这类「形体本身」，要看得见
};

// 把一个 SVG 片段（可含多个 <path>/<rect>/<circle>/<ellipse>）拆成描边层与
// 填充层。viewBox 固定按 24×24 处理——图标全部是 24 网格。
// 未识别的元素会被忽略而不是报错：图标集是静态表，宁可少画也不要崩。
Icon parse(const QString &fragment);

// 24×24 视口下的点/尺寸解析，供单元测试直接验证弧线换算等易错逻辑。
QPainterPath parsePathData(const QString &d);

} // namespace fg::svg

#endif // FLOWGRID_SVG_SUBSET_H
