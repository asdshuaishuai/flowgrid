#include "pairingpanel.h"
#include "core/designtokens.h"
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

QString signalText(int level)
{
    switch (level) {
    case 0:  return QObject::tr("信号弱");
    case 1:  return QObject::tr("信号中");
    default: return QObject::tr("信号强");
    }
}

} // namespace

PairingPanel::PairingPanel(QWidget *parent) : Panel(tr("添加设备"), parent)
{
    auto *body = qobject_cast<QVBoxLayout *>(content()->layout());

    body->addWidget(buildSteps());
    body->addWidget(buildPairCode());
    body->addWidget(buildDeviceList());
    setFooterWidget(buildFooter());

    setDevices({
        {QStringLiteral("deepin-laptop"), QStringLiteral("192.168.x.x"), 3, 8, false},
        {QStringLiteral("office-pc"), QStringLiteral("192.168.x.x"), 2, 24, false},
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

        auto *num = new QLabel(QString::number(i + 1), step);
        num->setFixedSize(22, 22);
        num->setAlignment(Qt::AlignCenter);
        num->setFont(uiFont(tokens::kFont12, QFont::Bold));
        // 当前步骤高亮，其余走中性色——设计稿的 .step__num
        const bool active = (i == 0);
        num->setStyleSheet(
            QStringLiteral("background:%1;color:%2;border-radius:11px")
                .arg(css(active ? p.accent : p.surface3),
                     css(active ? p.onAccent : p.text3)));

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
    label->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    vl->addWidget(label);

    // 配对码框：设计稿 .paircode，等宽大字符
    auto *codeBox = new QWidget(box);
    auto *hl = new QHBoxLayout(codeBox);
    hl->setContentsMargins(tokens::kS3, tokens::kS2, tokens::kS3, tokens::kS2);
    auto *value = new QLabel(QStringLiteral("···· ····"), codeBox);
    value->setFont(uiFont(tokens::kFont20, QFont::Bold));
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));
    auto *copy = new IconButton(QStringLiteral("copy"), tr("复制配对码"), codeBox);
    hl->addWidget(value);
    hl->addStretch(1);
    hl->addWidget(copy);
    applyBoxStyle(codeBox,
                  QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                      .arg(css(p.line2)).arg(tokens::kRadiusCard).arg(css(p.surface)));
    vl->addWidget(codeBox);

    auto *hint = new QLabel(tr("5 分钟后失效"), box);
    hint->setFont(uiFont(tokens::kFont11));
    hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    vl->addWidget(hint);

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
    wl->addWidget(icon);
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
    vl->addWidget(list);
    return box;
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
    while (listLayout_->count() > 0) {
        QLayoutItem *item = listLayout_->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    const Palette &p = Theme::instance().palette();
    auto *host = listLayout_->parentWidget();

    if (devices_.isEmpty()) {
        auto *empty = new QLabel(tr("未发现设备"), host);
        empty->setFont(uiFont(tokens::kFont12));
        empty->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        listLayout_->addWidget(empty);
        return;
    }

    for (const DiscoveredDevice &d : devices_) {
        auto *btn = new FgButton(tr("发起配对"), FgButton::Variant::Secondary, true, host);
        QObject::connect(btn, &FgButton::clicked, this,
                         [this, name = d.name] { Q_EMIT pairRequested(name); });

        // 副标题直接用字符串：早先这里建了个只为取 text() 的 QLabel 挂在 host 上，
        // 没人把它加进布局，于是它浮在列表左上角、和第一个设备名叠在一起。
        const QString hint =
            tr("%1 · %2 · 已发现 %3s").arg(d.address, signalText(d.signal)).arg(d.foundSecs);
        listLayout_->addWidget(new ListRow(d.name, hint, btn, host));
    }
}

} // namespace fg
