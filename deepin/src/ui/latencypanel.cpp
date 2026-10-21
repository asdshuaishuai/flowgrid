#include "latencypanel.h"
#include "core/designtokens.h"
#include "core/devicemanager.h"
#include "core/linkprobe.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTextStream>
#include <QLoggingCategory>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcLatency, "flowgrid.latency")

namespace fg {
namespace {

// 必须带 alpha：柔和底色（ok-soft / accent-soft）就是靠 12%~18% 透明度
// 叠出来的。用 HexRgb 丢 alpha 会把它们渲染成实色，整个设计稿的浅底全废。
QString css(const QColor &c)
{
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

} // namespace

LatencyPanel::LatencyPanel(QWidget *parent) : Panel(tr("网络延迟"), parent)
{
    contentLayout_ = qobject_cast<QVBoxLayout *>(content()->layout());

    // ── 标题栏右侧状态胶囊 ──
    {
        auto *pill = new QWidget(this);
        pill->setFixedHeight(tokens::kStatusPillHeight);  // 圆角取高的一半
        auto *pl = new QHBoxLayout(pill);
        pl->setContentsMargins(8, 0, 8, 0);
        pl->setSpacing(6);
        titleDot_ = new StatusDot(pill);
        titleLatency_ = new LatencyChip();
        titleLatency_->setFixedHeight(tokens::kStatusPillHeight);
        pl->addWidget(titleDot_);
        pl->addWidget(titleLatency_);
        pill->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px")
                                .arg(css(Theme::instance().palette().ok.soft),
                                     QString::number(tokens::pillRadius(tokens::kStatusPillHeight))));
        setTitleBarWidget(pill);
    }

