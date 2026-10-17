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

#include <functional>

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

// 设计稿折叠区末尾的「说明 + 小按钮」一行：
// <div class="row between" style="padding:0 var(--s-3) var(--s-3)">
//   <span class="t-11 c-3">…</span><a class="btn btn--sm btn--secondary">…</a>
// 三个折叠区共用，抽出来免得各写一遍 padding 又各漂一点。
QWidget *makeSectionFooter(const QString &hint, const QString &buttonText, QWidget *parent,
                           const std::function<void(FgButton *)> &onClick)
{
    const Palette &p = Theme::instance().palette();
    auto *row = new QWidget(parent);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(tokens::kS3, 0, tokens::kS3, tokens::kS3);
    hl->setSpacing(tokens::kS2);

    auto *label = new QLabel(hint, row);
    label->setFont(uiFont(tokens::kFont11));
    label->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    hl->addWidget(label);
    hl->addStretch(1);

    auto *btn = new FgButton(buttonText, FgButton::Variant::Secondary, true, row);
    // 自由函数里没有 QObject 派生带来的 connect 重载，必须写全 QObject::connect。
    QObject::connect(btn, &FgButton::clicked, row, [onClick, btn] { onClick(btn); });
    hl->addWidget(btn);
    return row;
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
    // 折叠区默认展开，而第一次真实采样要等到下一个周期才到：不做铺底的话
    // 「网络延迟」一打开就是一块 70px 的空白。prime() 只在构造时调一次，
    // 之后一律由 onSampled 推真实样本。
    chart_->prime(manager.activeLatencyMs());
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
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // 设计稿 ui.css .mapping：3 列网格，gap 8，padding 12；每格是 .keypair
    // （竖排，gap 5，padding 8）= [源键]→[目标键] + 下方一行说明。
    // 早先这里摆的是「2 列 × 3 行裸键帽」，既丢了箭头也丢了说明文字，
    // 而且 QLabel 在网格里会被拉伸到整格宽，键帽变成横贯屏幕的长条。
    struct Pair { const char *from; const char *to; const char *ctx; };
    const Pair pairs[3] = {
        {"Ctrl", "\xe2\x8c\x98", QT_TR_NOOP("复制 / 粘贴")},
        {"Win", "\xe2\x8c\xa5", QT_TR_NOOP("系统快捷键")},
        {"Alt", "Ctrl", QT_TR_NOOP("终端应用")},
    };

    auto *grid = new QWidget(body);
    auto *gl = new QGridLayout(grid);
    gl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    gl->setHorizontalSpacing(tokens::kS2);
    gl->setVerticalSpacing(tokens::kS2);

    const Palette &p = Theme::instance().palette();
    for (int i = 0; i < 3; ++i) {
        auto *cell = new QWidget(grid);
        auto *cl = new QVBoxLayout(cell);
        cl->setContentsMargins(tokens::kS2, tokens::kS2, tokens::kS2, tokens::kS2);
        cl->setSpacing(5);   // ui.css .keypair gap

        auto *keys = new QWidget(cell);
        auto *kl = new QHBoxLayout(keys);
        kl->setContentsMargins(0, 0, 0, 0);
        kl->setSpacing(5);   // ui.css .keys gap
        kl->addWidget(new FgKeycap(QString::fromUtf8(pairs[i].from), false, keys));
        auto *arrow = new QLabel(keys);
        arrow->setFixedSize(12, 12);
        arrow->setPixmap(icons::icon(QStringLiteral("arrowRight"), 12, p.text3).pixmap(12, 12));
        kl->addWidget(arrow, 0, Qt::AlignVCenter);
        kl->addWidget(new FgKeycap(QString::fromUtf8(pairs[i].to), false, keys));
        kl->addStretch(1);
        cl->addWidget(keys);

        auto *ctx = new QLabel(tr(pairs[i].ctx), cell);
        ctx->setFont(uiFont(tokens::kFont11));
        ctx->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        cl->addWidget(ctx);

        gl->addWidget(cell, 0, i);
    }
    vl->addWidget(grid);

    // 设计稿：映射网格下面还有一行「按应用切换映射，游戏内自动停用」+「编辑」。
    vl->addWidget(makeSectionFooter(tr("按应用切换映射，游戏内自动停用"), tr("编辑"), body,
                                    [this](FgButton *) { Q_EMIT keymapRequested(); }));

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
    // 设计稿 ui.css .chart__axis：两端各一个时间标记（connections 是 24s 窗口，
    // latency 浮板是 60s 窗口）。采样节奏那句话归下面的页脚行，别在这儿重复。
    auto *left = new QLabel(tr("-24s"), axis);
    auto *right = new QLabel(tr("现在"), axis);
    for (QLabel *l : {left, right}) {
        l->setFont(uiFont(tokens::kFont11));
        l->setStyleSheet(
            QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
    }
    al->addWidget(left);
    al->addStretch(1);
    al->addWidget(right);
    vl->addWidget(axis);

    // 设计稿：图表区下面一行「每 2 秒采样一次」+「详情」。这里左侧文案和上面
    // 的轴行是同一句，保留轴行放「采样历史 / 现在」，把采样节奏交给这行。
    vl->addWidget(makeSectionFooter(tr("每 2 秒采样一次"), tr("详情"), body,
                                    [this](FgButton *) { Q_EMIT latencyRequested(); }));

    latencyBadge_ = new Badge(tr("均值 --"), Badge::Variant::Ok);
    return new Collapsible(tr("网络延迟"), QStringLiteral("activity"), body, latencyBadge_);
}

QWidget *ConnectionsPanel::buildClipboardSection()
{
    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(0, tokens::kS2, 0, tokens::kS2);
    vl->setSpacing(tokens::kS1);

    // 设计稿这一区列的是最近几条已同步的剪贴板条目。Go 侧 ClipboardSession 只
    // 提供一次性出队的 takePending，没有历史留存；剪贴板浮板已经把那批条目收走
    // 并展示了，所以这里不再重复列一遍，也不再挂「cgo 桥尚未导出」这种早就过时
    // 的说明——保留一个入口指向剪贴板浮板即可。
    const Palette &p = Theme::instance().palette();
    auto *note = new QLabel(tr("最近同步的条目在剪贴板浮板内查看"), body);
    note->setWordWrap(true);
    note->setFont(uiFont(tokens::kFont11));
    note->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    vl->addWidget(note);

    vl->addWidget(makeSectionFooter(QString(), tr("打开剪贴板"), body,
                                    [this](FgButton *) { Q_EMIT clipboardRequested(); }));

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
    clearLayout(deviceListLayout_);
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
    updateLatencyBadge();
}

void ConnectionsPanel::updateLatencyBadge()
{
    if (latencyBadge_ == nullptr) {
        return;
    }
    // 均值徽章跟着真实采样算，不写死；首帧也要算一次，否则折叠区一展开就摆着
    // 「均值 --」直到下一个采样周期。
    double sum = 0.0;
    int n = 0;
    for (const Device &d : DeviceManager::instance().devices()) {
        if (d.online) {
            sum += d.latencyMs;
            ++n;
        }
    }
    latencyBadge_->setText(n > 0 ? tr("均值 %1ms").arg(sum / n, 0, 'f', 1) : tr("无数据"));
    latencyBadge_->setVariant(Badge::Variant::Ok);
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
    updateLatencyBadge();
}

} // namespace fg
