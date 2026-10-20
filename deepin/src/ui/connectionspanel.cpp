#include "connectionspanel.h"
#include "core/designtokens.h"
#include "core/devicemanager.h"
#include "core/theme.h"
#include "core/settings.h"
#include "icons.h"
#include "clipboardpanel.h"
#include "filespanel.h"
#include "keymappanel.h"
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

// 一行页脚：左侧说明 + 右侧若干按钮。设计稿里 .section__body 的页脚都是这个
// 形状，只是按钮个数从 1 个到 2 个不等，早先是按个数各写一遍。
QWidget *makeSectionFooterRow(QWidget *hint, const QList<QWidget *> &buttons, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(tokens::kS3, 0, tokens::kS3, tokens::kS3);
    hl->setSpacing(tokens::kS2);
    if (hint != nullptr) {
        hl->addWidget(hint);
    }
    hl->addStretch(1);
    for (QWidget *b : buttons) {
        hl->addWidget(b);
    }
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
        pill->setFixedHeight(tokens::kStatusPillHeight);  // 圆角取高的一半
        auto *pl = new QHBoxLayout(pill);
        pl->setContentsMargins(8, 0, 8, 0);
        pl->setSpacing(6);
        statusDot_ = new StatusDot(pill);
        statusLabel_ = new QLabel(tr("已连接"), pill);
        statusLabel_->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
        pl->addWidget(statusDot_);
        pl->addWidget(statusLabel_);
        pill->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px")
                                .arg(css(Theme::instance().palette().ok.soft),
                                     QString::number(tokens::pillRadius(tokens::kStatusPillHeight))));
        hl->addWidget(pill);
        hl->addStretch(1);

        auto *addBtn = new FgButton(tr("添加设备"), FgButton::Variant::Accent, false, row);
        connect(addBtn, &FgButton::clicked, this, [this] { Q_EMIT addRequested(); });
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
    // 图表**不从假数据起手**。早先这里调 prime()，用当前读数加一段随机抖动
    // 铺满 12 根柱子，好让折叠区一展开不是空白——可那 12 根柱子是从没量过
    // 的数，整片图表都是假的。现在它从空开始，随真实采样一根根长出来
    // （每 2 秒一根，24 秒填满）；空的时候画布上写「等待采样」。
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
    auto *grid = new QWidget(body);
    keymapGrid_ = new QGridLayout(grid);
    keymapGrid_->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    keymapGrid_->setHorizontalSpacing(tokens::kS2);
    keymapGrid_->setVerticalSpacing(tokens::kS2);
    vl->addWidget(grid);

    // 设计稿：映射网格下面还有一行「按应用切换映射，游戏内自动停用」+「编辑」。
    vl->addWidget(makeSectionFooter(tr("按应用切换映射，游戏内自动停用"), tr("编辑"), body,
                                    [this](FgButton *) { Q_EMIT keymapRequested(); }));

    keymapBadge_ = new Badge(QStringLiteral("—"), Badge::Variant::Neutral);
    rebuildKeymapGrid();   // 挂上规则表前是空的，attachKeymap 会再补一次
    return new Collapsible(tr("键位映射"), QStringLiteral("keyboard"), body, keymapBadge_);
}