    // ── 大数字读数 + 链路 ──
    {
        auto *row = new QWidget(this);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(tokens::kS3);

        auto *col = new QVBoxLayout;
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(2);
        auto *caption = new QLabel(tr("当前延迟"), row);
        caption->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
        caption->setStyleSheet(
            QStringLiteral("color:%1;text-transform:uppercase;letter-spacing:0.3px")
                .arg(css(Theme::instance().palette().text3)));
        bigValue_ = new QLabel(QStringLiteral("--"), row);
        // 设计稿写死 .t-28 w-bold mono：读数每 2 秒跳一次，不等宽整行会左右抖。
        bigValue_->setFont(uiMonoFont(tokens::kFont28, QFont::Bold));
        col->addWidget(caption);
        col->addWidget(bigValue_);

        // 读数底下那行小字：**说明这个数是量到谁的**。
        // 传输层没落地时测的是本机到默认网关的真实往返（ICMP RTT÷2），
        // 不写清楚就容易被当成「到某台对端的延迟」。
        measureNote_ = new QLabel(row);
        measureNote_->setFont(uiFont(tokens::kFont11));
        measureNote_->setStyleSheet(
            QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
        col->addWidget(measureNote_);

        // 设计稿：这一行是 align-items:flex-end——两列**底对齐**。
        // 早先两列都是顶对齐，链路名（大数字那列更高）被挤到行底之外，
        // 文字下半截被裁掉一条。
        auto *linkCol = new QVBoxLayout;
        linkCol->setContentsMargins(0, 0, 0, 0);
        linkCol->setSpacing(2);
        auto *linkCaption = new QLabel(tr("链路"), row);
        linkCaption->setFont(uiFont(tokens::kFont11));
        linkCaption->setStyleSheet(
            QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
        linkCaption->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // 设计稿里链路值是 [wifi 图标] + 名称，整体右对齐
        auto *linkRow = new QWidget(row);
        auto *lrl = new QHBoxLayout(linkRow);
        lrl->setContentsMargins(0, 0, 0, 0);
        lrl->setSpacing(tokens::kS1);
        linkLabel_ = new QLabel(QStringLiteral("—"), linkRow);
        linkLabel_->setFont(uiFont(tokens::kFont12, QFont::DemiBold));
        linkLabel_->setStyleSheet(
            QStringLiteral("color:%1").arg(css(Theme::instance().palette().text1)));
        // mac 端：连上了是 lan，没连上是 wifi，尺寸 16（设计稿 .ic 默认档）、
        // 字色 text1。这里原来固定画一个 12px 的 wifi，跟「链路已通」对不上。
        linkIcon_ = new QLabel(linkRow);
        linkIcon_->setFixedSize(tokens::kIconMd, tokens::kIconMd);
        linkIcon_->setPixmap(icons::icon(QStringLiteral("wifi"), tokens::kIconMd,
                                         Theme::instance().palette().text1)
                                 .pixmap(tokens::kIconMd, tokens::kIconMd));
        lrl->addStretch(1);
        lrl->addWidget(linkIcon_);
        lrl->addWidget(linkLabel_);
        linkCol->addWidget(linkCaption);
        linkCol->addWidget(linkRow);

        // QHBoxLayout::addLayout 不收对齐参数（那是 QGridLayout 才有的重载），
        // 要让两列底对齐只能各包一层 QWidget 再用 addWidget(..., alignment)。
        auto *colBox = new QWidget(row);
        colBox->setLayout(col);
        hl->addWidget(colBox, 0, Qt::AlignBottom);
        hl->addStretch(1);
        auto *linkBox = new QWidget(row);
        linkBox->setLayout(linkCol);
        hl->addWidget(linkBox, 0, Qt::AlignBottom);
        contentLayout_->addWidget(row);
    }

    // ── 柱状图折叠区（默认展开） ──
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
        auto *left = new QLabel(QStringLiteral("-60s"), axis);
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

        // 没有可采样的设备时，图表下面补一行提示（mac 端是 FGEmptyHint
        // 「连接设备后开始采样」）。早先这里只有一块空画布，用户不知道
        // 该连设备还是该等——图上写「等待采样」也没说清楚在等什么。
        auto *hintBox = new QWidget(body);
        auto *hbl = new QHBoxLayout(hintBox);
        hbl->setContentsMargins(tokens::kS3, tokens::kS2, tokens::kS3, tokens::kS2);
        hbl->setSpacing(tokens::kS1);
        auto *hintIcon = new QLabel(hintBox);
        hintIcon->setFixedSize(tokens::kIconXs, tokens::kIconXs);
        hintIcon->setPixmap(icons::icon(QStringLiteral("info"), tokens::kIconXs,
                                        Theme::instance().palette().text3)
                                .pixmap(tokens::kIconXs, tokens::kIconXs));
        auto *hintText = new QLabel(tr("连接设备后开始采样"), hintBox);
        hintText->setFont(uiFont(tokens::kFont11));
        hintText->setStyleSheet(
            QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
        hbl->addWidget(hintIcon);
        hbl->addWidget(hintText);
        hbl->addStretch(1);
        vl->addWidget(hintBox);
        chartHint_ = hintBox;

        chartBadge_ = new Badge(tr("均值 —"), Badge::Variant::Ok);
        auto *section = new Collapsible(tr("近 60 秒采样"), QStringLiteral("activity"), body,
                                        chartBadge_);
        contentLayout_->addWidget(section);
    }

    // ── 分设备 ──
    {
        const Palette &p = Theme::instance().palette();
        auto *card = new QWidget(this);
        deviceListLayout_ = new QVBoxLayout(card);
        deviceListLayout_->setContentsMargins(0, 0, 0, 0);
        deviceListLayout_->setSpacing(0);

        deviceListLayout_->addWidget(groupTitle(tr("分设备"), card));

        deviceRows_ = new QVBoxLayout;
        deviceRows_->setContentsMargins(0, 0, 0, 0);
        deviceRows_->setSpacing(0);
        deviceListLayout_->addLayout(deviceRows_);

        applyBoxStyle(card,
                      QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                          .arg(css(p.line2))
                          .arg(tokens::kRadiusCard)
                          .arg(css(p.card)));
        contentLayout_->addWidget(card);
    }

    contentLayout_->addWidget(buildThresholdSection());
    setFooterWidget(buildFooter());

    auto &manager = DeviceManager::instance();
    connect(&manager, &DeviceManager::devicesChanged, this, &LatencyPanel::refresh);
    connect(&manager, &DeviceManager::sampled, this, &LatencyPanel::onSampled);

    refresh();
    // 图表**不从假数据起手**。早先这里调 prime()，按 mock 源的抖动带铺一组
    // 占位样本，理由是「不然一打开就是一块 70px 的空白」——可那些柱子是从没
    // 量过的数，12 根齐平，看着像在动其实全是编的。现在图表从空开始，随真实
    // 采样一根根长出来（每 2 秒一根，24 秒填满），空的时候画布上写「等待采样」。
    // 徽章初值也跟着诚实：没有样本就是「均值 --」。
}

QWidget *LatencyPanel::buildThresholdSection()
{
    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    vl->setSpacing(tokens::kS3);

    const Palette &p = Theme::instance().palette();
    auto makeRow = [&](const QString &label, QWidget *control, const QString &unit) {
        auto *row = new QWidget(body);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(tokens::kS3);
        auto *text = new QLabel(label, row);
        text->setFont(uiFont(tokens::kFont12));
        text->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));
        auto *unitLabel = new QLabel(unit, row);
        unitLabel->setFont(uiFont(tokens::kFont11));
        unitLabel->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        hl->addWidget(text);
        hl->addStretch(1);
        hl->addWidget(control);
        hl->addWidget(unitLabel);
        vl->addWidget(row);
    };

