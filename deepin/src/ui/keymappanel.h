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

class KeymapPanel : public Panel {
    Q_OBJECT
public:
    explicit KeymapPanel(QWidget *parent = nullptr);

    // 规则表来自协议层解析的 YAML。这里保留一个 setter，便于将来接真实加载。
    void setRules(const QList<KeyRule> &rules);

Q_SIGNALS:
    // 「新增规则」「编辑」目前没有落盘流程，交给外层统一提示未接通。
    void editRequested();

private:
    QWidget *buildRuleList();
    QWidget *buildFooter();
    void rebuildRows();

    QVBoxLayout *listLayout_ = nullptr;
    Badge *countBadge_ = nullptr;
    QList<KeyRule> rules_;
};

} // namespace fg

#endif // FLOWGRID_KEYMAPPANEL_H