// 摘要区只列前三条生效规则（设计稿这一区就是 3 列），顺序与键位浮板一致——两边
// 读同一张表，不再各摆一份示例：早先这里写死「3 条生效」+ 三条示例，而键位浮板
// 里生效的是 5 条，同一个应用里两块屏各说各话。
void ConnectionsPanel::rebuildKeymapGrid()
{
    if (keymapGrid_ == nullptr) {
        return;
    }
    clearLayout(keymapGrid_);
    if (keymap_ == nullptr) {
        return;
    }

    const Palette &p = Theme::instance().palette();
    QWidget *host = keymapGrid_->parentWidget();
    int shown = 0;
    for (const KeyRule &rule : keymap_->rules()) {
        if (shown >= 3) {
            break;
        }
        if (!isEffectiveKeyRule(rule)) {
            continue;
        }
        auto *cell = new QWidget(host);
        auto *cl = new QVBoxLayout(cell);
        cl->setContentsMargins(tokens::kS2, tokens::kS2, tokens::kS2, tokens::kS2);
        cl->setSpacing(5);   // ui.css .keypair gap

        auto *keys = new QWidget(cell);
        auto *kl = new QHBoxLayout(keys);
        kl->setContentsMargins(0, 0, 0, 0);
        kl->setSpacing(5);   // ui.css .keys gap
        kl->addWidget(new FgKeycap(rule.from, false, keys));
        auto *arrow = new QLabel(keys);
        arrow->setFixedSize(tokens::kIconXs, tokens::kIconXs);
        arrow->setPixmap(icons::icon(QStringLiteral("arrowRight"), tokens::kIconXs, p.text3).pixmap(tokens::kIconXs, tokens::kIconXs));
        kl->addWidget(arrow, 0, Qt::AlignVCenter);
        kl->addWidget(new FgKeycap(rule.to, false, keys));
        kl->addStretch(1);
        cl->addWidget(keys);

        auto *ctx = new QLabel(rule.context, cell);
        ctx->setFont(uiFont(tokens::kFont11));
        ctx->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        cl->addWidget(ctx);

        keymapGrid_->addWidget(cell, 0, shown);
        ++shown;
    }

    if (keymapBadge_ != nullptr) {
        keymapBadge_->setText(tr("%1 条生效").arg(keymap_->effectiveRuleCount()));
    }
}