    // 表头徽章在折叠状态下是这一区唯一可见的数值：输入框改成 50 而徽章停在
    // 20ms，展开前后就自相矛盾。让它跟着输入走（本区只作展示，没有落盘）。
    auto *badge = new Badge(QStringLiteral("20ms"), Badge::Variant::Neutral);

    auto *threshold = new QLineEdit(QStringLiteral("20"), body);
    threshold->setFixedWidth(64);
    QObject::connect(threshold, &QLineEdit::textChanged, badge, [badge](const QString &t) {
        badge->setText(t.isEmpty() ? QStringLiteral("—") : tr("%1ms").arg(t));
    });
    auto *times = new QLineEdit(QStringLiteral("3"), body);
    times->setFixedWidth(64);
    makeRow(tr("告警阈值"), threshold, tr("ms"));
    makeRow(tr("连续超阈值次数"), times, tr("次"));

    auto *toggle = new ToggleSwitch(body);
    toggle->setChecked(true);
    makeRow(tr("超阈值时自动切换链路"), toggle, QString());

    auto *note = new QLabel(
        tr("阈值当前仅作展示：自动切链需要传输层落地后才能生效"), body);
    note->setWordWrap(true);
    note->setFont(uiFont(tokens::kFont11));
    note->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    vl->addWidget(note);

    auto *section = new Collapsible(tr("告警阈值"), QStringLiteral("settings"), body, badge);
    section->setCollapsed(true);   // 设计稿 data-collapsed
    return section;
}

QWidget *LatencyPanel::buildFooter()
{
    const Palette &p = Theme::instance().palette();
    auto *foot = new QWidget(this);
    auto *hl = new QHBoxLayout(foot);
    hl->setContentsMargins(tokens::kContentPadding, 10, tokens::kContentPadding, 10);
    hl->setSpacing(tokens::kS2);

    auto *icon = new QLabel(foot);
    icon->setFixedSize(tokens::kIconSm, tokens::kIconSm);
    icon->setPixmap(icons::icon(QStringLiteral("info"), tokens::kIconSm, p.text3).pixmap(tokens::kIconSm, tokens::kIconSm));
    auto *hint = new QLabel(tr("每 2 秒采样一次"), foot);
    hint->setFont(uiFont(tokens::kFont11));
    hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    hl->addWidget(icon);
    hl->addWidget(hint);
    hl->addStretch(1);

    auto *exportDiag = new FgButton(tr("导出诊断"), FgButton::Variant::Secondary, true, foot);
    exportDiag->setIcon(QStringLiteral("copy"));
    connect(exportDiag, &FgButton::clicked, this, [this] {
        // 导出要写文件到用户目录，等用户主动点的时候再问存哪；
        // 当前先落地一份带时间戳的纯文本，够定位链路问题。
        const QString path = QDir(QDir::homePath())
                                 .filePath(tr("flowgrid-延迟诊断-%1.txt")
                                               .arg(QDateTime::currentDateTime()
                                                        .toString(Qt::ISODate)
                                                        .replace(QLatin1Char(':'),
                                                                 QLatin1Char('-'))));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qCWarning(lcLatency) << "导出诊断失败" << f.errorString();
            return;
        }
        const DeviceManager &m = DeviceManager::instance();
        QTextStream out(&f);
        out << "FlowGrid 延迟诊断 " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        out << "设备数 " << m.devices().size() << "\n";
        for (const Device &d : m.devices()) {
            out << "  " << d.name << " | " << (d.online ? "在线" : "离线")
                << " | " << QString::number(d.latencyMs, 'f', 2) << "ms"
                << " | " << d.platformLabel << " | " << linkName(d.link) << "\n";
        }
        qCInfo(lcLatency) << "诊断报告已导出" << path;
    });
    hl->addWidget(exportDiag);

    auto *close = new FgButton(tr("关闭"), FgButton::Variant::Ghost, true, foot);
    connect(close, &FgButton::clicked, this, &Panel::hide);
    hl->addWidget(close);
    return foot;
}

