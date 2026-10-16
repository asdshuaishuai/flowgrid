#include "connectionspanel.h"
#include "core/designtokens.h"
#include "core/devicemanager.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QApplication>
#include <QGridLayout>
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

// 必须带 alpha：柔和底色（ok-soft / accent-soft）就是靠 12%~18% 透明度
// 叠出来的。用 HexRgb 丢 alpha 会把它们渲染成实色，整个设计稿的浅底全废。
QString css(const QColor &c)
{
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

} // namespace

ConnectionsPanel::ConnectionsPanel(QWidget *parent) : Panel(tr("设备与连接"), parent)
{
    contentLayout_ = qobject_cast<QVBoxLayout *>(content()->layout());

    // ── 顶部一行：状态胶囊 + 添加设备 ──
    {
        auto *row = new QWidget(this);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(tokens::kS3);

        auto *pill = new QWidget(row);
        auto *pl = new QHBoxLayout(pill);
        pl->setContentsMargins(8, 0, 8, 0);
        pl->setSpacing(6);
        statusDot_ = new StatusDot(pill);
        statusLabel_ = new QLabel(tr("已连接"), pill);
        statusLabel_->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
        pl->addWidget(statusDot_);
        pl->addWidget(statusLabel_);
        pill->setStyleSheet(QStringLiteral("background:%1;border-radius:10px")
                                .arg(css(Theme::instance().palette().ok.soft)));
        hl->addWidget(pill);
        hl->addStretch(1);

        auto *addBtn = new FgButton(tr("添加设备"), FgButton::Variant::Accent, false, row);
        connect(addBtn, &FgButton::clicked, this, [this] {
            QMetaObject::invokeMethod(this, "addRequested");
        });
        hl->addWidget(addBtn);

        contentLayout_->addWidget(row);
    }

    contentLayout_->addWidget(buildDeviceList());
    contentLayout_->addWidget(buildKeymapSection());
    contentLayout_->addWidget(buildLatencySection());
    contentLayout_->addWidget(buildClipboardSection());
    setFooterWidget(buildFooter());

    auto &manager = DeviceManager::instance();
    connect(&manager, &DeviceManager::devicesChanged, this, &ConnectionsPanel::refresh);
    connect(&manager, &DeviceManager::sampled, this, &ConnectionsPanel::onSampled);

    refresh();
}

QWidget *ConnectionsPanel::buildDeviceList()
{
    deviceList_ = new QWidget(this);
    deviceListLayout_ = new QVBoxLayout(deviceList_);
    deviceListLayout_->setContentsMargins(0, 0, 0, 0);
    deviceListLayout_->setSpacing(tokens::kS3);
    return deviceList_;
}

QWidget *ConnectionsPanel::buildKeymapSection()
{
    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    vl->setSpacing(tokens::kS2);

    // 键位映射屏尚未接通，这里按设计稿摆出静态结构并注明，不接假交互。
    auto *grid = new QWidget(body);
    auto *gl = new QGridLayout(grid);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setHorizontalSpacing(tokens::kS2);
    gl->setVerticalSpacing(tokens::kS2);
    const char *pairs[3][2] = {{"Ctrl", "Cmd"}, {"Win", "Alt"}, {"Alt", "Ctrl"}};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 2; ++c) {
            auto *key = new QLabel(QString::fromUtf8(pairs[r][c]), grid);
            key->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
            key->setAlignment(Qt::AlignCenter);
            key->setMinimumWidth(34);
            key->setStyleSheet(
                QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3px;color:%4")
                    .arg(css(Theme::instance().palette().line2))
                    .arg(tokens::kRadiusCtl)
                    .arg(css(Theme::instance().palette().surface))
                    .arg(css(Theme::instance().palette().text1)));
            gl->addWidget(key, r, c * 2);
        }
    }
    vl->addWidget(grid);

    auto *note = new QLabel(tr("键位映射屏尚未接通，此处为设计稿静态结构"), body);
    note->setFont(uiFont(tokens::kFont11));
    note->setStyleSheet(
        QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
    vl->addWidget(note);

    auto *badge = new Badge(tr("3 条生效"), Badge::Variant::Neutral);
    return new Collapsible(tr("键位映射"), QStringLiteral("keyboard"), body, badge);
}

