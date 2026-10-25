#include "keymappanel.h"
#include "core/designtokens.h"
#include "core/keyrules.h"
#include "core/settings.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QVBoxLayout>

#include <functional>

Q_LOGGING_CATEGORY(lcKeymap, "flowgrid.keymap")

namespace fg {
namespace {

// 规则表尚未接通（等传输层加载 YAML）：开关照设计稿摆出来，但点了要给出反馈，
// 不然就和「点了没反应的按钮」是同一类问题——unwiredButton 是这么处理的。
void unwiredToggle(ToggleSwitch *sw, const QString &what, QObject *ctx)
{
    QObject::connect(sw, &ToggleSwitch::clicked, ctx, [sw, what] {
        qCInfo(lcKeymap) << what << (sw->isChecked() ? QStringLiteral("已开启") : QStringLiteral("已关闭"))
                         << QStringLiteral("尚未接通，规则表仍由 YAML 提供");
    });
}

// 行首带图标的列表行标题（设计稿里的 code / play 图标）。
// 图标与文字分两个颜色：mac 端图标是 text-2、文字是 text-1，一个颜色画两样
// 会让图标抢过文字。
QWidget *iconTitle(const QString &iconName, const QString &text, const QColor &iconColor,
                   const QColor &textColor, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS2);
    auto *icon = new QLabel(w);
    icon->setFixedSize(tokens::kIconMd, tokens::kIconMd);
    icon->setPixmap(icons::icon(iconName, tokens::kIconMd, iconColor)
                        .pixmap(tokens::kIconMd, tokens::kIconMd));
    auto *label = new QLabel(text, w);
    label->setFont(uiFont(tokens::kFont12));
    label->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(textColor)));
    hl->addWidget(icon);
    hl->addWidget(label);
    return w;
}

} // namespace

KeymapPanel::KeymapPanel(QWidget *parent) : Panel(tr("键位映射"), parent)
{
    auto *body = qobject_cast<QVBoxLayout *>(content()->layout());

    // ── 顶部：生效条数 + 规则来源 + 新增规则 ──
    {
        auto *row = new QWidget(this);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(tokens::kS3);

        const Palette &p = Theme::instance().palette();
        countBadge_ = new Badge(QStringLiteral("0 条"), Badge::Variant::Accent);
        // 顶部这行字原来写的是「按应用自动切换」——那是句**承诺**：应用级规则
        // 到了重映射通路才能兑现，而通路目前只按全局规则换算。写一句兑现不了
        // 的承诺，不如把真话说出来：现在生效的规则是从哪儿来的。
        sourceHint_ = new QLabel(row);
        sourceHint_->setFont(uiFont(tokens::kFont11));
        sourceHint_->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));

        hl->addWidget(countBadge_);
        hl->addWidget(sourceHint_);
        hl->addStretch(1);
        hl->addWidget(unwiredButton(tr("新增规则"), tr("新增规则"), row,
                                     QStringLiteral("plus"), false, true,
                                     [this] { Q_EMIT editRequested(); }));
        body->addWidget(row);
    }

    body->addWidget(buildGlobalSection());
    body->addWidget(buildAppSection());

    // 冲突横幅：内容随规则表变，所以给它一个常驻的容器，刷新时换里面的
    // 横幅。规则表里没有冲突时容器是空的——不画一条没内容的告警。
    conflictHost_ = new QWidget(this);
    auto *conflictLayout = new QVBoxLayout(conflictHost_);
    conflictLayout->setContentsMargins(0, 0, 0, 0);
    body->addWidget(conflictHost_);

    body->addWidget(buildAdvancedSection());
    setFooterWidget(buildFooter());

    // 显示**真实规则表**：规则由协议层解析 YAML（core/keyrules），这一屏只
    // 负责把规则画出来。以前这里写死六条示例——界面显示的与真正生效的不是
    // 同一份，「屏上写着 Ctrl 直通、实际却转成了 ⌘」就成了最难查的一类
    // 不一致。
    reloadFromKeyRules();
    // 先渲染一次，**再**订阅「规则表变了」：构造期间触发回调会碰到半构造的
    // 控件。连接时规则表会被重新读一次（用户改了 YAML 就能生效），那一刻
    // 这一屏必须跟着换。
    QObject::connect(&KeyRules::instance(), &KeyRules::tableChanged, this,
                     &KeymapPanel::reloadFromKeyRules);
}

