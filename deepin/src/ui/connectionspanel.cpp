#include "connectionspanel.h"
#include "core/designtokens.h"
#include "core/devicemanager.h"
#include "core/theme.h"
#include "icons.h"
#include "clipboardpanel.h"
#include "filespanel.h"
#include "widgets.h"

#include <QApplication>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QVBoxLayout>

#include <functional>

Q_LOGGING_CATEGORY(lcConnections, "flowgrid.connections")

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

// 与文件传输屏同一套单位，换算规则不重复写。
QString humanSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
    }
    if (bytes < 1024LL * 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

QString agoText(qint64 secs)
{
    if (secs < 60) {
        return QStringLiteral("%1s").arg(secs);
    }
    if (secs < 3600) {
        return QStringLiteral("%1m").arg(secs / 60);
    }
    return QStringLiteral("%1h").arg(secs / 3600);
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
    contentLayout_->addWidget(buildFileSection());
    contentLayout_->addWidget(buildLayoutSection());
    setFooterWidget(buildFooter());
    rebuildFileRows();

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
    // 设计稿 .section__body 里的内容左右各留 12px。这块的 vl 是 0 左右边距的
    // （下面的 makeSectionFooter 自带 12px，两处一叠就成了 24px），所以正文得
    // 自己套一层带边距的壳，否则这句话会直接贴着卡片描边，比同屏其它折叠区
    // 的内缩明显窄一截。
    auto *noteRow = new QWidget(body);
    auto *noteHl = new QHBoxLayout(noteRow);
    noteHl->setContentsMargins(tokens::kS3, 0, tokens::kS3, 0);
    noteHl->setSpacing(0);
    noteHl->addWidget(note);
    vl->addWidget(noteRow);

    vl->addWidget(makeSectionFooter(QString(), tr("打开剪贴板"), body,
                                    [this](FgButton *) { Q_EMIT clipboardRequested(); }));

    clipboardBadge_ = new Badge(QStringLiteral("—"), Badge::Variant::Neutral);
    refreshClipboardBadge();
    return new Collapsible(tr("剪贴板同步"), QStringLiteral("clipboard"), body,
                           clipboardBadge_);
}

// ── 文件传输折叠区 ────────────────────────────────────────────────────
//
// 传输本身归 FilesPanel 所有（FileSender / FileReceiver 只有一个活动事务，
// 两个面板各开一份会话只会互相抢）。这里只读它导出的快照，源一变就重建。
QWidget *ConnectionsPanel::buildFileSection()
{
    fileBody_ = new QWidget(this);
    fileRows_ = new QVBoxLayout(fileBody_);
    fileRows_->setContentsMargins(0, 0, 0, 0);
    fileRows_->setSpacing(0);

    fileRows_->addWidget(
        makeSectionFooter(tr("单文件上限 16 MiB · SHA-256 校验后交付"), tr("详情"), fileBody_,
                          [this](FgButton *) { Q_EMIT filesRequested(); }));

    fileBadge_ = new Badge(tr("空闲"), Badge::Variant::Neutral);
    return new Collapsible(tr("文件传输"), QStringLiteral("file"), fileBody_, fileBadge_);
}

void ConnectionsPanel::rebuildFileRows()
{
    if (fileRows_ == nullptr) {
        return;
    }
    clearLayout(fileRows_);
    const Palette &p = Theme::instance().palette();

    QList<XferSummary> list;
    if (files_ != nullptr) {
        list = files_->transferSummary();
    }

    // 徽章讲的是进行中的路数：设计稿给的是 ok 档的「1 项进行中」。
    int active = 0;
    for (const XferSummary &s : list) {
        if (s.state == XferSummary::State::Active) {
            ++active;
        }
    }
    if (fileBadge_ != nullptr) {
        fileBadge_->setText(active > 0 ? tr("%1 项进行中").arg(active) : tr("空闲"));
        fileBadge_->setVariant(active > 0 ? Badge::Variant::Ok : Badge::Variant::Neutral);
    }

    // 设计稿 .xfer 列表只给两条：一条进行中、一条已完成。多余的终态条目让
    // 这一屏去文件传输浮板看，这里只负责「现在有没有东西在跑」。
    QList<XferSummary> shown;
    for (const XferSummary &s : list) {
        if (s.state == XferSummary::State::Active) {
            shown.append(s);
        }
    }
    for (const XferSummary &s : list) {
        if (s.state != XferSummary::State::Active && shown.size() < 2) {
            shown.append(s);
        }
    }

    if (shown.isEmpty()) {
        fileRows_->addWidget(new EmptyState(QStringLiteral("file"), tr("暂无传输"),
                                            QString(), fileBody_));
        return;
    }

    for (const XferSummary &e : shown) {
        const bool activeRow = e.state == XferSummary::State::Active;
        auto *row = new QWidget(fileBody_);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(tokens::kS3, 9, tokens::kS3, 9);
        hl->setSpacing(tokens::kS3);

        // ui.css .xfer__icon：28×28 / r-ctl / 按状态取 soft 底 + text 字色。
        auto *iconBox = new QLabel(row);
        iconBox->setFixedSize(28, 28);
        iconBox->setAlignment(Qt::AlignCenter);
        const StatusColor tone =
            activeRow ? StatusColor{p.surface3, p.surface3, p.text2}
                      : (e.state == XferSummary::State::Done ? p.ok : p.danger);
        iconBox->setPixmap(
            icons::icon(activeRow ? (e.inbound ? QStringLiteral("download")
                                               : QStringLiteral("upload"))
                                  : (e.state == XferSummary::State::Done
                                         ? QStringLiteral("check")
                                         : QStringLiteral("warn")),
                      14, tone.text)
                .pixmap(14, 14));
        applyBoxStyle(iconBox,
                      QStringLiteral("border-radius:%1px;background:%2")
                          .arg(QString::number(tokens::kRadiusCtl), css(tone.soft)));
        hl->addWidget(iconBox, 0, Qt::AlignTop);

        auto *body = new QWidget(row);
        auto *vl = new QVBoxLayout(body);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(4);

        auto *top = new QHBoxLayout;
        top->setSpacing(tokens::kS2);
        auto *name = new ElidedLabel(e.name, body);
        name->setFont(uiFont(tokens::kFont12, QFont::Medium));
        name->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));
        top->addWidget(name, 1);
        auto *pct = new QLabel(activeRow ? tr("%1%").arg(e.pct) : tr("完成"), body);
        // .xfer__pct 指定 font-mono + tabular-nums：进度每 500ms 跳一次。
        pct->setFont(uiMonoFont(tokens::kFont11, QFont::DemiBold));
        pct->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text2)));
        pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pct->setFixedWidth(34);
        top->addWidget(pct, 0, Qt::AlignTop);
        vl->addLayout(top);

        if (activeRow) {
            auto *bar = new ProgressTrack(body);
            bar->setProgress(e.pct);
            vl->addWidget(bar);
        }

        QString meta = activeRow
                           ? tr("%1 · → %2").arg(humanSize(e.totalBytes), e.peer)
                           : tr("%1 · %2 · %3")
                                 .arg(humanSize(e.totalBytes),
                                      e.inbound ? tr("收到") : tr("已发送"),
                                      tr("%1 前").arg(agoText(e.agoSecs)));
        auto *metaLabel = new ElidedLabel(meta, body);
        metaLabel->setFont(uiFont(tokens::kFont11));
        metaLabel->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        vl->addWidget(metaLabel);

        hl->addWidget(body, 1);
        applyBoxStyle(row, QStringLiteral("border-bottom:1px solid %1").arg(css(p.hairline)));
        fileRows_->addWidget(row);
    }
}

