// 极小 SVG 子集渲染器。
//
// 为什么自己写：本机的 Qt6Svg 只有运行时库、没有开发包，QSvgRenderer 不可用；
// 而设计稿的图标是「四平台逐像素一致」的硬要求，把 app.js 里的 d 字符串作为
// 唯一真相源直接渲染，比把每条路径手工译成 QPainterPath 更不容易走样。
//
// 只实现设计稿图标真正用到的子集：<path> 的 M/L/H/V/C/S/A/Z（含小写相对形式）
// 与 <rect rx> / <circle> / <ellipse>。够用即止，不做通用 SVG 解析器。
#ifndef FLOWGRID_SVG_SUBSET_H
#define FLOWGRID_SVG_SUBSET_H

#include <QPainterPath>

namespace fg::svg {

// 把一个 SVG 片段（可含多个 <path>/<rect>/<circle>/<ellipse>）转成可描边的路径。
// viewBox 固定按 24×24 处理——设计稿的 .ic 全部是 24 网格。
// 未识别的元素会被忽略而不是报错：图标集是静态表，宁可少画也不要崩。
QPainterPath parse(const QString &fragment);

// 24×24 视口下的点/尺寸解析，供单元测试直接验证弧线换算等易错逻辑。
QPainterPath parsePathData(const QString &d);

} // namespace fg::svg

#endif // FLOWGRID_SVG_SUBSET_H