// 设计稿 keymap.html 的「全局」折叠区：3×2 键位网格。
QWidget *KeymapPanel::buildGlobalSection()
{
    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    mappingLayout_ = new QVBoxLayout;
    mappingLayout_->setContentsMargins(0, 0, 0, 0);
    vl->addLayout(mappingLayout_);

    // 与顶部「N 条生效」同源：两条徽章各写一个数，规则一改就会自相矛盾。
    globalBadge_ = new Badge(tr("0 条"), Badge::Variant::Accent, this);
    return new Collapsible(tr("全局"), QStringLiteral("keyboard"), body, globalBadge_, this);
}

// 应用级覆盖：每行一个上下文（app:<bundleID> / game）+ 开关 + 进入编辑。
//
// 这一区原来写死两行「Visual Studio Code」「游戏（全屏时）」——那是设计稿里
// 的示例，规则表里根本没有对应的规则，屏上写着两行覆盖、实际一条都不生效。
// 现在每行都来自规则表里 context 非 global 的那几条。
QWidget *KeymapPanel::buildAppSection()
{
    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    appLayout_ = vl;

    appBadge_ = new Badge(tr("0 条"), Badge::Variant::Neutral, this);
    return new Collapsible(tr("应用覆盖"), QStringLiteral("monitor"), body, appBadge_, this);
}

// 应用覆盖区按规则表重建。
void KeymapPanel::reloadAppSection()
{
    if (appLayout_ == nullptr) {
        return;
    }
    clearLayout(appLayout_);
    const QList<KeyContextGroup> groups = KeyRules::instance().contextGroups();
    appGroupCount_ = groups.size();
    if (appBadge_ != nullptr) {
        appBadge_->setText(tr("%1 条").arg(groups.size()));
    }
    if (groups.isEmpty()) {
        // 为什么空要说清楚：规则文件里没有应用级上下文，不是功能坏了。
        appLayout_->addWidget(new EmptyState(
            QStringLiteral("monitor"), tr("没有应用级覆盖"),
            tr("规则文件里没有 app:/game 上下文"), this));
        return;
    }

    const Palette &p = Theme::instance().palette();
    for (const KeyContextGroup &group : groups) {
        auto *host = new QWidget(this);
        auto *hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        // mac 端开关与「编辑」按钮之间是 s3(12)，这里原来写的是 s2(8)。
        hl->setSpacing(tokens::kS3);

        auto *sw = new ToggleSwitch(host);
        sw->setChecked(true);
        // 应用级规则目前**不会**被重映射通路采用（通路只按全局规则换算，
        // 见 PeerController::applyKeyRules）。开关点了要说清楚这一点，
        // 不能让人以为一开就生效了。
        QObject::connect(sw, &ToggleSwitch::clicked, this, [group] {
            qCInfo(lcKeymap) << QObject::tr("应用级覆盖") << group.label
                             << QObject::tr("尚未接通：重映射通路目前只按全局规则换算");
        });
        auto *edit = new IconButton(QStringLiteral("edit"), tr("进入编辑"), host);
        QObject::connect(edit, &IconButton::clicked, this, [this] { Q_EMIT editRequested(); });
        hl->addWidget(sw);
        hl->addWidget(edit);

        auto *title = iconTitle(group.iconName, group.label, p.text2, p.text1, host);
        appLayout_->addWidget(new ListRow(QString(), group.hint, host, this, title));
    }
}

// 高级区：设计稿默认收起。
QWidget *KeymapPanel::buildAdvancedSection()
{
    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    const struct { const char *label; const char *hint; } rows[2] = {
        {QT_TR_NOOP("按键重复率跟随目标端"), QT_TR_NOOP("使用被控设备的系统重复设置")},
        {QT_TR_NOOP("发送原始扫描码"), QT_TR_NOOP("游戏与远程桌面场景更精确")},
    };
    for (int i = 0; i < 2; ++i) {
        auto *sw = new ToggleSwitch(this);
        sw->setChecked(false);
        unwiredToggle(sw, tr(rows[i].label), this);
        vl->addWidget(new ListRow(tr(rows[i].label), tr(rows[i].hint), sw, this));
    }

    auto *badge = new Badge(tr("2 项"), Badge::Variant::Neutral, this);
    auto *section = new Collapsible(tr("高级"), QStringLiteral("settings"), body, badge, this);
    section->setCollapsed(true);
    return section;
}