// ── 设备布局折叠区 ────────────────────────────────────────────────────
//
// 设计稿的格子可拖动改屏幕相对位置。那需要一份可持久化的屏幕布局，目前没有
// 存储也没有对应的协议帧；这里按设计把 3×3 网格与图例摆出来，点格子记一条
// 日志说明尚未接通，而不是做成拖得动、退出就没了的假交互。
void ConnectionsPanel::attachPanels(ClipboardPanel *clip, FilesPanel *files)
{
    // 两个折叠区的数据源。传输与剪贴板都只归各自浮板所有，这里只订阅变化
    // 重建本屏，绝不另开一份会话或另存一份计数——两处各记一份必然会漂。
    clipboard_ = clip;
    files_ = files;
    if (clipboard_ != nullptr) {
        connect(clipboard_, &ClipboardPanel::transfersChanged, this,
                &ConnectionsPanel::refreshClipboardBadge);
    }
    if (files_ != nullptr) {
        connect(files_, &FilesPanel::transfersChanged, this,
                &ConnectionsPanel::rebuildFileRows);
    }
    refreshClipboardBadge();
    rebuildFileRows();
}

void ConnectionsPanel::refreshClipboardBadge()
{
    if (clipboardBadge_ == nullptr) {
        return;
    }
    const int n = clipboard_ != nullptr ? clipboard_->historyCount() : 0;
    clipboardBadge_->setText(n > 0 ? tr("%1 项").arg(n) : tr("空闲"));
    clipboardBadge_->setVariant(n > 0 ? Badge::Variant::Neutral : Badge::Variant::Idle);
}