void LatencyPanel::refresh()
{
    auto &manager = DeviceManager::instance();
    const Device *active = manager.activeDevice();
    // 有读数 ≠ 设备在线：读数是「没测到」时是 --，不是 0.0ms。
    const double ms = manager.activeLatencyMs();
    const bool hasReading = ms >= 0.0;
    const Quality q = Theme::qualityOf(ms, hasReading);

    if (bigValue_ != nullptr) {
        bigValue_->setText(hasReading ? QStringLiteral("%1ms").arg(ms, 0, 'f', 1)
                                      : QStringLiteral("—"));
        bigValue_->setStyleSheet(
            QStringLiteral("color:%1").arg(css(Theme::instance().color(q).text)));
    }
    if (linkLabel_ != nullptr) {
        // mac 端：连上了显示链路名，没连上写「未连接」。
        linkLabel_->setText(active != nullptr && active->online ? linkName(active->link)
                                                                : tr("未连接"));
    }
    if (linkIcon_ != nullptr) {
        const bool connected = active != nullptr && active->online;
        linkIcon_->setPixmap(icons::icon(connected ? QStringLiteral("lan")
                                                   : QStringLiteral("wifi"),
                                         tokens::kIconMd,
                                         Theme::instance().palette().text1)
                                 .pixmap(tokens::kIconMd, tokens::kIconMd));
    }
    if (chartHint_ != nullptr) {
        // 没有可采样的设备才提示；有设备时这行字是多余的。
        chartHint_->setVisible(active == nullptr);
    }
    if (measureNote_ != nullptr) {
        // 这一行必须说清「量的是谁」。传输层落地前没有对端，测的是本机到
        // 默认网关的 ICMP 往返；照实写，别让用户以为是到某台对端的延迟。
        const QString gw = LinkProbe::instance()->target();
        if (gw.isEmpty()) {
            measureNote_->setText(tr("无默认路由，链路延迟不可测"));
        } else if (hasReading) {
            measureNote_->setText(tr("实测网关 %1 · ICMP RTT÷2").arg(gw));
        } else {
            measureNote_->setText(tr("等待对 %1 的采样").arg(gw));
        }
    }
    if (titleDot_ != nullptr) {
        titleDot_->setQuality(q);
        titleDot_->setPulsing(hasReading);
    }
    if (titleLatency_ != nullptr) {
        titleLatency_->setLatency(ms, hasReading);
    }

    // 分设备列表
    if (deviceRows_ != nullptr) {
        clearLayout(deviceRows_);
        const Palette &p = Theme::instance().palette();
        const QList<Device> &devices = manager.devices();
        if (devices.isEmpty()) {
            auto *empty = new QLabel(tr("暂无设备"), this);
            empty->setFont(uiFont(tokens::kFont12));
            empty->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
            empty->setAlignment(Qt::AlignCenter);
            empty->setContentsMargins(0, tokens::kS5, 0, tokens::kS5);
            deviceRows_->addWidget(empty);
        }

        for (const Device &d : devices) {
            // mac 端那一行是「本机」/「离线」/「系统 · 链路 · 已连接」三选一。
            // 这里原来还给本机行也缀上链路与 IP，一行挤四段字；地址与丢包率
            // 归连接屏的设备卡管，这一行按 mac 端说清楚状态就够。
            QString hint = tr("离线");
            if (d.isLocal) {
                hint = tr("本机");
            } else if (d.online) {
                hint = d.platformLabel + QStringLiteral(" · ") + linkName(d.link)
                       + tr(" · 已连接");
            }
            auto *lat = new LatencyChip();
            lat->setLatency(d.latencyMs, d.latencyMs >= 0.0);

            // 设计稿每行名字前面带设备类型图标（笔记本 / 台式机 / 平板）。
            auto *title = new QWidget(this);
            auto *tl = new QHBoxLayout(title);
            tl->setContentsMargins(0, 0, 0, 0);
            tl->setSpacing(tokens::kS2);
            auto *icon = new QLabel(title);
            icon->setFixedSize(tokens::kIconMd, tokens::kIconMd);
            icon->setPixmap(icons::icon(d.icon, tokens::kIconMd, d.online ? p.text2 : p.text3)
                                .pixmap(tokens::kIconMd, tokens::kIconMd));
            tl->addWidget(icon);
            auto *name = new QLabel(d.name, title);
            // mac 端设备名是 12px 常规字重（不是半粗），字重重一档会显得
            // 比 mac 端更抢眼。
            name->setFont(uiFont(tokens::kFont12));
            name->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));
            tl->addWidget(name);

            deviceRows_->addWidget(new ListRow(QString(), hint, lat, this, title));
        }
    }
}

void LatencyPanel::onSampled()
{
    const double ms = DeviceManager::instance().activeLatencyMs();
    if (ms < 0.0 || chart_ == nullptr) {
        return;
    }
    chart_->pushSample(ms);
    refreshChartBadge();
    refresh();
}

// 徽章是「近 60 秒均值」，不是最新一个读数：两者只在曲线平直时相等，早先
// 拿最新样本冒充均值，2 秒跳一次的数字一抖徽章就跟着抖。均值取 Go 侧
// LatencyMonitor.Stats()——本层不做延迟计算（见 latencypanel.h 的说明）。
void LatencyPanel::refreshChartBadge()
{
    if (chartBadge_ == nullptr) {
        return;
    }
    const double avg = DeviceManager::instance().activeLatencyAvgMs();
    chartBadge_->setText(avg >= 0.0 ? tr("均值 %1ms").arg(avg, 0, 'f', 1) : tr("均值 —"));
}

} // namespace fg
