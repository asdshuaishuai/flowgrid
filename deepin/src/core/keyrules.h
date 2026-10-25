// 键位规则表：规则的**本体**在协议层（proto::KeyMap 持 YAML 解析出来的表），
// 这一层只做三件事：
//   · 找到并加载规则文件；
//   · 把规则翻译成界面要的「源键 → 目标键」字形；
//   · 把**同一张表**交给重映射通路。
//
// 关键约束：界面显示的必须是真正生效的那一份。规则表一份、显示另一份，
// 「屏上写着 Ctrl 直通、实际却转成了 ⌘」就成了最难查的一类不一致。
#ifndef FLOWGRID_KEYRULES_H
#define FLOWGRID_KEYRULES_H

#include "core/protocolapi.h"

#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace fg {

// 一条显示用的规则行。
struct KeyRuleRow {
    QString fromLabel;   // 源键字形（"Ctrl"）
    QString toLabel;     // 目标键字形（"⌘"）；空 = 未设置
    QString caption;     // 说明（"Linux → macOS"）
};

// 一个应用级覆盖分组：规则表里 context 非 global 的那几条，按 context 归并。
//
// 为什么要分组：协议里的应用规则是「app:<bundleID>」或「game」这种上下文，
// 一个上下文下的多条规则是**同一个覆盖**（那个应用整体换算），拆成多行就
// 把「一条覆盖」说成了「好几条」。
struct KeyContextGroup {
    QString context;     // 原始上下文（"app:com.microsoft.VSCode" / "game"）
    QString label;       // 显示名（bundle ID 原样，或「游戏模式」）
    QString iconName;    // "code" / "play"
    QString hint;        // 说明（"2 条覆盖：Ctrl → ⌘"）
    int ruleCount = 0;
};

// 一条冲突：规则把某个键映到了目标平台的系统修饰键上。
struct KeyConflictRow {
    QString fromLabel;   // 源键字形（"Ctrl"）
    QString toLabel;     // 目标键字形（"⌘"）
    QString toPlatform;  // 目标平台名（"macOS"）
};

class KeyRules : public QObject {
    Q_OBJECT
public:
    static KeyRules &instance();

    // 找规则文件并加载。
    //
    // 查找顺序（先到先得）：
    //   1. 用户级 ~/.config/flowgrid/keymap.yaml
    //   2. 系统级 /usr/local/share/flowgrid/keymap.yaml（随安装落地）
    //   3. 发行版级 /usr/share/flowgrid/keymap.yaml
    //   4. 应用自带的 :/keymap.yaml（没安装也能跑）
    //
    // 返回 false = **没找到**任何规则文件。这不是错误——按平台默认换算即可
    // （§6.3 Ctrl⇄⌘），界面据此显示「按平台默认」的空状态。
    // 找到了但解析失败才是失败：lastError() 给出原因。规则坏了不能静默按
    // 默认跑，那等于用户配的映射悄悄失效。
    bool load() const;

    bool loaded() const { return table_ != nullptr && table_->isValid(); }

    // 需要时确保已加载（界面可能先于连接被构造）。
    //
    // 懒加载而不是「谁先谁负责」：界面与重映射通路都要这张表，谁先起来谁就
    // 负责找文件——那会出现「有时加载了有时没加载」的玄学。
    void ensureLoaded() const;

    // 重新读一次规则文件，并通知界面刷新。
    //
    // 为什么单独一个 reload()：load() 是 const 的（懒加载在 const 方法里做），
    // 而「规则表变了」这个信号只能从非 const 的地方发。
    void reload();

    const proto::KeyMap *table() const { return table_.get(); }
    QString lastError() const { return lastError_; }
    QString sourcePath() const { return sourcePath_; }

    // 规则表 → 显示行。**只取全局规则**（context == global）：应用级的那
    // 几条由 contextGroups() 单独显示，混进全局区就成了两处各算一遍。
    // 表为空时返回空表（界面据此显示空状态）。
    QList<KeyRuleRow> rows() const;

    // 应用级覆盖分组（context 非 global 的规则）。没有就返回空表。
    QList<KeyContextGroup> contextGroups() const;

    // 冲突（规则把键映到了目标平台的系统修饰键上）。没有就返回空表。
    QList<KeyConflictRow> conflicts() const;

    // 平台号 → 平台名（"Linux" / "macOS" / "Windows"）。给规则的说明用。
    static QString platformName(std::uint8_t platform);

    // HID Usage ID → 字形。只认修饰键（Ctrl / Shift / Alt / GUI）——规则表
    // 关心的就是这几个键，普通字母键各平台本来就一样，没必要逐个列。
    static QString keyGlyph(std::uint8_t platform, std::uint16_t hidKey);

    // 上下文（"app:<bundleID>" / "game"）→ 显示名。
    static QString contextLabel(const QString &context);

Q_SIGNALS:
    // 规则表被（重新）载入。
    //
    // 为什么要有这个信号：重映射通路在**连接时**重新读一次规则文件，用户
    // 改了 YAML 之后重连就生效；界面若还画着旧的那一份，就成了「屏上写着
    // 一套、实际跑着另一套」——正是这一整轮要消灭的那种不一致。
    void tableChanged();

private:
    KeyRules() = default;

    // mutable：加载是**懒**的，而 rows() 是 const 的（它只读）。
    mutable std::unique_ptr<proto::KeyMap> table_;
    mutable QString lastError_;
    mutable QString sourcePath_;
    mutable bool tried_ = false;   // 已经找过一次了，别反复找
};

} // namespace fg

#endif // FLOWGRID_KEYRULES_H