void ConnectionsPanel::attachKeymap(KeymapPanel *keymap)
{
    keymap_ = keymap;
    if (keymap_ != nullptr) {
        // 规则表将来由 YAML 加载：换了表这块摘要要跟着变，不能停在首次渲染。
        connect(keymap_, &KeymapPanel::rulesChanged, this, &ConnectionsPanel::rebuildKeymapGrid);
    }
    rebuildKeymapGrid();
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

    // 设计稿这一区的页脚是「跨端同步已加密，历史保留 N 条」+ 详情 + 清空。
    // 保留条数跟着设置走（sync/clipboard_history），别写死 50。
    clipboardFootHint_ = new QLabel(body);
    clipboardFootHint_->setFont(uiFont(tokens::kFont11));
    clipboardFootHint_->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    auto *open = new FgButton(tr("打开剪贴板"), FgButton::Variant::Secondary, true, body);
    connect(open, &FgButton::clicked, this, [this] { Q_EMIT clipboardRequested(); });
    auto *clear = new FgButton(tr("清空"), FgButton::Variant::Ghost, true, body);
    connect(clear, &FgButton::clicked, this, [this] {
        // 历史归剪贴板浮板所有，这里只调它的公开入口，不另存一份计数。
        if (clipboard_ != nullptr) {
            clipboard_->clearHistory();
            toast(tr("已清空同步历史"), ToastKind::Ok);
        } else {
            qCInfo(lcConnections) << "剪贴板浮板未挂上，清空按钮暂时无处可去";
        }
    });
    vl->addWidget(makeSectionFooterRow(clipboardFootHint_, {open, clear}, body));

    clipboardBadge_ = new Badge(QStringLiteral("—"), Badge::Variant::Neutral);
    refreshClipboardBadge();
    refreshClipboardFootHint();
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
    // 终态按「最新在前」挑：transferSummary() 给的是追加序（由旧到新），
    // 跟文件浮板「最新的在最上面」相反，两屏的行序就会自相矛盾
    // （41 分钟前 排在 3 分钟前 上面）。倒着取。
    for (int i = list.size() - 1; i >= 0; --i) {
        const XferSummary &s = list.at(i);
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
        iconBox->setFixedSize(tokens::kXferIconSize, tokens::kXferIconSize);
        iconBox->setAlignment(Qt::AlignCenter);
        // 与文件浮板同一套三档配色：拒绝是对端的决定（warn）、校验失败是数据
        // 损坏（danger）、超限是根本没上线。全涂成红色只会让人以为是同一种问题。
        const StatusColor tone =
            activeRow ? StatusColor{p.surface3, p.surface3, p.text2}
                      : (e.state == XferSummary::State::Done       ? p.ok
                         : e.state == XferSummary::State::Rejected ? p.warn
                                                                   : p.danger);
        iconBox->setPixmap(
            icons::icon(activeRow ? (e.inbound ? QStringLiteral("download")
                                               : QStringLiteral("upload"))
                                  : (e.state == XferSummary::State::Done
                                         ? QStringLiteral("check")
                                     : e.state == XferSummary::State::Rejected
                                         ? QStringLiteral("close")
                                         : QStringLiteral("warn")),
                      14, tone.text)
                .pixmap(tokens::kIconSm, tokens::kIconSm));
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
        // 终态不能一律写「完成」：这一支里还坐着「已拒绝 / 校验失败 / 超限」，
        // 配着红色图标却写「完成」，等于把失败说成成功。文案与文件浮板的徽章对齐。
        const QString stateLabel =
            e.state == XferSummary::State::Done       ? tr("完成")
            : e.state == XferSummary::State::Rejected ? tr("已拒绝")
            : e.state == XferSummary::State::Oversize ? tr("超限")
                                                      : tr("失败");
        auto *pct = new QLabel(activeRow ? tr("%1%").arg(e.pct) : stateLabel, body);
        // .xfer__pct 指定 font-mono + tabular-nums：进度每 500ms 跳一次。
        pct->setFont(uiMonoFont(tokens::kFont11, QFont::DemiBold));
        pct->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text2)));
        pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pct->setFixedWidth(tokens::kXferPctWidth);
        top->addWidget(pct, 0, Qt::AlignTop);
        vl->addLayout(top);

        if (activeRow) {
            auto *bar = new ProgressTrack(body);
            bar->setProgress(e.pct);
            vl->addWidget(bar);
        }

        QString meta;
        if (activeRow) {
            meta = tr("%1 · → %2").arg(humanSize(e.totalBytes), e.peer);
        } else if (e.state == XferSummary::State::Oversize) {
            // 超限的文件压根没上线，写「已发送」是替它喊冤（同文件浮板的措辞）。
            meta = tr("%1 · 超出单文件上限，未在链路上传输").arg(humanSize(e.totalBytes));
        } else {
            // 设计稿这一行是「2.1 MB · 收到 · Linux Workstation · 3m」：对端名
            // 不能省，否则「收到 / 已发送」是在跟谁传全靠猜。
            meta = tr("%1 · %2 · %3 · %4")
                       .arg(humanSize(e.totalBytes), e.inbound ? tr("收到") : tr("已发送"),
                            e.peer, tr("%1 前").arg(agoText(e.agoSecs)));
        }
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

