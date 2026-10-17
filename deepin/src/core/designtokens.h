// 设计令牌：逐条移植自 design/unified/tokens.css 第 0 / 4 节。
//
// 这里的每一个常量都是「形态」而非「装饰」——设计稿明确标注窗口几何与动效
// 节奏是从上一版逐像素搬过来、四个平台必须一致的，任何一屏都不允许改。
// 颜色不在本文件：颜色一律经 DPalette 语义角色解析（见 theme.h），
// 否则会同时违反设计稿的「主色跟随平台原生色」和 DTK 主题规范。
#ifndef FLOWGRID_DESIGN_TOKENS_H
#define FLOWGRID_DESIGN_TOKENS_H

#include <QtGlobal>

namespace fg::tokens {

// ── 0. 窗口几何 ─────────────────────────────────────────────────────────
inline constexpr int kPanelWidth     = 520;  // --w-panel：三平台完全一致
inline constexpr int kTrayWidth      = 300;  // --w-tray：win 280 ~ mac 320 取中值
inline constexpr int kTitlebarHeight = 34;   // --h-titlebar：mac/win 原值
inline constexpr int kContentPadding = 14;   // --pad-content
inline constexpr int kControlHeight  = 28;   // --h-control
inline constexpr int kIconButtonSize = 26;   // --h-iconbtn
inline constexpr int kSwitchWidth    = 36;   // --w-switch：mac 原值
inline constexpr int kSwitchHeight   = 20;   // --h-switch
inline constexpr int kSwitchKnob     = 16;   // --d-switch

// 浮板最大高度：设计稿为 calc(100vh - 48px)。Qt 侧按实际屏幕可用高度求值，
// 这里只保留那个 48px 的边距常量。
inline constexpr int kPanelScreenMargin = 48;

// ── 1. 动效 ───────────────────────────────────────────────────────────
inline constexpr int kDurMicro = 150;  // --dur-micro：hover / active
inline constexpr int kDurEnter = 220;  // --dur-enter：浮板与托盘入场
inline constexpr int kDurToast = 300;  // --dur-toast
inline constexpr int kDurChart = 500;  // 柱状图高度过渡（ui.css chart__bar）
inline constexpr int kDurCollapse = 200;  // chevron rotate(-90deg)

inline constexpr int kChartHeight  = 70;  // .chart：上一版原值
inline constexpr int kChartBars    = 12;  // 设计稿柱数
inline constexpr int kLatencySampleMs = 2000;  // 每 2 秒采样一次

// ── 2. 间距：统一 4 的倍数 ─────────────────────────────────────────────
inline constexpr int kS1 = 4;
inline constexpr int kS2 = 8;
inline constexpr int kS3 = 12;
inline constexpr int kS4 = 16;
inline constexpr int kS5 = 20;
inline constexpr int kS6 = 24;

// ── 3. 圆角：收敛为 4 级 + pill ───────────────────────────────────────
inline constexpr int kRadiusCtl     = 6;
inline constexpr int kRadiusCard    = 10;
inline constexpr int kRadiusSurface = 12;
// 4 来自设计稿 .seg button 与 .clip__type：控件内的小方块，以及承载一枚小
// 图标的方片。它不在 6/10/12 那条阶梯上，但同样是反复出现的固定形态。
inline constexpr int kRadiusChip    = 4;

// 「药丸」= 高的一半。
//
// 设计稿写的是 --r-pill: 999px，Qt 的样式表不认这个写法：超过半高的值会被
// 引擎夹断，22px 的序号圆传 999 只画出约 2px 的圆角，整个圆直接变成方块
// （角部像素填充从 0.00 变 1.00，肉眼一眼能看出）。所以令牌给的是按高度
// 求值的函数而不是常量：调用点传高度，「药丸」这件事在令牌层就定死了。
constexpr int pillRadius(int height) { return height / 2; }

// ── 4. 排版阶梯：11/12/13/15/20/28，不再出现 10px 以下字号 ─────────────
inline constexpr int kFont11 = 11;
inline constexpr int kFont12 = 12;
inline constexpr int kFont13 = 13;
inline constexpr int kFont15 = 15;
inline constexpr int kFont20 = 20;
inline constexpr int kFont28 = 28;

inline constexpr int kWeightRegular = 400;
inline constexpr int kWeightMedium  = 500;
inline constexpr int kWeightSemi    = 600;
inline constexpr int kWeightBold    = 700;

// ── 5. 组件尺寸 ───────────────────────────────────────────────────────
inline constexpr int kBadgeHeight    = 18;   // .badge
inline constexpr int kBadgeHoriPad   = 7;
inline constexpr int kStatusPillHeight = 20;  // .status-pill
inline constexpr int kStatusPillHoriPad = 8;
inline constexpr int kStatusDotSize  = 7;    // .dot
inline constexpr int kLatChipMinWidth = 56;  // .lat
inline constexpr int kDeviceIconSize = 42;   // .device__icon
inline constexpr int kSwitchBtnSize  = 28;   // .switch-btn__icon
inline constexpr int kStepBadgeSize  = 22;   // 配对流程的序号圆
inline constexpr int kEmptyIconSize  = 40;   // .empty__icon 圆形底

// ── 6. 阴影 ───────────────────────────────────────────────────────────
// 设计稿为 rgba 色值；Qt 侧给 QColor，由 theme 在明暗两套下取值。
inline constexpr int kShadowPanelY = 18;   // 0 18px 42px
inline constexpr int kShadowPanelR = 42;
inline constexpr int kShadowTrayY  = 16;   // 0 16px 32px
inline constexpr int kShadowTrayR  = 32;
inline constexpr int kShadowLiftY  = 2;    // 卡片浮起：0 2px 10px
inline constexpr int kShadowLiftR  = 10;
// 影子带的高度。Web 的 box-shadow 画在控件矩形之外，Qt 画不出界外的像素——
// 所以卡片本体要收到控件上方，下方留这条空带来承接悬停投影。
inline constexpr int kShadowLiftBand = kShadowLiftY + 2;

} // namespace fg::tokens

#endif // FLOWGRID_DESIGN_TOKENS_H
