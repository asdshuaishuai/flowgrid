// 键位映射浮板：对应 design/unified/screens/keymap.html。
// 规则表走 YAML，由协议层 LoadKeyMapFile 解析；本屏只负责展示与编辑入口。
#ifndef FLOWGRID_KEYMAPPANEL_H
#define FLOWGRID_KEYMAPPANEL_H

#include "ui/panel.h"

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

    // 已生效的规则条数。本屏两处徽章与连接屏那条「N 条生效」都取这一个数，
    // 免得同一张表在三个地方各算一遍。
    int effectiveRuleCount() const;

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

    QVBoxLayout *mappingLayout_ = nullptr;
    Badge *countBadge_ = nullptr;
    Badge *globalBadge_ = nullptr;
    QList<KeyRule> rules_;
};

} // namespace fg

#endif // FLOWGRID_KEYMAPPANEL_H
