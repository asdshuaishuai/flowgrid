#include "keymappanel.h"
#include "core/designtokens.h"
#include "core/settings.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QVBoxLayout>

#include <functional>

Q_LOGGING_CATEGORY(lcKeymap, "flowgrid.keymap")

namespace fg {
namespace {

QFont uiFont(int px, int weight = QFont::Normal)
{
    QFont f = QApplication::font();
    f.setPixelSize(px);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

QString css(const QColor &c)
{
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

// 规则编辑器未接通的动作统一记一条日志，不做成点了没反应的按钮。
FgButton *unwiredButton(const QString &text, const QString &iconName, const QString &what,
                        QWidget *parent, bool accent, const std::function<void()> &onClick)
{
    auto *btn = new FgButton(text,
                             accent ? FgButton::Variant::Accent : FgButton::Variant::Secondary,
                             false, parent);
    if (!iconName.isEmpty()) {
        btn->setIcon(iconName);
    }
    QObject::connect(btn, &FgButton::clicked, parent, [what, onClick] {
        qCInfo(lcKeymap) << what << QStringLiteral("尚未接通，规则表仍由 YAML 提供");
        if (onClick) {
            onClick();
        }
    });
    return btn;
}

// 行首带图标的列表行标题（设计稿里的 code / play 图标）。
QWidget *iconTitle(const QString &iconName, const QString &text, const QColor &color,
                   QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *hl = new QHBoxLayout(w);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS2);
    auto *icon = new QLabel(w);
    icon->setFixedSize(tokens::kIconSm, tokens::kIconSm);
    icon->setPixmap(icons::icon(iconName, tokens::kIconSm, color).pixmap(tokens::kIconSm, tokens::kIconSm));
    auto *label = new QLabel(text, w);
    label->setFont(uiFont(tokens::kFont12));
    label->setStyleSheet(QStringLiteral("color:%1").arg(css(color)));
    hl->addWidget(icon);
    hl->addWidget(label);
    return w;
}

} // namespace

KeymapPanel::KeymapPanel(QWidget *parent) : Panel(tr("键位映射"), parent)
{
    auto *body = qobject_cast<QVBoxLayout *>(content()->layout());

    // ── 顶部：生效条数 + 「按应用自动切换」说明 + 新增规则 ──
    {
        auto *row = new QWidget(this);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(tokens::kS3);

        const Palette &p = Theme::instance().palette();
        countBadge_ = new Badge(QStringLiteral("0 条"), Badge::Variant::Accent);
        auto *hint = new QLabel(tr("按应用自动切换"), row);
        hint->setFont(uiFont(tokens::kFont11));
        hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));

        hl->addWidget(countBadge_);
        hl->addWidget(hint);
        hl->addStretch(1);
        hl->addWidget(unwiredButton(tr("新增规则"), QStringLiteral("plus"), tr("新增规则"),
                                     row, true, [this] { Q_EMIT editRequested(); }));
        body->addWidget(row);
    }

    body->addWidget(buildGlobalSection());
    body->addWidget(buildAppSection());
    body->addWidget(banner(tr("「Win → ⌥」与系统聚焦快捷键冲突，仅在非全屏窗口下生效。"), this,
                           BannerVariant::Warn));
    body->addWidget(buildAdvancedSection());
    setFooterWidget(buildFooter());

    // 设计稿给了六条示例规则；真实规则等传输层加载 YAML 后经 setRules 注入。
    setRules({
        {QStringLiteral("Ctrl"), QStringLiteral("⌘"), tr("复制 / 粘贴"), true},
        {QStringLiteral("Win"), QStringLiteral("⌥"), tr("系统快捷键"), true},
        {QStringLiteral("Alt"), QStringLiteral("Ctrl"), tr("终端应用"), true},
        {QStringLiteral("Ctrl"), QStringLiteral("Ctrl"), tr("直通（不转换）"), true},
        {QStringLiteral("⌘"), QStringLiteral("Ctrl"), tr("反向（回切 Windows）"), true},
        {QString(), QString(), tr("点击添加"), false},
    });
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

// 应用级覆盖：每行一个应用 + 开关 + 进入编辑。
QWidget *KeymapPanel::buildAppSection()
{
    const Palette &p = Theme::instance().palette();
    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    struct App { const char *icon; const char *name; const char *hint; };
    const App apps[2] = {
        {"code", QT_TR_NOOP("Visual Studio Code"), QT_TR_NOOP("Ctrl 直通，禁用 ⌘ 转换")},
        {"play", QT_TR_NOOP("游戏（全屏时）"), QT_TR_NOOP("停用全部映射，避免误触")},
    };
    for (int i = 0; i < 2; ++i) {
        auto *host = new QWidget(body);
        auto *hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(tokens::kS2);

        auto *sw = new ToggleSwitch(host);
        sw->setChecked(true);
        auto *edit = new IconButton(QStringLiteral("edit"), tr("进入编辑"), host);
        QObject::connect(edit, &IconButton::clicked, this, [this] { Q_EMIT editRequested(); });
        hl->addWidget(sw);
        hl->addWidget(edit);

        auto *title = iconTitle(QString::fromUtf8(apps[i].icon), tr(apps[i].name), p.text1, host);
        vl->addWidget(new ListRow(QString(), tr(apps[i].hint), host, this, title));
    }

    auto *badge = new Badge(tr("2 条"), Badge::Variant::Neutral, this);
    return new Collapsible(tr("应用覆盖"), QStringLiteral("monitor"), body, badge, this);
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
    hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
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

void KeymapPanel::setRules(const QList<KeyRule> &rules)
{
    rules_ = rules;
    if (countBadge_ != nullptr) {
        countBadge_->setText(tr("%1 条生效").arg(rules.size()));
    }
    if (globalBadge_ != nullptr) {
        globalBadge_->setText(tr("%1 条").arg(rules.size()));
    }
    rebuildRows();
}

void KeymapPanel::rebuildRows()
{
    if (mappingLayout_ == nullptr) {
        return;
    }
    clearLayout(mappingLayout_);
    if (rules_.isEmpty()) {
        const Palette &p = Theme::instance().palette();
        auto *empty = new QLabel(tr("尚无规则"), this);
        empty->setFont(uiFont(tokens::kFont12));
        empty->setAlignment(Qt::AlignCenter);
        empty->setContentsMargins(0, tokens::kS5, 0, tokens::kS5);
        empty->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        mappingLayout_->addWidget(empty);
        return;
    }

    QList<KeyPair> pairs;
    pairs.reserve(rules_.size());
    for (const KeyRule &rule : rules_) {
        // 全空的一行是设计稿里的「未设置 → 未设置 / 点击添加」占位槽。
        const bool ghost = rule.from.isEmpty() || rule.to.isEmpty();
        pairs.append(KeyPair{ghost ? tr("未设置") : rule.from, ghost ? tr("未设置") : rule.to,
                             rule.context, ghost});
    }
    mappingLayout_->addWidget(buildMappingGrid(pairs, this));
}

} // namespace fg
