#include "theme.h"
#include "designtokens.h"
#include "hosttheme.h"

#include <DGuiApplicationHelper>
#include <DPalette>

#include <QGuiApplication>
#include <QPalette>

// DPalette / DGuiApplicationHelper 住在 Dtk::Gui 命名空间。
DGUI_USE_NAMESPACE

namespace fg {
namespace {

// 在给定底色上按比例升降明度，用来从 DPalette 现有角色派生出设计稿需要的
// 中间档（line-1/2/3、hairline、surface-hi）。DTK 没有这些细档语义角色，
// 但派生比硬编码更安全：用户换主题色时派生值仍然跟随。
QColor shade(const QColor &base, qreal amount)
{
    if (!base.isValid()) {
        return base;
    }
    QColor c = base;
    // 约定：正数提亮、负数压暗。
    //
    // 原来写的是 `lighten ? amount : -amount`，负数那一支等于又加了一遍
    // 绝对值——两个符号都往亮里推。于是所有「负数 = 压暗」的调用点全都反了：
    // 浅色主题下 line-3 比 line-2 还浅（边框四档的层次整个翻过来），
    // text2/text3 的「更淡」也是靠这个反号凑出来的。
    const qreal delta = amount >= 0 ? amount : -amount;
    c.setHslF(c.hueF(), c.saturationF(),
              qBound(0.0, c.lightnessF() + (amount >= 0 ? delta : -delta), 1.0),
              c.alphaF());
    return c;
}

// 半透明叠色：等价于 CSS 的 rgb(r g b / a)。
QColor alpha(const QColor &c, qreal a)
{
    QColor r = c;
    r.setAlphaF(qBound(0.0, a, 1.0));
    return r;
}

// 设计稿 tokens.css 第 2 节的明暗中性色。四平台逐像素一致，因此这里只是
// **兜底**：正常情况下这些值由 DPalette 提供（这样能跟随 dde 活动色与字体）。
// 只有当 DPalette 的明暗跟宿主判定对不上时才启用——那说明 DTK 的平台主题
// 插件没跟上宿主（无头后端、缺平台主题插件、或 --theme 强制切换）。
//
// 不兜底的后果不是「看不清」而是「串色」：DPalette 此时整份退化成内置浅色
// 默认值，面是白的、主色变成 Qt 的洋红，在深色桌面上就是一块刺眼的白板。
// 所以兜底时连 accent 一起接管（见下面 accent 那一段）。
void applyNeutralFallback(Palette *p)
{
    if (p->isDark) {
        p->surface      = QColor(0x1e, 0x21, 0x27);
        p->surface3     = QColor(0x2d, 0x32, 0x3c);
        p->surfaceHi    = alpha(QColor(255, 255, 255), 0.055);
        p->surfacePress = alpha(QColor(255, 255, 255), 0.095);
        p->edgeHi       = QColor(255, 255, 255, 18);
        p->hairline     = alpha(QColor(255, 255, 255), 0.05);
        p->line1        = alpha(QColor(255, 255, 255), 0.07);
        p->line2        = alpha(QColor(255, 255, 255), 0.13);
        p->line3        = alpha(QColor(255, 255, 255), 0.22);
        p->text1        = QColor(0xe9, 0xed, 0xf2);
        p->text2        = QColor(0xa2, 0xaa, 0xb6);
        p->text3        = QColor(0x6e, 0x77, 0x83);
        p->accent       = QColor(0, 148, 255);   // tokens.css: deepin 暗色主色
    } else {
        p->surface      = QColor(0xff, 0xff, 0xff);
        p->surface3     = QColor(0xee, 0xf0, 0xf3);   // --gray-100
        p->surfaceHi    = alpha(QColor(16, 24, 40), 0.038);
        p->surfacePress = alpha(QColor(16, 24, 40), 0.075);
        p->edgeHi       = QColor(255, 255, 255, 191);
        p->hairline     = alpha(QColor(16, 24, 40), 0.055);
        p->line1        = alpha(QColor(16, 24, 40), 0.07);
        p->line2        = alpha(QColor(16, 24, 40), 0.12);
        p->line3        = alpha(QColor(16, 24, 40), 0.22);
        p->text1        = QColor(0x17, 0x1b, 0x21);
        p->text2        = QColor(0x5f, 0x68, 0x75);
        p->text3        = QColor(0x8b, 0x93, 0xa0);
        p->accent       = QColor(0, 129, 255);   // tokens.css: deepin 浅色主色
    }
}

} // namespace

Theme::Theme()
{
    refresh();
}

Theme &Theme::instance()
{
    static Theme t;
    return t;
}

Quality Theme::qualityOf(double ms, bool online)
{
    // ms < 0 = 没有读数（还没测到 / 设备离线）。这不是「良好」——
    // 把「没测到」显示成绿色，等于告诉用户链路一切正常。
    if (!online || ms < 0.0) {
        return Quality::Idle;
    }
    // 阈值取自设计稿 latency.html：告警阈值 20ms，且 3.1ms 仍属 good、
    // 11.6ms 属 warn。所以 good/warn 的分界在 4ms（体验开始变差的点），
    // warn/bad 的分界才用 20ms 这个设计稿显式给出的告警阈值。
    // 注意不要沿用旧客户端的 2ms/4ms 分界，那会让 3.4ms 被误判成告警。
    if (ms > 20.0) {
        return Quality::Bad;
    }
    if (ms > 4.0) {
        return Quality::Warn;
    }
    return Quality::Good;
}

StatusColor Theme::color(Quality q) const
{
    switch (q) {
    case Quality::Good:
        return palette_.ok;
    case Quality::Warn:
        return palette_.warn;
    case Quality::Bad:
        return palette_.danger;
    case Quality::Idle:
        break;
    }
    return palette_.idle;
}

void Theme::refresh()
{
    auto *helper = DGuiApplicationHelper::instance();
    const DPalette dtk = helper->applicationPalette();

    // 明暗以 HostTheme 为唯一判据——它才是真正去问 Appearance1 的那个。
    // 早先这里自己去问 helper->themeType()，等于存在两套探测逻辑：无头或
    // 平台主题插件缺席时两者不一致，深色状态色会被配到浅色面上。
    Palette p;
    p.isDark = HostTheme::instance().isDark();

    // DPalette 自己的明暗判定（问 DTK，不问宿主）。
    const bool dtkIsDark = helper->themeType() == DGuiApplicationHelper::DarkType;

    // ── 面 ──────────────────────────────────────────────────────────────
    // QPalette::Window 是 DPalette 里唯一保证不透明的一档，面、页、卡都从它派生。
    p.surface = dtk.color(QPalette::Active, QPalette::Window);

    // DPalette 在「DTK 的平台主题插件没加载」时会**静默退化成内置浅色默认值**，
    // 而 helper->themeType() 仍然照着宿主报 Dark——两边对不上，面和字会一起串色。
    //
    // 实测（探针打印 DPalette 各角色）：
    //   真实显示 DISPLAY=:0（插件已加载，bloom.dark）：
    //     Window #ff252525 / TextTitle #d8ffffff / Highlight #ff024cca（deepin 活动色）
    //   offscreen（插件缺席）：
    //     Window #fff8f8f8 / TextTitle #d8000000 / Highlight #ff0081ff（Qt 默认洋红）
    //
    // 注意退化时**主色也跟着变**——所以兜底必须连 accent 一起接管，
    // 否则面板会变成洋红配深蓝。
    //
    // 判据只有一个：底色明暗跟宿主对不对得上。对不上就别信 DPalette 的这一半，
    // 整组中性色换成设计稿取值（下面 applyNeutralFallback 那条路）。
    const bool surfaceLooksDark = p.surface.lightnessF() < 0.5;
    const bool dtkSelfInconsistent = (surfaceLooksDark != p.isDark);

    const bool dtkFollowsHost = (dtkIsDark == p.isDark) && !HostTheme::instance().isForced()
                                && !dtkSelfInconsistent;

    p.surface3 = dtk.color(QPalette::Active, QPalette::Mid);
    p.surfaceHi = p.isDark ? alpha(p.surface.lighter(115), 0.055)
                           : alpha(QColor(16, 24, 40), 0.038);
    p.surfacePress = p.isDark ? alpha(p.surface.lighter(115), 0.095)
                              : alpha(QColor(16, 24, 40), 0.075);
    p.edgeHi = p.isDark ? QColor(255, 255, 255, 18) : QColor(255, 255, 255, 191);

    // ── 边框：设计稿 hairline .055 < line-1 .07 < line-2 .12 < line-3 .22 ──
    // FrameBorder 是 DTK 官方边框色，落在线-2 附近；其余三档按比例派生，
    // 保证四档相对关系不变。
    p.line2 = dtk.color(DPalette::FrameBorder);

    // 四档边框是同一墨色压在底上的不同透明度（tokens.css：hairline .055 <
    // line-1 .07 < line-2 .12 < line-3 .22），FrameBorder 只给了 line-2 那一档，
    // 其余三档按这个比例推。方向随主题走：浅色底浅，墨越浓边框越明显
    // （更深）；深色底深，墨越浓反而越亮。
    //
    // 「弱的那档更淡」在两个主题下的方向是相反的，所以这两行不是随手写的：
    // light 下 weaker 要更浅（+），dark 下 weaker 要更深（−）。
    if (p.isDark) {
        p.hairline = shade(p.line2, -0.040);
        p.line1 = shade(p.line2, -0.028);
        p.line3 = shade(p.line2, 0.060);
    } else {
        p.hairline = shade(p.line2, 0.050);
        p.line1 = shade(p.line2, 0.035);
        p.line3 = shade(p.line2, -0.050);
    }

    // ── 文字三档 ────────────────────────────────────────────────────────
    p.text1 = dtk.color(DPalette::TextTitle);
    p.text2 = dtk.color(DPalette::TextTips);
    p.text3 = dtk.color(DPalette::PlaceholderText);
    if (!dtkFollowsHost) {
        // 平台主题插件没跟上：中性色整组换成设计稿取值（DPalette 那组来自浅色，
        // 和深色状态色拼在一起是深字白底）。
        applyNeutralFallback(&p);
    }
    // DPalette 的提示色在个别主题下会与标题色过近，这里保底拉开可读性。
    if (p.text2.isValid() && p.text1.isValid() && p.text2 == p.text1) {
        // 次要文字要比标题淡：浅色底上「淡」= 更浅，深色底上「淡」= 更深。
        p.text2 = shade(p.text1, p.isDark ? -0.22 : 0.18);
    }
    if (p.text3.isValid() && p.text2.isValid() && p.text3 == p.text2) {
        p.text3 = shade(p.text2, p.isDark ? -0.22 : 0.16);
    }

    // ── 主色：跟随 deepin 活动色 ───────────────────────────────────────
    // 兜底时 applyNeutralFallback 已经按 tokens.css 给了对应明暗的 deepin 蓝，
    // 这里不能再被 DPalette 的浅色 Highlight 覆盖回去。
    if (dtkFollowsHost) {
        p.accent = dtk.color(QPalette::Active, QPalette::Highlight);
    }
    p.accentText = p.accent;
    if (dtkFollowsHost) {
        p.onAccent = dtk.color(QPalette::Active, QPalette::HighlightedText);
    }
    p.accentSoft = alpha(p.accent, p.isDark ? 0.18 : 0.12);
    p.accentLine = alpha(p.accent, p.isDark ? 0.38 : 0.30);
    if (!p.onAccent.isValid()) {
        p.onAccent = QColor(Qt::white);
    }

    // ── 状态色：四平台一致，取值来自 tokens.css ─────────────────────────
    if (p.isDark) {
        p.ok = {QColor(0x2c, 0xc4, 0x87), QColor(16, 163, 111, 43), QColor(0x5f, 0xd7, 0xa5)};
        p.warn = {QColor(0xf0, 0xa6, 0x3c), QColor(217, 119, 6, 43), QColor(0xf5, 0xc0, 0x78)};
        p.danger = {QColor(0xf2, 0x55, 0x5a), QColor(220, 38, 38, 41), QColor(0xff, 0x8b, 0x8f)};
        p.idle = {QColor(0x7b, 0x84, 0x92), QColor(138, 147, 161, 46), QColor(0x9a, 0xa3, 0xb0)};
        p.onSolid = QColor(0xff, 0xff, 0xff);
        p.shadowPanel = QColor(0, 0, 0, 122);
        p.shadowTray = QColor(0, 0, 0, 112);
        p.shadowLift = QColor(0, 0, 0, 77);
        p.shadowBar = QColor(0, 0, 0, 102);
        p.shadowPop = QColor(0, 0, 0, 133);
        p.toastBg = QColor(46, 51, 60, 245);
        p.toastFg = QColor(0xf2, 0xf5, 0xf8);
    } else {
        p.ok = {QColor(0x10, 0xa3, 0x6f), QColor(16, 163, 111, 31), QColor(0x0a, 0x7d, 0x55)};
        p.warn = {QColor(0xd9, 0x77, 0x06), QColor(217, 119, 6, 31), QColor(0xa3, 0x5a, 0x04)};
        p.danger = {QColor(0xdc, 0x26, 0x26), QColor(220, 38, 38, 28), QColor(0xb9, 0x1c, 0x1c)};
        p.idle = {QColor(0x8a, 0x93, 0xa1), QColor(138, 147, 161, 33), QColor(0x6b, 0x74, 0x80)};
        p.onSolid = QColor(0xff, 0xff, 0xff);
        p.shadowPanel = QColor(24, 30, 44, 36);
        p.shadowTray = QColor(15, 23, 42, 41);
        p.shadowLift = QColor(16, 24, 40, 20);
        p.shadowBar = QColor(15, 23, 42, 46);
        p.shadowPop = QColor(15, 23, 42, 56);
        p.toastBg = QColor(28, 34, 44, 240);
        p.toastFg = QColor(0xff, 0xff, 0xff);
    }

    // ── 卡 / 井 / 条带：与主题无关的层级角色 ────────────────────────────
    // 「卡片比页面亮」这条被钉死：浅色下是白卡浮在灰页上，深色下是略亮的深灰
    // 卡浮在更暗的页上。方向反了（卡片比页面暗）看着就像页面被挖了个洞——
    // 两套主题对同一件事得用同一个方向。
    //
    // 派生只从 p.surface（QPalette::Window）出发，因为它是明暗下唯一保证
    // 不透明的一档。原来还用过 p.surface2（AlternateBase），但那是个
    // 「叠在别人上面的遮罩」：深色下实测是 alpha 14 的黑，拿它去 fill 卡片，
    // 画在 #252525 的页面上算出来是 37×(1−14/255)=35，跟页面几乎一模一样，
    // 等于没画——这就是深色下卡片一直浮不出来的原因。
    //
    // 浅色下白是「高」的一端，所以井要压暗一档；深色下深灰是「低」的一端，
    // 所以井就是原底色、卡要提亮一档。
    if (p.isDark) {
        p.card = shade(p.surface, 0.035);
        p.well = p.surface;
    } else {
        p.card = p.surface;
        p.well = shade(p.surface, -0.070);
    }
    // 三档都是拿来 fill 的底色，半透明会跟底下的东西叠出意外的颜色。
    p.card.setAlpha(255);
    p.well.setAlpha(255);

    // ── surface-2：设计稿 tokens.css 的 --fg-surface-2 ──────────────────
    //
    // 标题栏、页脚、折叠区（.section）、分组标题（.list__group-title）共用这一档。
    // 它必须**比外壳底低半档**（浅色下压暗、深色下提亮），否则这几条带就整个
    // 融进面板底里——浮板看着像一整块白板/黑板，标题栏、页脚、卡片全糊在一层，
    // 这就是「内外不一致」。
    //
    // 取设计稿原值（浅 #f7f8fa / 深 #252932），与 mac 端 FGColor.surface2 同源：
    // 这类中性面是设计稿逐平台一致的值，不跟 DDE 的活动色走。
    //
    // 为什么不用 shade() 派生：派生只能动明度，深色下 surface(#1e2127) 提到
    // #212835 就到头了，跟设计稿的 #252932 还差 3~8 个单位——标题栏会比折叠区
    // 还暗，「条带比面板底亮」这条反而破了。原值没有这个问题。
    p.surface2 = p.isDark ? QColor(0x25, 0x29, 0x32) : QColor(0xf7, 0xf8, 0xfa);
    p.surface2.setAlpha(255);

    palette_ = p;
    Q_EMIT changed();
}

} // namespace fg
