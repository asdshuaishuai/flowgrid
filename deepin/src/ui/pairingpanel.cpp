#include "pairingpanel.h"
#include "core/designtokens.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcPairing, "flowgrid.pairing")

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

QString signalText(int level)
{
    switch (level) {
    case 0:  return QObject::tr("信号弱");
    case 1:  return QObject::tr("信号中");
    default: return QObject::tr("信号强");
    }
}

// 还没接通的动作一律明确记日志，不做成点了没反应的按钮。
FgButton *unwiredButton(const QString &text, const QString &iconName, const QString &what,
                        QWidget *parent, bool accent)
{
    auto *btn =
        new FgButton(text, accent ? FgButton::Variant::Accent : FgButton::Variant::Secondary,
                     true, parent);
    if (!iconName.isEmpty()) {
        btn->setIcon(iconName);
    }
    QObject::connect(btn, &FgButton::clicked, parent, [what] {
        qCInfo(lcPairing) << what << QStringLiteral("尚未接通");
    });
    return btn;
}

} // namespace

PairingPanel::PairingPanel(QWidget *parent) : Panel(tr("添加设备"), parent)
{
    auto *body = qobject_cast<QVBoxLayout *>(content()->layout());

    body->addWidget(buildSteps());
    body->addWidget(
        banner(tr("在新设备上打开 FlowGrid → 添加设备，输入下面的一次性配对码。"), this,
             BannerVariant::Info));
    body->addWidget(buildPairCode());
    body->addWidget(buildDeviceList());
    body->addWidget(buildManualSection());
    setFooterWidget(buildFooter());

    // 设计稿列了三台：两台在线可发起配对，一台没在跑 FlowGrid、只给一个离线徽章。
    // 早先只列了前两台，「离线」这个状态就没地方出现。
    setDevices({
        {QStringLiteral("deepin-laptop"), QStringLiteral("192.168.x.x"),
         QStringLiteral("laptop"), 3, 8, true, false},
        {QStringLiteral("office-pc"), QStringLiteral("192.168.x.x"),
         QStringLiteral("monitor"), 2, 24, true, false},
        {QStringLiteral("tablet-7f"), QString(), QStringLiteral("tablet"), 0, 0, false, false},
    });
}

QWidget *PairingPanel::buildSteps()
{
    // 设计稿：三个步骤 1 发现 / 2 配对 / 3 完成，中间用连接线
    const Palette &p = Theme::instance().palette();
    auto *row = new QWidget(this);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS2);

    const QStringList labels{tr("发现"), tr("配对"), tr("完成")};
    for (int i = 0; i < labels.size(); ++i) {
        auto *step = new QWidget(row);
        auto *sv = new QVBoxLayout(step);
        sv->setContentsMargins(0, 0, 0, 0);
        sv->setSpacing(4);
        sv->setAlignment(Qt::AlignCenter);

        // 设计稿 .step--done 的序号圆里是 check 图标，不是数字 1。
        const bool done = (i < 1);
        const bool active = (i == 1);   // 本屏停在第 2 步
        auto *num = new QLabel(step);
        num->setFixedSize(tokens::kStepBadgeSize, tokens::kStepBadgeSize);
        num->setAlignment(Qt::AlignCenter);
        if (done) {
            num->setPixmap(icons::icon(QStringLiteral("check"), 12, p.ok.text).pixmap(12, 12));
        } else {
            num->setText(QString::number(i + 1));
            num->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
        }
        // .step--on 整圆填主色 + on-accent 字；.step--done 是 ok-soft 浅底 +
        // ok-line 描边 + ok-text 字，不是实心——实心会把「已完成」和「正在进行」
        // 画成同一档轻重；未到的档只给 line-2 描边，圆底透出面板色。
        QColor line = p.line2;
        QString background = QStringLiteral("transparent");
        QColor fg = p.text3;
        if (active) {
            background = css(p.accent);
            line = p.accent;
            fg = p.onAccent;
        } else if (done) {
            background = css(p.ok.soft);   // 带 alpha，css() 会输出 8 位值
            line = p.ok.solid;
            line.setAlphaF(0.45);
            fg = p.ok.text;
        }
        num->setStyleSheet(
            QStringLiteral("background:%1;color:%2;border:1px solid %3;border-radius:%4px")
                .arg(background)
                .arg(css(fg))
                .arg(css(line))
                .arg(QString::number(tokens::pillRadius(tokens::kStepBadgeSize))));

        auto *text = new QLabel(labels.at(i), step);
        text->setAlignment(Qt::AlignCenter);
        text->setFont(uiFont(tokens::kFont11, active ? QFont::DemiBold : QFont::Normal));
        text->setStyleSheet(
            QStringLiteral("color:%1").arg(css(active ? p.text1 : p.text3)));

        sv->addWidget(num);
        sv->addWidget(text);
        hl->addWidget(step);

        if (i != labels.size() - 1) {
            auto *line = new QWidget(row);
            line->setFixedHeight(1);
            line->setStyleSheet(QStringLiteral("background:%1").arg(css(p.line2)));
            hl->addWidget(line, 1);
        } else {
            hl->addStretch(1);
        }
    }
    return row;
}

