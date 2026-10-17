// 主题层：设计令牌 → DTK 语义色。
//
// 取色规则（AGENTS.md「优先 DPalette 语义角色，禁止硬编码主题色」）：
//   · 主色 / 面 / 文字 / 边框 一律走 DPalette + QPalette 语义角色，
//     因此自动跟随 dde 的亮暗主题与用户自选活动色，无需本客户端判断。
//   · 状态色（正常/告警/危险/离线）是设计稿里四平台逐像素一致的值，
//     刻意不进 DPalette——DTK 没有 success/danger 语义角色，若用 Highlight
//     推导会让同一个状态在四个平台显示不同颜色，违反设计稿平台映射规则
//     （index.html：只有主色与字体族两类值保留平台映射）。
//   · 明暗两套取值来自 tokens.css 第 2b 节，不是拍脑袋。
#ifndef FLOWGRID_THEME_H
#define FLOWGRID_THEME_H

#include <QColor>
#include <QObject>

namespace fg {

// 延迟分级。阈值取自设计稿 latency.html 的显式告警线 20ms：≤4ms 良好、
// 4~20ms 偏慢、>20ms 告警。注意**不要**沿用旧客户端的 2/4ms——那套阈值会把
// 同一份设计稿里的 3ms 正常读数误判成告警。
enum class Quality { Good, Warn, Bad, Idle };

// 一个语义色的三档：实色 / 浅底 / 文字色。
struct StatusColor {
    QColor solid;
    QColor soft;
    QColor text;
};

// 一屏用到的全部语义色。改这里等于改全应用配色，故集中一处。
struct Palette {
    // 中性面
    QColor surface;        // 浮板底
    QColor surface2;       // 分组 / 页脚
    QColor surface3;       // 轨道 / 凹陷
    QColor surfaceHi;      // hover
    QColor surfacePress;   // pressed
    QColor edgeHi;         // 浮板顶部 1px 内高光

    // 边框：设计稿四档 hairline < line-1 < line-2 < line-3
    QColor hairline;
    QColor line1;
    QColor line2;
    QColor line3;

    // 文字三档：层级靠明度差而非只靠字号
    QColor text1;
    QColor text2;
    QColor text3;

    // 主色：取自 DPalette，跟随 deepin 活动色
    QColor accent;
    QColor accentText;
    QColor accentSoft;   // 浅底
    QColor accentLine;   // 描边
    QColor onAccent;

    // 状态色四组
    StatusColor ok;
    StatusColor warn;
    StatusColor danger;
    StatusColor idle;

    // 阴影（浮板 / 托盘 / 卡片浮起）
    QColor shadowPanel;
    QColor shadowTray;
    QColor shadowLift;
    QColor shadowBar;      // --shadow-bar：分段控件选中档等「压在面上」的浅浮起

    bool isDark = false;
};

class Theme : public QObject {
    Q_OBJECT
public:
    static Theme &instance();

    const Palette &palette() const { return palette_; }
    bool isDark() const { return palette_.isDark; }

    // 重新从 DPalette 解析；主题变了才调用，随后发 changed()。
    void refresh();

    // 按状态档取三色（实色 / 浅底 / 文字）。
    StatusColor color(Quality q) const;

    // 延迟（ms）→ 档。offline 为 true 时直接给 Idle。
    static Quality qualityOf(double ms, bool online);

Q_SIGNALS:
    void changed();

private:
    Theme();

    Palette palette_;
};

} // namespace fg

#endif // FLOWGRID_THEME_H