QWidget *ConnectionsPanel::buildLatencySection()
{
    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    chart_ = new BarChart(body);
    vl->addWidget(chart_);

    auto *axis = new QWidget(body);
    auto *al = new QHBoxLayout(axis);
    al->setContentsMargins(tokens::kS3, 0, tokens::kS3, tokens::kS3);
    auto *left = new QLabel(tr("采样历史"), axis);
    left->setFont(uiFont(tokens::kFont11));
    left->setStyleSheet(
        QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
    auto *right = new QLabel(tr("每 2 秒采样一次"), axis);
    right->setFont(uiFont(tokens::kFont11));
    right->setStyleSheet(
        QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
    al->addWidget(left);
    al->addStretch(1);
    al->addWidget(right);
    vl->addWidget(axis);

    latencyBadge_ = new Badge(tr("均值 --"), Badge::Variant::Ok);
    return new Collapsible(tr("网络延迟"), QStringLiteral("activity"), body, latencyBadge_);
}

QWidget *ConnectionsPanel::buildClipboardSection()
{
    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(tokens::kS2, tokens::kS2, tokens::kS2, tokens::kS2);
    vl->setSpacing(tokens::kS1);

    auto *note = new QLabel(tr("剪贴板屏尚未接通，且 cgo 桥尚未导出 ClipboardSession"), body);
    note->setWordWrap(true);
    note->setFont(uiFont(tokens::kFont11));
    note->setStyleSheet(
        QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
    vl->addWidget(note);

    auto *badge = new Badge(QStringLiteral("0 项"), Badge::Variant::Neutral);
    return new Collapsible(tr("剪贴板同步"), QStringLiteral("clipboard"), body, badge);
}

QWidget *ConnectionsPanel::buildFooter()
{
    const Palette &p = Theme::instance().palette();
    auto *foot = new QWidget(this);
    auto *hl = new QHBoxLayout(foot);
    hl->setContentsMargins(tokens::kContentPadding, 10, tokens::kContentPadding, 10);
    hl->setSpacing(tokens::kS2);

    auto *icon = new QLabel(foot);
    icon->setFixedSize(14, 14);
    icon->setPixmap(icons::icon(QStringLiteral("shield"), 14, p.text3).pixmap(14, 14));
    auto *hint = new QLabel(tr("端到端加密 · IntelligentLink 优先"), foot);
    hint->setFont(uiFont(tokens::kFont11));
    hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    hl->addWidget(icon);
    hl->addWidget(hint);
    hl->addStretch(1);

    auto *toSettings = new FgButton(tr("设置"), FgButton::Variant::Secondary, true, foot);
    toSettings->setIcon(QStringLiteral("settings"));
    connect(toSettings, &FgButton::clicked, this, &ConnectionsPanel::settingsRequested);
    hl->addWidget(toSettings);

    auto *close = new FgButton(tr("关闭"), FgButton::Variant::Ghost, true, foot);
    connect(close, &FgButton::clicked, this, &Panel::hide);
    hl->addWidget(close);
    return foot;
}

void ConnectionsPanel::rebuildDeviceCards()
{
    if (deviceListLayout_ == nullptr) {
        return;
    }
    while (deviceListLayout_->count() > 0) {
        QLayoutItem *item = deviceListLayout_->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    const QList<Device> &devices = DeviceManager::instance().devices();
    if (devices.isEmpty()) {
        auto *empty = new QLabel(tr("尚未发现设备"), deviceList_);
        empty->setFont(uiFont(tokens::kFont12));
        empty->setStyleSheet(
            QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
        deviceListLayout_->addWidget(empty);
        return;
    }
    for (const Device &d : devices) {
        auto *card = new DeviceCard(d, deviceList_);
        connect(card, &DeviceCard::switchRequested, this, [](const QString &id) {
            DeviceManager::instance().setActive(id);
        });
        connect(card, &DeviceCard::actionRequested, this,
                [](const QString &, const QString &) { /* 重连 / 更多待传输层落地 */ });
        deviceListLayout_->addWidget(card);
    }
}

void ConnectionsPanel::refresh()
{
    auto &manager = DeviceManager::instance();
    const Device *active = manager.activeDevice();
    const bool online = active != nullptr && active->online;

    if (statusLabel_ != nullptr) {
        statusLabel_->setText(active != nullptr ? tr("已连接 %1").arg(active->name)
                                                : tr("未连接"));
        const StatusColor sc = Theme::instance().color(
            Theme::qualityOf(manager.activeLatencyMs(), online));
        statusLabel_->setStyleSheet(QStringLiteral("color:%1").arg(css(sc.text)));
        statusDot_->setQuality(Theme::qualityOf(manager.activeLatencyMs(), online));
        statusDot_->setPulsing(online);
    }

    rebuildDeviceCards();
}

void ConnectionsPanel::onSampled()
{
    const double ms = DeviceManager::instance().activeLatencyMs();
    if (chart_ == nullptr) {
        return;
    }
    if (ms < 0.0) {
        return;   // 离线：图表保留最后形状，不塞 0 假装有链路
    }
    chart_->pushSample(ms);

    // 均值徽章跟随真实采样算，不写死。
    if (latencyBadge_ != nullptr) {
        double sum = 0.0;
        int n = 0;
        for (const Device &d : DeviceManager::instance().devices()) {
            if (d.online) {
                sum += d.latencyMs;
                ++n;
            }
        }
        latencyBadge_->setText(n > 0 ? tr("均值 %1ms").arg(sum / n, 0, 'f', 1)
                                    : tr("无数据"));
        latencyBadge_->setVariant(Badge::Variant::Ok);
    }
}

} // namespace fg