QWidget *PairingPanel::buildPairCode()
{
    const Palette &p = Theme::instance().palette();
    auto *box = new QWidget(this);
    auto *vl = new QVBoxLayout(box);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(tokens::kS2);

    auto *label = new QLabel(tr("一次性配对码"), box);
    label->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    label->setStyleSheet(
        QStringLiteral("color:%1;text-transform:uppercase;letter-spacing:0.3px")
            .arg(css(p.text3)));
    vl->addWidget(label);

    // 设计稿 .paircode：虚线描边 + surface-2 底，里面四个 34×42 的数字槽，
    // 槽是 surface 底、r-ctl 圆角、底部 1px 阴影（box-shadow:0 1px 0 line-1，
    // Qt 样式表没有 box-shadow，用 border-bottom 等价代偿）。配对码要真生成需要
    // 传输层的密钥协商，所以这里画的是四个空槽而不是具体数字——摆一个"8421"
    // 会让人真去输一个根本不存在的码；槽心那个「·」是占位提示，说明这里待填数字。
    auto *codeBox = new QWidget(box);
    auto *hl = new QHBoxLayout(codeBox);
    hl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    hl->setSpacing(tokens::kS2);
    hl->addStretch(1);
    for (int i = 0; i < kPairCodeDigits; ++i) {
        auto *slot = new QLabel(codeBox);
        slot->setFixedSize(34, 42);
        slot->setAlignment(Qt::AlignCenter);
        slot->setFont(uiFont(tokens::kFont20, QFont::Bold));
        slot->setText(QStringLiteral("·"));
        slot->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px;color:%3;"
                                          "border-bottom:1px solid %4")
                                .arg(css(p.surface))
                                .arg(tokens::kRadiusCtl)
                                .arg(css(p.text3))
                                .arg(css(p.line1)));
        hl->addWidget(slot);
    }
    hl->addStretch(1);
    applyBoxStyle(codeBox,
                  QStringLiteral("border:1px dashed %1;border-radius:%2px;background:%3")
                      .arg(css(p.line2)).arg(tokens::kRadiusCard).arg(css(p.surface2)));
    vl->addWidget(codeBox);

    // 「5 分钟后失效」在左，「刷新」在右。
    auto *row = new QWidget(box);
    auto *rh = new QHBoxLayout(row);
    rh->setContentsMargins(0, 0, 0, 0);
    rh->setSpacing(tokens::kS2);
    auto *hint = new QLabel(tr("5 分钟后失效"), row);
    hint->setFont(uiFont(tokens::kFont11));
    hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    rh->addWidget(hint);
    rh->addStretch(1);
    rh->addWidget(unwiredButton(tr("刷新"), QStringLiteral("refresh"), tr("刷新配对码"),
                                 row, false));
    vl->addWidget(row);

    // 配对码要真生成需要传输层的密钥协商；这里明说边界，不假装已接通。
    auto *warn = new QWidget(box);
    auto *wl = new QHBoxLayout(warn);
    wl->setContentsMargins(0, tokens::kS1, 0, 0);
    wl->setSpacing(tokens::kS2);
    auto *icon = new QLabel(warn);
    icon->setFixedSize(14, 14);
    icon->setPixmap(icons::icon(QStringLiteral("warn"), 14, p.warn.solid).pixmap(14, 14));
    auto *text = new QLabel(
        tr("配对码由传输层密钥协商生成，传输层尚未实现，当前为占位"), warn);
    text->setWordWrap(true);
    text->setFont(uiFont(tokens::kFont11));
    text->setStyleSheet(QStringLiteral("color:%1").arg(css(p.warn.text)));
    wl->addWidget(icon, 0, Qt::AlignTop);
    wl->addWidget(text, 1);
    vl->addWidget(warn);
    return box;
}

QWidget *PairingPanel::buildDeviceList()
{
    auto *box = new QWidget(this);
    auto *vl = new QVBoxLayout(box);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(tokens::kS2);

    auto *title = new QLabel(tr("网络中的设备"), box);
    title->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    title->setStyleSheet(
        QStringLiteral("color:%1;text-transform:uppercase;letter-spacing:0.4px")
            .arg(css(Theme::instance().palette().text3)));
    vl->addWidget(title);

    auto *list = new QWidget(box);
    listLayout_ = new QVBoxLayout(list);
    listLayout_->setContentsMargins(0, 0, 0, 0);
    listLayout_->setSpacing(0);
    applyBoxStyle(list,
                  QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                      .arg(css(Theme::instance().palette().line2))
                      .arg(tokens::kRadiusCard)
                      .arg(css(Theme::instance().palette().surface)));
    vl->addWidget(list);
    return box;
}