QWidget *KeymapPanel::buildFooter()
{
    // 设计稿页脚：左「规则按从上到下顺序匹配」+ info 图标，右「恢复默认 / 保存」。
    const Palette &p = Theme::instance().palette();
    auto *foot = new QWidget(this);
    auto *hl = new QHBoxLayout(foot);
    hl->setContentsMargins(tokens::kContentPadding, 10, tokens::kContentPadding, 10);
    hl->setSpacing(tokens::kS2);

    auto *icon = new QLabel(foot);
    icon->setFixedSize(tokens::kIconSm, tokens::kIconSm);
    icon->setPixmap(icons::icon(QStringLiteral("info"), tokens::kIconSm, p.text3).pixmap(tokens::kIconSm, tokens::kIconSm));
    auto *hint = new QLabel(tr("规则按从上到下顺序匹配"), foot);
    hint->setFont(uiFont(tokens::kFont11));
    hint->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
    hl->addWidget(icon);
    hl->addWidget(hint);
    hl->addStretch(1);

    auto *reset = new FgButton(tr("恢复默认"), FgButton::Variant::Secondary, true, foot);
    QObject::connect(reset, &FgButton::clicked, this, [this] {
        // 规则编辑器未接通，恢复默认要连带写回 YAML，先把边界说清楚。
        qCInfo(lcKeymap) << tr("恢复默认映射") << QStringLiteral("尚未接通");
        Q_EMIT editRequested();
    });
    hl->addWidget(reset);

    auto *save = new FgButton(tr("保存"), FgButton::Variant::Accent, true, foot);
    QObject::connect(save, &FgButton::clicked, this, [this] {
        qCInfo(lcKeymap) << tr("保存映射") << QStringLiteral("尚未接通");
        Q_EMIT editRequested();
        hide();
    });
    hl->addWidget(save);
    return foot;
}

// 从规则表重新取一次并刷新显示。
//
// 规则表由 core/keyrules 持有（它同时是重映射通路用的那张表），所以这一屏
// 显示的永远是真正生效的那一份。
void KeymapPanel::reloadFromKeyRules()
{
    QList<KeyRule> rules;
    const QList<KeyRuleRow> rows = KeyRules::instance().rows();
    for (const KeyRuleRow &row : rows) {
        rules.append(KeyRule{row.fromLabel, row.toLabel, row.caption, true});
    }
    // 末尾那个「未设置 → 未设置 / 点击添加」是占位槽，不是一条规则。
    rules.append(KeyRule{QString(), QString(), tr("点击添加"), false});
    setRules(rules);
    reloadAppSection();
    reloadConflictBanner();
    reloadSourceHint();
}

// 冲突横幅按规则表重建：没有冲突就不画横幅。
//
// 判定在协议层（规则把键映到了目标平台的系统修饰键上），这里只把结果写成
// 一句话。以前这里写死一句「「Win → ⌥」与系统聚焦快捷键冲突」——不管规则
// 表里有没有那条规则都在那儿报警，规则改了横幅也不动。
void KeymapPanel::reloadConflictBanner()
{
    if (conflictHost_ == nullptr) {
        return;
    }
    auto *layout = qobject_cast<QVBoxLayout *>(conflictHost_->layout());
    if (layout == nullptr) {
        return;
    }
    clearLayout(layout);

    const QList<KeyConflictRow> conflicts = KeyRules::instance().conflicts();
    conflictText_.clear();
    if (conflicts.isEmpty()) {
        return;
    }
    QStringList pairs;
    QStringList platforms;
    for (const KeyConflictRow &c : conflicts) {
        pairs << QStringLiteral("%1 → %2").arg(c.fromLabel, c.toLabel);
        if (!platforms.contains(c.toPlatform)) {
            platforms << c.toPlatform;
        }
    }
    conflictText_ = tr("「%1」映射到 %2 的系统修饰键，可能与系统快捷键冲突")
                        .arg(pairs.join(QStringLiteral("、")),
                             platforms.join(QStringLiteral("、")));
    layout->addWidget(banner(conflictText_, conflictHost_, BannerVariant::Warn));
}

