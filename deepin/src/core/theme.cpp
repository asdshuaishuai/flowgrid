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
    const bool lighten = amount > 0;
    c.setHslF(c.hueF(), c.saturationF(),
              qBound(0.0, c.lightnessF() + (lighten ? amount : -amount), 1.0),
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
// 只有当 DPalette 的明暗与宿主判定的明暗对不上时才启用——那说明 DTK 的平台
// 主题插件没跟上宿主（无头后端、缺平台主题插件、或 --theme 强制切换）。
// 直接沿用 DPalette 的浅色值配深色的状态色，会得到「深色文字画在白底上」。
void applyNeutralFallback(Palette *p)
{
    if (p->isDark) {
        p->surface      = QColor(0x1e, 0x21, 0x27);
        p->surface2     = QColor(0x25, 0x29, 0x32);
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
        p->surface2     = QColor(0xf6, 0xf7, 0xf9);   // --gray-50
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
    if (!online) {
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

    // DPalette 的明暗与宿主判定是否一致？不一致说明 DTK 平台主题插件没跟上
    // 宿主（无头后端、插件缺失、或 --theme 强制切换），此时它的中性色不可信。
    const bool dtkIsDark = helper->themeType() == DGuiApplicationHelper::DarkType;
    const bool dtkFollowsHost = (dtkIsDark == p.isDark) && !HostTheme::instance().isForced();

    // ── 面 ──────────────────────────────────────────────────────────────
    // DPalette 的 ItemBackground 是 DTK 为「卡片/列表项」准备的角色，
    // 比 QPalette::Base 更贴近设计稿的卡片底。
    p.surface = dtk.color(QPalette::Active, QPalette::Window);
    p.surface2 = dtk.color(QPalette::Active, QPalette::AlternateBase);
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
    p.line1 = shade(p.line2, p.isDark ? 0.02 : -0.015);
    p.hairline = shade(p.line2, p.isDark ? 0.005 : -0.01);
    p.line3 = shade(p.line2, p.isDark ? 0.06 : -0.08);

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
        p.text2 = shade(p.text1, p.isDark ? -0.22 : -0.18);
    }
    if (p.text3.isValid() && p.text2.isValid() && p.text3 == p.text2) {
        p.text3 = shade(p.text2, p.isDark ? -0.22 : -0.16);
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
        p.shadowPanel = QColor(0, 0, 0, 122);
        p.shadowTray = QColor(0, 0, 0, 112);
        p.shadowLift = QColor(0, 0, 0, 77);
    } else {
        p.ok = {QColor(0x10, 0xa3, 0x6f), QColor(16, 163, 111, 31), QColor(0x0a, 0x7d, 0x55)};
        p.warn = {QColor(0xd9, 0x77, 0x06), QColor(217, 119, 6, 31), QColor(0xa3, 0x5a, 0x04)};
        p.danger = {QColor(0xdc, 0x26, 0x26), QColor(220, 38, 38, 28), QColor(0xb9, 0x1c, 0x1c)};
        p.idle = {QColor(0x8a, 0x93, 0xa1), QColor(138, 147, 161, 33), QColor(0x6b, 0x74, 0x80)};
        p.shadowPanel = QColor(24, 30, 44, 36);
        p.shadowTray = QColor(15, 23, 42, 41);
        p.shadowLift = QColor(16, 24, 40, 20);
    }

    palette_ = p;
    Q_EMIT changed();
}

} // namespace fg