void ConnectionsPanel::rebuildLayoutGrid()
{
    if (layoutGrid_ == nullptr) {
        return;
    }
    clearLayout(layoutGrid_);
    // 格子随设备状态重建，不能只在构造时填一次：设备上下线、切换控制端
    // 都会改「当前」标记和实色档位，网格留在旧状态就和同屏的设备卡自相矛盾。
    const Palette &p = Theme::instance().palette();
    QWidget *host = layoutGrid_->parentWidget();
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
            auto *cell = new QWidget(host);
            auto *cl = new QVBoxLayout(cell);
            cl->setContentsMargins(0, 0, 0, 0);
            cl->setSpacing(tokens::kS1);
            cl->setAlignment(Qt::AlignCenter);
            auto *iconBox = new QLabel(cell);
            iconBox->setFixedSize(tokens::kCellIconSize, tokens::kCellIconSize);
            iconBox->setAlignment(Qt::AlignCenter);
            iconBox->setText(d.abbrev.isEmpty() ? d.name.left(2).toUpper() : d.abbrev);
            iconBox->setFont(uiFont(tokens::kFont15, QFont::Bold));
            // 42px 的色块有足够面积，柔底 + 同族字色读得清档位。原先用实色
            // 块压白字，一屏三个饱和色块（绿/绿/橙）比整块面板都跳，deepin
            // 那种克制的调子里完全站不住。状态语义不变：底色仍是这个设备的
            // 延迟档（绿=好、橙=慢），只是把实心换成同族的淡底。
            iconBox->setStyleSheet(
                QStringLiteral("background:%1;border-radius:%2px;color:%3")
                    .arg(css(tone.soft))
                    .arg(QString::number(tokens::kRadiusCard))
                    .arg(css(tone.text)));
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
            // 悬停就能看到「还没接通」，不用非点一下才知道。
            cell->setToolTip(tr("屏幕布局尚无持久化，暂不可拖动"));
            // customContextMenuRequested 只在显式开了 CustomContextMenu 策略时才
            // 发出。此前漏了这一句，下面这个 lambda 一次也不会跑——所谓「点格子
            // 记一条日志说明尚未接通」，其实从来没有日志。
            cell->setContextMenuPolicy(Qt::CustomContextMenu);
            QObject::connect(cell, &QWidget::customContextMenuRequested, this, [d] {
                qCInfo(lcConnections)
                    << "调整设备布局：" << d.name << QStringLiteral("屏幕布局尚无持久化，暂不可拖动");
            });
            layoutGrid_->addWidget(cell, i / 3, i % 3);
            continue;
        }
        // .cell__empty：surface-3 底 + 2px 虚线 line-3 + 加号
        auto *empty = new QLabel(QStringLiteral("+"), host);
        empty->setFixedSize(tokens::kCellIconSize, tokens::kCellIconSize);
        empty->setAlignment(Qt::AlignCenter);
        empty->setFont(uiFont(tokens::kFont13, QFont::Bold));
        empty->setStyleSheet(
            QStringLiteral("background:%1;color:%2;border:2px dashed %3;border-radius:%4px")
                .arg(css(p.surface3), css(p.text3), css(p.line3))
                .arg(QString::number(tokens::kRadiusCard)));
        // 那个「+」看着像个能点的按钮，说清楚它是空位。
        empty->setToolTip(tr("空位：设备上线后落在这里（布局拖动与持久化尚未接通）"));
        layoutGrid_->addWidget(empty, i / 3, i % 3, Qt::AlignCenter);
    }
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
                      .arg(css(p.well)));
    layoutGrid_ = new QGridLayout(gridBox);
    layoutGrid_->setContentsMargins(tokens::kS4, tokens::kS4, tokens::kS4, tokens::kS4);
    layoutGrid_->setHorizontalSpacing(tokens::kS2);
    layoutGrid_->setVerticalSpacing(tokens::kS2);

    vl->addWidget(gridBox);
    rebuildLayoutGrid();

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
            icon->setFixedSize(tokens::kIconXs, tokens::kIconXs);
            icon->setPixmap(
                icons::icon(QString::fromLatin1(it.icon), tokens::kIconXs, it.color).pixmap(tokens::kIconXs, tokens::kIconXs));
            lh->addWidget(icon);
        }
        auto *label = new QLabel(it.text, legend);
        label->setFont(uiFont(tokens::kFont11));
        label->setStyleSheet(QStringLiteral("color:%1").arg(css(it.color)));
        lh->addWidget(label);
    }
    lh->addStretch(1);
    vl->addWidget(legend);

    // 这一整块是**示意**：拖动与持久化都还没接通，界面上必须写明白。
    //
    // 空槽画的是「+」，看上去就是个「添加设备」的按钮；在线设备那几格又能
    // 悬停出「暂不可拖动」的提示。唯独空槽什么提示都没有——用户点下去什么
    // 也不会发生，却以为这是能用的布局编辑器。按「未接通的功能要在界面上说
    // 清楚」的要求，这里补一句。
    auto *note = new QLabel(tr("布局拖动与持久化尚未接通，当前仅作示意"), body);
    note->setFont(uiFont(tokens::kFont11));
    note->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    vl->addWidget(note);

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
    icon->setFixedSize(tokens::kIconSm, tokens::kIconSm);
    icon->setPixmap(icons::icon(QStringLiteral("shield"), tokens::kIconSm, p.text3).pixmap(tokens::kIconSm, tokens::kIconSm));
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
                [](const QString &id, const QString &kind) {
                    // 这张卡右侧的按钮：离线设备是「重连」，当前设备是「更多信息」，
                    // 两者都要等传输层。以前这里挂的是个空 lambda ——点下去既没有
                    // 动作也没有日志，用户只能猜。按项目惯例如实记一条。
                    qCInfo(lcConnections)
                        << "设备动作" << kind << id << QStringLiteral("尚未接通");
                });
        deviceListLayout_->addWidget(card);
    }
}

