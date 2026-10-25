// 键位映射浮板：对应 design/unified/screens/keymap.html。
// 规则表走 YAML，由协议层 LoadKeyMapFile 解析；本屏只负责展示与编辑入口。
#ifndef FLOWGRID_KEYMAPPANEL_H
#define FLOWGRID_KEYMAPPANEL_H

#include "ui/panel.h"

class QLabel;
class QVBoxLayout;

namespace fg {

// 前向声明必须落在 fg 内：widgets.h 里定义的是 fg::Badge，声明在全局会
// 造出一个同名但毫无关系的 ::Badge，赋值时报无法转换。
class Badge;

struct KeyRule {
    QString from;
    QString to;
    QString context;    // 全局 / 应用名
    bool enabled = true;
};

// 设计稿网格末尾那条「未设置 → 未设置 / 点击添加」是占位槽，不是一条规则。
// 徽章计数与网格渲染共用这一个判据，否则加一行占位就多出一条「生效」。
inline bool isPlaceholderKeyRule(const KeyRule &rule)
{
    return rule.from.isEmpty() || rule.to.isEmpty();
}

// 真正生效的规则：不是占位槽，且没有被停用。
inline bool isEffectiveKeyRule(const KeyRule &rule)
{
    return !isPlaceholderKeyRule(rule) && rule.enabled;
}

class KeymapPanel : public Panel {
    Q_OBJECT
public:
    explicit KeymapPanel(QWidget *parent = nullptr);

    // 规则表来自协议层解析的 YAML。这里保留一个 setter，便于将来接真实加载。
    void setRules(const QList<KeyRule> &rules);

    // 从规则表（core/keyrules）重新取一次并刷新显示。
    //
    // 规则表同时是重映射通路用的那张表——所以这一屏显示的永远是真正生效的
    // 那一份，不会出现「屏上写着一套、实际跑着另一套」。
    void reloadFromKeyRules();

    // 已生效的规则条数。本屏两处徽章与连接屏那条「N 条生效」都取这一个数，
    // 免得同一张表在三个地方各算一遍。
    int effectiveRuleCount() const;

    // 应用覆盖区当前显示了几个分组（供自检比对规则表）。
    int appGroupCount() const { return appGroupCount_; }

    // 冲突横幅当前显示的文字；空 = 没有画横幅。
    QString conflictText() const { return conflictText_; }

    // 供连接屏那种只读摘要区取同一张表，不另存一份。
    const QList<KeyRule> &rules() const { return rules_; }

Q_SIGNALS:
    // 「新增规则」「编辑」目前没有落盘流程，交给外层统一提示未接通。
    void editRequested();
    // 规则表被整体替换后发出（将来接 YAML 加载时用）。
    void rulesChanged();

private:
    QWidget *buildGlobalSection();
    QWidget *buildAppSection();
    QWidget *buildAdvancedSection();
    QWidget *buildFooter();
    void rebuildRows();
    // 应用覆盖区：按规则表里 context 非 global 的那几条重建。
    void reloadAppSection();
    // 冲突横幅：按规则表算出的冲突重建；没有冲突就不画。
    void reloadConflictBanner();
    // 顶部提示：现在生效的规则来自哪个文件。
    void reloadSourceHint();
    // 给网格末尾那个「未设置 → 未设置 / 点击添加」的占位格装上点击响应。
    // mac 端那一格是真按钮（点了加一条规则），这一屏的网格在 widgets.cpp 里
    // 画（不能改），所以用事件过滤器补上——不然「点击添加」四个字就是一句
    // 空话。
    void makeGhostCellClickable(QWidget *grid, int ghostIndex);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QVBoxLayout *mappingLayout_ = nullptr;
    Badge *countBadge_ = nullptr;
    Badge *globalBadge_ = nullptr;
    Badge *appBadge_ = nullptr;
    QLabel *sourceHint_ = nullptr;
    QVBoxLayout *appLayout_ = nullptr;
    QWidget *conflictHost_ = nullptr;
    QList<KeyRule> rules_;
    // 当前渲染出来的应用分组数与冲突文案：自检拿它们跟规则表逐条比对，
    // 免得「屏上画着一套、规则表里是另一套」。
    int appGroupCount_ = 0;
    QString conflictText_;
    // 占位格（点击添加）那个格子，事件过滤器认它用。
    QWidget *ghostCell_ = nullptr;
};

} // namespace fg

#endif // FLOWGRID_KEYMAPPANEL_H