// 顶部提示：现在生效的规则是从哪儿来的。
void KeymapPanel::reloadSourceHint()
{
    if (sourceHint_ == nullptr) {
        return;
    }
    const KeyRules &rules = KeyRules::instance();
    const QString full = rules.loaded() ? rules.sourcePath() : tr("按平台默认换算（没有规则文件）");
    sourceHint_->setToolTip(full);
    // 路径可能很长，压到一行宽度内；完整路径挂 tooltip。
    const QFontMetrics fm(sourceHint_->font());
    sourceHint_->setText(fm.elidedText(full, Qt::ElideMiddle, 180));
}

void KeymapPanel::setRules(const QList<KeyRule> &rules)
{
    rules_ = rules;
    // 徽章报的是「生效的规则数」，不是网格里的格子数（末尾的「点击添加」占位槽不算）。
    const int effective = effectiveRuleCount();
    if (countBadge_ != nullptr) {
        countBadge_->setText(tr("%1 条生效").arg(effective));
    }
    if (globalBadge_ != nullptr) {
        globalBadge_->setText(tr("%1 条").arg(effective));
    }
    rebuildRows();
    Q_EMIT rulesChanged();
}

int KeymapPanel::effectiveRuleCount() const
{
    int n = 0;
    for (const KeyRule &rule : rules_) {
        if (isEffectiveKeyRule(rule)) {
            ++n;
        }
    }
    return n;
}

void KeymapPanel::rebuildRows()
{
    if (mappingLayout_ == nullptr) {
        return;
    }
    clearLayout(mappingLayout_);
    if (rules_.isEmpty()) {
        const Palette &p = Theme::instance().palette();
        // 为什么空要说清楚：没有规则文件 ≠ 功能坏了。没有规则时按平台默认
        // 换算（§6.3 Ctrl⇄⌘），照样能用。
        auto *empty = new QLabel(
            KeyRules::instance().loaded() ? tr("尚无规则") : tr("按平台默认换算（未找到规则文件）"),
            this);
        empty->setFont(uiFont(tokens::kFont12));
        empty->setAlignment(Qt::AlignCenter);
        empty->setContentsMargins(0, tokens::kS5, 0, tokens::kS5);
        empty->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
        mappingLayout_->addWidget(empty);
        return;
    }

    QList<KeyPair> pairs;
    pairs.reserve(rules_.size());
    int ghostIndex = -1;
    for (int i = 0; i < rules_.size(); ++i) {
        const KeyRule &rule = rules_.at(i);
        // 占位槽与徽章计数共用判据（isPlaceholderKeyRule），两处各写一遍就会漂开。
        const bool ghost = isPlaceholderKeyRule(rule);
        if (ghost) {
            ghostIndex = i;
        }
        pairs.append(KeyPair{ghost ? tr("未设置") : rule.from, ghost ? tr("未设置") : rule.to,
                             rule.context, ghost});
    }
    auto *grid = buildMappingGrid(pairs, this);
    mappingLayout_->addWidget(grid);
    makeGhostCellClickable(grid, ghostIndex);
}

// mac 端那个「未设置 → 未设置 / 点击添加」的格子是个按钮：点了就加一条规则。
// 网格本身在 widgets.cpp 里画（那边不能改），所以这里给它装个事件过滤器。
// 找不到对应格子就什么都不做——宁可这一格不可点，也不要点到别的格子上。
void KeymapPanel::makeGhostCellClickable(QWidget *grid, int ghostIndex)
{
    ghostCell_ = nullptr;
    if (grid == nullptr || ghostIndex < 0) {
        return;
    }
    QList<QWidget *> cells;
    for (QWidget *child : grid->findChildren<QWidget *>()) {
        if (child->parentWidget() == grid) {
            cells.append(child);
        }
    }
    if (cells.size() != rules_.size() || ghostIndex >= cells.size()) {
        return;
    }
    QWidget *ghost = cells.at(ghostIndex);
    ghost->setCursor(Qt::PointingHandCursor);
    ghost->installEventFilter(this);
    ghostCell_ = ghost;
}

bool KeymapPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ghostCell_ && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            // 规则编辑器未接通：点「点击添加」与点顶部「新增规则」一样，如实记
            // 一笔「尚未接通」，不假装已经加上了规则。
            qCInfo(lcKeymap) << tr("新增规则") << QStringLiteral("尚未接通，规则表仍由 YAML 提供");
            Q_EMIT editRequested();
            return true;
        }
    }
    return Panel::eventFilter(watched, event);
}

} // namespace fg