QWidget *ConnectionsPanel::buildLayoutSection()
{
    const Palette &p = Theme::instance().palette();
    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    vl->setSpacing(tokens::kS3);

    // ui.css .layout：2px 虚线 line-2 / r-card / surface-2 底 / 3×3 / gap 8 /
    // min-height 200。格子用 QGridLayout，三个空槽也要占位，网格才不会塌成两列。
    auto *gridBox = new QWidget(body);
    applyBoxStyle(gridBox,
                  QStringLiteral("border:2px dashed %1;border-radius:%2px;background:%3")
                      .arg(css(p.line2))
                      .arg(QString::number(tokens::kRadiusCard))
                      .arg(css(p.surface2)));
    auto *grid = new QGridLayout(gridBox);
    grid->setContentsMargins(tokens::kS4, tokens::kS4, tokens::kS4, tokens::kS4);
    grid->setHorizontalSpacing(tokens::kS2);
    grid->setVerticalSpacing(tokens::kS2);

    // 只有在线设备占格：离线设备没有屏幕可摆。排到前九个槽的是在线的那些，
    // 剩下的槽留给空位，设计稿里的「+」就是等新设备上线后落进去的。
    QList<Device> placeable;
    for (const Device &d : DeviceManager::instance().devices()) {
        if (d.online) {
            placeable.append(d);
        }
    }
    for (int i = 0; i < 9; ++i) {
        if (i < placeable.size()) {
            const Device &d = placeable.at(i);
            // 设计稿三个格子分别是 accent / ok-solid / warn-solid；这里按设备
            // 当前的延迟档取实色，规则一致且随实时读数变化。
            const StatusColor tone =
                Theme::instance().color(Theme::instance().qualityOf(d.latencyMs, true));
            auto *cell = new QWidget(gridBox);
            auto *cl = new QVBoxLayout(cell);
            cl->setContentsMargins(0, 0, 0, 0);
            cl->setSpacing(tokens::kS1);
            cl->setAlignment(Qt::AlignCenter);
            auto *iconBox = new QLabel(cell);
            iconBox->setFixedSize(56, 56);
            iconBox->setAlignment(Qt::AlignCenter);
            iconBox->setText(d.abbrev.isEmpty() ? d.name.left(2).toUpper() : d.abbrev);
            iconBox->setFont(uiFont(tokens::kFont15, QFont::Bold));
            // 设计稿 .cell__icon 就是 color: #fff（ui.css 第 438 行），压在
            // 实色块上；与托盘 .switch-btn__icon 同一处理，不走 onAccent——
            // 那一档是配主色用的，状态色块套上去会串色。
            iconBox->setStyleSheet(
                QStringLiteral("background:%1;border-radius:%2px;color:%3")
                    .arg(css(tone.solid))
                    .arg(QString::number(tokens::kRadiusCard))
                    .arg(QStringLiteral("#ffffff")));
            cl->addWidget(iconBox);
            auto *label = new QLabel(d.name.section(QLatin1Char(' '), 0, 0), cell);
            label->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
            label->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));
            label->setAlignment(Qt::AlignCenter);
            cl->addWidget(label);
            if (d.isActive) {
                auto *hint = new QLabel(tr("当前"), cell);
                hint->setFont(uiFont(tokens::kFont11, QFont::Medium));
                hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.ok.text)));
                hint->setAlignment(Qt::AlignCenter);
                cl->addWidget(hint);
            }
            cell->setCursor(Qt::PointingHandCursor);
            QObject::connect(cell, &QWidget::customContextMenuRequested, this, [d] {
                qCInfo(lcConnections)
                    << "调整设备布局：" << d.name << QStringLiteral("屏幕布局尚无持久化，暂不可拖动");
            });
            grid->addWidget(cell, i / 3, i % 3);
            continue;
        }
        // .cell__empty：surface-3 底 + 2px 虚线 line-3 + 加号
        auto *empty = new QLabel(QStringLiteral("+"), gridBox);
        empty->setFixedSize(56, 56);
        empty->setAlignment(Qt::AlignCenter);
        empty->setFont(uiFont(tokens::kFont13, QFont::Bold));
        empty->setStyleSheet(
            QStringLiteral("background:%1;color:%2;border:2px dashed %3;border-radius:%4px")
                .arg(css(p.surface3), css(p.text3), css(p.line3))
                .arg(QString::number(tokens::kRadiusCard)));
        grid->addWidget(empty, i / 3, i % 3, Qt::AlignCenter);
    }
    vl->addWidget(gridBox);

    // .legend：三条说明，每条一个图标
    auto *legend = new QWidget(body);
    auto *lh = new QHBoxLayout(legend);
    lh->setContentsMargins(0, 0, 0, 0);
    lh->setSpacing(tokens::kS3);
    struct LegendItem {
        const char *icon;   // 空串表示这一条用圆点，不用图标
        QString text;
        QColor color;
    };
    const LegendItem items[] = {
        {"grip", tr("拖动调整屏幕相对位置"), p.text3},
        {"", tr("当前控制端"), p.ok.text},
        {"info", tr("边缘触发切换"), p.text3},
    };
    for (const LegendItem &it : items) {
        if (*it.icon == '\0') {
            auto *dot = new StatusDot(legend);
            dot->setFixedSize(tokens::kStatusDotSize, tokens::kStatusDotSize);
            dot->setQuality(Quality::Good);
            lh->addWidget(dot, 0, Qt::AlignVCenter);
        } else {
            auto *icon = new QLabel(legend);
            icon->setFixedSize(12, 12);
            icon->setPixmap(
                icons::icon(QString::fromLatin1(it.icon), 12, it.color).pixmap(12, 12));
            lh->addWidget(icon);
        }
        auto *label = new QLabel(it.text, legend);
        label->setFont(uiFont(tokens::kFont11));
        label->setStyleSheet(QStringLiteral("color:%1").arg(css(it.color)));
        lh->addWidget(label);
    }
    lh->addStretch(1);
    vl->addWidget(legend);

    auto *badge = new Badge(tr("拖动排布"), Badge::Variant::Neutral);
    return new Collapsible(tr("设备布局"), QStringLiteral("grid"), body, badge);
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