void ConnectionsPanel::refresh()
{
    auto &manager = DeviceManager::instance();
    const Device *active = manager.activeDevice();
    const bool online = active != nullptr && active->online;
    // 对端数：能连上的、且不是本机的设备。传输层没落地时这个数是 0。
    int peers = 0;
    for (const Device &d : manager.devices()) {
        if (d.online && !d.isLocal) {
            ++peers;
        }
    }

    if (statusLabel_ != nullptr) {
        // 这一句必须说真话。早先写的是「已连接 <设备名>」——设备名是设计稿里
        // 那台 fake 的「deepin 工作站」，于是这一屏顶上一上来就宣称连着一台
        // 不存在的机器。现在：本机在线但没有对端，就写「本机在线 · 无对端」。
        if (peers > 0) {
            statusLabel_->setText(tr("已连接 %1").arg(active != nullptr ? active->name : ""));
        } else if (online) {
            statusLabel_->setText(tr("本机在线 · 无对端"));
        } else {
            statusLabel_->setText(tr("未联网"));
        }
        const StatusColor sc = Theme::instance().color(
            Theme::qualityOf(manager.activeLatencyMs(), online));
        statusLabel_->setStyleSheet(QStringLiteral("color:%1").arg(css(sc.text)));
        statusDot_->setQuality(Theme::qualityOf(manager.activeLatencyMs(), online));
        statusDot_->setPulsing(online);
    }

    rebuildDeviceCards();
    rebuildLayoutGrid();
    updateLatencyBadge();
    refreshClipboardFootHint();
}

void ConnectionsPanel::refreshClipboardFootHint()
{
    if (clipboardFootHint_ == nullptr) {
        return;
    }
    const int keep = Settings::instance().clipboardHistory();
    clipboardFootHint_->setText(tr("跨端同步已加密，历史保留 %1 条").arg(keep));
}

void ConnectionsPanel::updateLatencyBadge()
{
    if (latencyBadge_ == nullptr) {
        return;
    }
    // 与延迟浮板取同一个数（设计稿两屏都写 2.3ms）：本区画的是**当前控制端**的
    // 采样曲线。早先这里按「所有在线设备求瞬时值平均」，会把一条 12ms 的局域网
    // 设备和 2ms 的直连设备平均成 6ms——一个不属于任何链路的数，还跟同屏顶部
    // 胶囊上的读数对不上。均值来自 Go 的 LatencyMonitor，本层略过采样。
    const double avg = DeviceManager::instance().activeLatencyAvgMs();
    latencyBadge_->setText(avg >= 0.0 ? tr("均值 %1ms").arg(avg, 0, 'f', 1) : tr("无数据"));
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
