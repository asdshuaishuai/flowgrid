#include "latencypanel.h"
#include "core/designtokens.h"
#include "core/devicemanager.h"
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
        auto *wifi = new QLabel(linkRow);
        wifi->setFixedSize(12, 12);
        wifi->setPixmap(icons::icon(QStringLiteral("wifi"), 12,
                                    Theme::instance().palette().text3).pixmap(12, 12));
        lrl->addStretch(1);
        lrl->addWidget(wifi);
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

        chartBadge_ = new Badge(tr("均值 --"), Badge::Variant::Ok);
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
                          .arg(css(p.surface)));
        contentLayout_->addWidget(card);
    }

    contentLayout_->addWidget(buildThresholdSection());
    setFooterWidget(buildFooter());

    auto &manager = DeviceManager::instance();
    connect(&manager, &DeviceManager::devicesChanged, this, &LatencyPanel::refresh);
    connect(&manager, &DeviceManager::sampled, this, &LatencyPanel::onSampled);

    refresh();
    // 首屏在第一次真实采样到达前没有历史；prime() 按 mock 源的抖动带铺一组
    // 占位样本，全推同一个 seed 会让 12 根柱子齐平，既不像设计稿的 22~44px
    // 起伏，也看不出「采样在动」。首次 sampled() 到达后即被真实数据顶掉。
    chart_->prime(manager.activeLatencyMs());
    // 徽章初值跟着 prime 的种子走：否则首屏要等第一个 2 秒采样才变，
    // 展开图表区先看到的是一个「均值 --」的空徽章。
    if (chartBadge_ != nullptr && manager.activeLatencyMs() >= 0.0) {
        chartBadge_->setText(tr("均值 %1ms").arg(manager.activeLatencyMs(), 0, 'f', 1));
    }
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

    auto *threshold = new QLineEdit(QStringLiteral("20"), body);
    threshold->setFixedWidth(64);
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

    auto *badge = new Badge(QStringLiteral("20ms"), Badge::Variant::Neutral);
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
    icon->setFixedSize(14, 14);
    icon->setPixmap(icons::icon(QStringLiteral("info"), 14, p.text3).pixmap(14, 14));
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
    const bool online = active != nullptr && active->online;
    const double ms = manager.activeLatencyMs();
    const Quality q = Theme::qualityOf(ms, online);

    if (bigValue_ != nullptr) {
        bigValue_->setText(online ? QStringLiteral("%1ms").arg(ms, 0, 'f', 1)
                                  : QStringLiteral("--"));
        bigValue_->setStyleSheet(
            QStringLiteral("color:%1").arg(css(Theme::instance().color(q).text)));
    }
    if (linkLabel_ != nullptr) {
        linkLabel_->setText(active != nullptr ? linkName(active->link) : QStringLiteral("—"));
    }
    if (titleDot_ != nullptr) {
        titleDot_->setQuality(q);
        titleDot_->setPulsing(online);
    }
    if (titleLatency_ != nullptr) {
        titleLatency_->setLatency(ms, online);
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
            QString hint = tr("离线");
            if (d.online) {
                hint = (d.isLocal ? tr("本机") : d.platformLabel) +
                       QStringLiteral(" · ") + linkName(d.link) +
                       tr(" · 丢包 %1%").arg(d.packetLoss, 0, 'f', 1);
            }
            auto *lat = new LatencyChip();
            lat->setLatency(d.latencyMs, d.online);

            // 设计稿每行名字前面带设备类型图标（笔记本 / 台式机 / 平板）。
            auto *title = new QWidget(this);
            auto *tl = new QHBoxLayout(title);
            tl->setContentsMargins(0, 0, 0, 0);
            tl->setSpacing(tokens::kS2);
            auto *icon = new QLabel(title);
            icon->setFixedSize(14, 14);
            icon->setPixmap(icons::icon(d.icon, 14, d.online ? p.text2 : p.text3).pixmap(14, 14));
            tl->addWidget(icon);
            auto *name = new QLabel(d.name, title);
            name->setFont(uiFont(tokens::kFont12, QFont::DemiBold));
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
    if (chartBadge_ != nullptr) {
        chartBadge_->setText(tr("均值 %1ms").arg(ms, 0, 'f', 1));
    }
    refresh();
}

} // namespace fg
