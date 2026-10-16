#include "keymappanel.h"
#include "core/designtokens.h"
#include "core/settings.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

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

// 键帽：设计稿 .key，带 1px 底边模拟键帽厚度
QLabel *keycap(const QString &text, bool ghost, QWidget *parent)
{
    const Palette &p = Theme::instance().palette();
    auto *key = new QLabel(text, parent);
    key->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    key->setAlignment(Qt::AlignCenter);
    key->setMinimumWidth(34);
    if (ghost) {
        key->setStyleSheet(QStringLiteral("border:1px dashed %1;border-radius:%2px;color:%3")
                               .arg(css(p.line2)).arg(tokens::kRadiusCtl).arg(css(p.text3)));
    } else {
        key->setStyleSheet(
            QStringLiteral("border:1px solid %1;border-bottom-width:2px;border-radius:%2px;"
                          "background:%3;color:%4")
                .arg(css(p.line2)).arg(tokens::kRadiusCtl).arg(css(p.surface))
                .arg(css(p.text1)));
    }
    return key;
}

} // namespace

KeymapPanel::KeymapPanel(QWidget *parent) : Panel(tr("键位映射"), parent)
{
    auto *body = qobject_cast<QVBoxLayout *>(content()->layout());
    auto &settings = Settings::instance();

    // ── 顶部：生效开关 + 新增规则 ──
    {
        auto *row = new QWidget(this);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(tokens::kS3);

        countBadge_ = new Badge(QStringLiteral("0 条"), Badge::Variant::Accent);
        auto *toggle = new ToggleSwitch(row);
        toggle->setChecked(settings.keymapEnabled());
        QObject::connect(toggle, &ToggleSwitch::clicked, this, [toggle] {
            Settings::instance().setKeymapEnabled(toggle->isChecked());
        });

        hl->addWidget(countBadge_);
        hl->addWidget(toggle);
        hl->addStretch(1);

        auto *add = new FgButton(tr("新增规则"), FgButton::Variant::Accent, false, row);
        QObject::connect(add, &FgButton::clicked, this, &KeymapPanel::editRequested);
        hl->addWidget(add);
        body->addWidget(row);
    }

    // ── 按应用自动切换 ──
    {
        auto *appToggle = new ToggleSwitch(this);
        appToggle->setChecked(true);
        body->addWidget(new ListRow(tr("按应用自动切换"),
                                       tr("游戏内自动停用映射"), appToggle, this));
    }

    body->addWidget(buildRuleList());

    // ── 说明条 ──
    {
        const Palette &p = Theme::instance().palette();
        auto *banner = new QWidget(this);
        auto *hl = new QHBoxLayout(banner);
        hl->setContentsMargins(tokens::kS3, tokens::kS2, tokens::kS3, tokens::kS2);
        hl->setSpacing(tokens::kS2);
        auto *icon = new QLabel(banner);
        icon->setFixedSize(14, 14);
        icon->setPixmap(icons::icon(QStringLiteral("warn"), 14, p.warn.solid).pixmap(14, 14));
        auto *text = new QLabel(
            tr("规则表按 YAML 加载，修改后热重载；游戏内不生效以避免误触"), banner);
        text->setWordWrap(true);
        text->setFont(uiFont(tokens::kFont11));
        text->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        hl->addWidget(icon);
        hl->addWidget(text, 1);
        body->addWidget(banner);
    }

    setFooterWidget(buildFooter());

    // 设计稿里的三条示例规则，先按静态数据呈现，真实规则等传输层加载 YAML 后注入。
    setRules({
        {QStringLiteral("Ctrl"), QStringLiteral("⌘"), tr("复制 / 粘贴"), true},
        {QStringLiteral("Win"), QStringLiteral("⌥"), tr("系统快捷键"), true},
        {QStringLiteral("Alt"), QStringLiteral("Ctrl"), tr("终端应用"), true},
    });
}

QWidget *KeymapPanel::buildRuleList()
{
    const Palette &p = Theme::instance().palette();
    auto *list = new QWidget(this);
    listLayout_ = new QVBoxLayout(list);
    listLayout_->setContentsMargins(0, 0, 0, 0);
    listLayout_->setSpacing(tokens::kS2);
    applyBoxStyle(list,
                  QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                      .arg(css(p.line2)).arg(tokens::kRadiusCard).arg(css(p.surface)));
    return list;
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
    icon->setFixedSize(14, 14);
    icon->setPixmap(icons::icon(QStringLiteral("info"), 14, p.text3).pixmap(14, 14));
    auto *hint = new QLabel(tr("规则按从上到下顺序匹配"), foot);
    hint->setFont(uiFont(tokens::kFont11));
    hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    hl->addWidget(icon);
    hl->addWidget(hint);
    hl->addStretch(1);

    auto *reset = new FgButton(tr("恢复默认"), FgButton::Variant::Secondary, true, foot);
    QObject::connect(reset, &FgButton::clicked, this, [this] {
        // 规则编辑器未接通，恢复默认要连带写回 YAML，先把边界说清楚。
        Q_EMIT editRequested();
    });
    hl->addWidget(reset);

    auto *save = new FgButton(tr("保存"), FgButton::Variant::Accent, true, foot);
    QObject::connect(save, &FgButton::clicked, this, [this] {
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
    rebuildRows();
}

void KeymapPanel::rebuildRows()
{
    if (listLayout_ == nullptr) {
        return;
    }
    while (listLayout_->count() > 0) {
        QLayoutItem *item = listLayout_->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    const Palette &p = Theme::instance().palette();

    if (rules_.isEmpty()) {
        auto *empty = new QLabel(tr("尚无规则"), listLayout_->parentWidget());
        empty->setFont(uiFont(tokens::kFont12));
        empty->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        listLayout_->addWidget(empty);
        return;
    }

    for (const KeyRule &rule : rules_) {
        auto *row = new QWidget(listLayout_->parentWidget());
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(tokens::kS3, tokens::kS2, tokens::kS3, tokens::kS2);
        hl->setSpacing(tokens::kS2);

        auto *keys = new QWidget(row);
        auto *kl = new QHBoxLayout(keys);
        kl->setContentsMargins(0, 0, 0, 0);
        kl->setSpacing(5);
        kl->addWidget(keycap(rule.from, false, keys));
        auto *arrow = new QLabel(keys);
        arrow->setFixedSize(14, 14);
        arrow->setPixmap(icons::icon(QStringLiteral("arrowRight"), 14, p.text3).pixmap(14, 14));
        kl->addWidget(arrow);
        kl->addWidget(keycap(rule.to, rule.to.isEmpty(), keys));

        auto *context = new QLabel(rule.context, row);
        context->setFont(uiFont(tokens::kFont11));
        context->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));

        hl->addWidget(keys);
        hl->addStretch(1);
        hl->addWidget(context);
        listLayout_->addWidget(row);
    }
}

} // namespace fg