// 设计稿 pairing.html 末尾的「手动添加」折叠区（默认收起，头部挂「高级」徽章）。
QWidget *PairingPanel::buildManualSection()
{
    const Palette &p = Theme::instance().palette();
    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    vl->setSpacing(tokens::kS2);

    const auto field = [body, &p](const QString &label, const QString &placeholder) {
        auto *wrap = new QWidget(body);
        auto *v = new QVBoxLayout(wrap);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);
        auto *text = new QLabel(label, wrap);
        text->setFont(uiFont(tokens::kFont11));
        text->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text2)));
        auto *edit = new QLineEdit(wrap);
        applyInputStyle(edit);
        edit->setPlaceholderText(placeholder);
        v->addWidget(text);
        v->addWidget(edit);
        return wrap;
    };
    vl->addWidget(field(tr("设备地址"), tr("192.168.1.50:7788")));
    vl->addWidget(field(tr("备注名"), tr("例如：书房主机")));
    vl->addWidget(unwiredButton(tr("连接"), QString(), tr("手动添加设备"), body, false));

    auto *badge = new Badge(tr("高级"), Badge::Variant::Neutral, this);
    auto *section = new Collapsible(tr("手动添加"), QStringLiteral("lan"), body, badge, this);
    section->setCollapsed(true);   // 设计稿 data-collapsed
    return section;
}

QWidget *PairingPanel::buildFooter()
{
    auto *foot = new QWidget(this);
    auto *hl = new QHBoxLayout(foot);
    hl->setContentsMargins(tokens::kContentPadding, 10, tokens::kContentPadding, 10);
    hl->setSpacing(tokens::kS2);

    // 设计稿停在第 2 步：左边「取消」，右边「第 2 步 / 共 3 步」+「完成」。
    auto *cancel = new FgButton(tr("取消"), FgButton::Variant::Ghost, true, foot);
    QObject::connect(cancel, &FgButton::clicked, this, &Panel::hide);
    hl->addWidget(cancel);
    hl->addStretch(1);

    auto *step = new QLabel(tr("第 2 步 / 共 3 步"), foot);
    step->setFont(uiFont(tokens::kFont11));
    step->setStyleSheet(QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
    hl->addWidget(step);

    auto *done = new FgButton(tr("完成"), FgButton::Variant::Accent, true, foot);
    // 完成 = 跳回设备与连接，由 main 统一做浮板互斥。
    QObject::connect(done, &FgButton::clicked, this, &PairingPanel::doneRequested);
    hl->addWidget(done);
    return foot;
}

void PairingPanel::setDevices(const QList<DiscoveredDevice> &devices)
{
    devices_ = devices;
    rebuildDevices();
}

void PairingPanel::rebuildDevices()
{
    if (listLayout_ == nullptr) {
        return;
    }
    clearLayout(listLayout_);
    const Palette &p = Theme::instance().palette();
    auto *host = listLayout_->parentWidget();

    if (devices_.isEmpty()) {
        auto *empty = new QLabel(tr("未发现设备"), host);
        empty->setFont(uiFont(tokens::kFont12));
        empty->setAlignment(Qt::AlignCenter);
        empty->setContentsMargins(0, tokens::kS5, 0, tokens::kS5);
        empty->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        listLayout_->addWidget(empty);
        return;
    }

    for (int i = 0; i < devices_.size(); ++i) {
        const DiscoveredDevice &d = devices_.at(i);

        // 设计稿的行首带设备类型图标（laptop / monitor / tablet）。ListRow 的主标题
        // 只收纯文本，所以这里自己拼一行：图标 + 名字。
        auto *title = new QWidget(host);
        auto *th = new QHBoxLayout(title);
        th->setContentsMargins(0, 0, 0, 0);
        th->setSpacing(tokens::kS2);
        auto *icon = new QLabel(title);
        icon->setFixedSize(14, 14);
        icon->setPixmap(icons::icon(d.icon, 14, d.online ? p.text2 : p.text3).pixmap(14, 14));
        auto *name = new QLabel(d.name, title);
        name->setFont(uiFont(tokens::kFont12));
        name->setStyleSheet(QStringLiteral("color:%1").arg(css(d.online ? p.text1 : p.text3)));
        th->addWidget(icon);
        th->addWidget(name);

        // 离线设备没有地址、没有信号，也没在跑 FlowGrid——给一个 idle 徽章，
        // 不给「发起配对」按钮，免得点了必然失败。
        const QString hint =
            d.online ? tr("%1 · %2 · 已发现 %3s").arg(d.address, signalText(d.signal)).arg(d.foundSecs)
                     : tr("未运行 FlowGrid");
        QWidget *trailing = nullptr;
        if (d.online) {
            auto *btn =
                new FgButton(tr("发起配对"),
                             i == 0 ? FgButton::Variant::Accent : FgButton::Variant::Secondary,
                             true, host);
            QObject::connect(btn, &FgButton::clicked, this,
                             [this, name = d.name] { Q_EMIT pairRequested(name); });
            trailing = btn;
        } else {
            trailing = new Badge(tr("离线"), Badge::Variant::Idle, host);
        }
        listLayout_->addWidget(new ListRow(QString(), hint, trailing, host, title));
    }
}

} // namespace fg
