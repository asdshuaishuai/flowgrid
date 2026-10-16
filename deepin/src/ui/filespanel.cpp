#include "filespanel.h"
#include "core/designtokens.h"
#include "core/settings.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcFiles, "flowgrid.files")

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

QString humanSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
    }
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

// 记录行右侧的「多久之前」，与设计稿的 3m / 12m 同一量级。
QString agoText(qint64 ms)
{
    const qint64 secs = ms / 1000;
    if (secs < 60) {
        return QObject::tr("%1s").arg(secs);
    }
    if (secs < 3600) {
        return QObject::tr("%1m").arg(secs / 60);
    }
    return QObject::tr("%1h").arg(secs / 3600);
}

// 28×28 圆角图标底座（ui.css .xfer__icon），按状态取 soft 底 + text 字色。
QLabel *xferIcon(const QString &name, const StatusColor &c, QWidget *parent)
{
    auto *box = new QLabel(parent);
    box->setFixedSize(28, 28);
    box->setAlignment(Qt::AlignCenter);
    box->setPixmap(icons::icon(name, 14, c.text).pixmap(14, 14));
    box->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px")
                           .arg(css(c.soft), QString::number(tokens::kRadiusCtl)));
    return box;
}

} // namespace

// ── DropZone ──────────────────────────────────────────────────────────
DropZone::DropZone(QWidget *parent) : QWidget(parent)
{
    // 固定 objectName：样式表必须写成 #dropZone 限定，否则裸声明会级联到
    // 三个子 QLabel 上，拖放区会变成「三条虚线框」。
    setObjectName(QStringLiteral("dropZone"));
    // 普通 QWidget 子类默认不让 QStyleSheetStyle 画自己的边框/底色，
    // 必须显式打开；否则样式表只会「级联」到子控件上——正是 v6 里拖放区
    // 画成三条虚线框、而容器本身没有边框的原因。
    setAttribute(Qt::WA_StyledBackground, true);
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
}

void DropZone::applyRestingStyle()
{
    applyBoxStyle(this,
                  QStringLiteral("border:2px dashed %1;border-radius:%2px;background:%3")
                      .arg(css(Theme::instance().palette().line2),
                           QString::number(tokens::kRadiusCard),
                           css(Theme::instance().palette().surface2)));
}

void DropZone::enterEvent(QEnterEvent *e)
{
    // 设计稿 .drop:hover：描边转主色、底转主色浅底。配色走 Theme，
    // 不在这里写死任何主题色。
    const Palette &p = Theme::instance().palette();
    applyBoxStyle(this,
                  QStringLiteral("border:2px dashed %1;border-radius:%2px;background:%3")
                      .arg(css(p.accent), QString::number(tokens::kRadiusCard),
                           css(p.accentSoft)));
    QWidget::enterEvent(e);
}

void DropZone::leaveEvent(QEvent *e)
{
    applyRestingStyle();
    QWidget::leaveEvent(e);
}

void DropZone::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        Q_EMIT clicked();
    }
    QWidget::mouseReleaseEvent(e);
}

void DropZone::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
        const Palette &p = Theme::instance().palette();
        applyBoxStyle(this,
                      QStringLiteral("border:2px dashed %1;border-radius:%2px;background:%3")
                          .arg(css(p.accent), QString::number(tokens::kRadiusCard),
                               css(p.accentSoft)));
    }
}

void DropZone::dragMoveEvent(QDragMoveEvent *e)
{
    e->acceptProposedAction();
}

void DropZone::dropEvent(QDropEvent *e)
{
    QStringList paths;
    const QList<QUrl> urls = e->mimeData()->urls();
    for (const QUrl &u : urls) {
        if (u.isLocalFile()) {
            paths << u.toLocalFile();
        }
    }
    if (!paths.isEmpty()) {
        Q_EMIT filesDropped(paths);
    }
    e->acceptProposedAction();
    // 复位静止态，否则拖拽结束后会一直停在主色样式上。
    applyRestingStyle();
}

// ── FilesPanel ────────────────────────────────────────────────────────
FilesPanel::FilesPanel(QWidget *parent) : Panel(tr("文件传输"), parent)
{
    auto *body = qobject_cast<QVBoxLayout *>(content()->layout());
    const auto &settings = Settings::instance();

    {
        auto *pill = new QWidget(this);
        auto *pl = new QHBoxLayout(pill);
        pl->setContentsMargins(8, 0, 8, 0);
        pl->setSpacing(6);
        auto *dot = new StatusDot(pill);
        dot->setPulsing(settings.fileTransfer());
        auto *label = new QLabel(settings.fileTransfer() ? tr("已启用") : tr("已关闭"), pill);
        label->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
        pl->addWidget(dot);
        pl->addWidget(label);
        pill->setStyleSheet(QStringLiteral("background:%1;border-radius:10px")
                                .arg(css(Theme::instance().palette().ok.soft)));
        setTitleBarWidget(pill);
    }

    body->addWidget(buildTransferList());
    body->addWidget(buildDropZone());
    body->addWidget(buildRecords());
    setFooterWidget(buildFooter());

    // 首帧就要画出空状态，否则只有两个小标题、下面什么都没有，看着像加载坏了。
    rebuildTransfers();
    rebuildRecords();

    // 会话轮询早先只有 pollSessions() 这个方法、没有任何调用点，等于整块列表
    // 是死的。这里按 500ms 拉一次：Go 侧是非阻塞出队，没数据时开销可忽略，
    // 而面板「按需唤出」的形态也要求唤出瞬间状态就是新的。
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &FilesPanel::pollSessions);
    timer->start(500);
}

QWidget *FilesPanel::buildTransferList()
{
    const Palette &p = Theme::instance().palette();
    auto *box = new QWidget(this);
    auto *vl = new QVBoxLayout(box);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(tokens::kS1);

    auto *title = new QLabel(tr("进行中"), box);
    title->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    title->setStyleSheet(
        QStringLiteral("color:%1;text-transform:uppercase;letter-spacing:0.4px").arg(css(p.text3)));
    vl->addWidget(title);

    auto *list = new QWidget(box);
    transferLayout_ = new QVBoxLayout(list);
    transferLayout_->setContentsMargins(0, 0, 0, 0);
    transferLayout_->setSpacing(0);   // ui.css .xfer 用 border-bottom 分隔，不留间隙
    // 设计稿里「进行中」和「传输记录」都是 .list 卡片（1px line-2 描边 + r-card
    // 圆角 + surface 底），和剪贴板屏的同步历史是同一个容器样式。
    applyBoxStyle(list,
                  QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                      .arg(css(p.line2)).arg(tokens::kRadiusCard).arg(css(p.surface)));
    vl->addWidget(list);
    return box;
}

QWidget *FilesPanel::buildDropZone()
{
    const Palette &p = Theme::instance().palette();
    auto *zone = new DropZone(this);
    auto *vl = new QVBoxLayout(zone);
    vl->setContentsMargins(tokens::kS5, tokens::kS3, tokens::kS5, tokens::kS3);
    vl->setSpacing(tokens::kS2);

    auto *icon = new QLabel(zone);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(icons::icon(QStringLiteral("upload"), 20, p.text3).pixmap(20, 20));

    auto *text = new QLabel(tr("拖入文件，或点击选择"), zone);
    text->setAlignment(Qt::AlignCenter);
    text->setFont(uiFont(tokens::kFont12, QFont::DemiBold));
    text->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));

    auto *hint = new QLabel(
        tr("单文件 1 B – 16 MiB · 文件名不含路径分隔符"), zone);
    hint->setAlignment(Qt::AlignCenter);
    hint->setFont(uiFont(tokens::kFont11));
    hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));

    vl->addWidget(icon);
    vl->addWidget(text);
    vl->addWidget(hint);
    zone->applyRestingStyle();

    connect(zone, &DropZone::clicked, this, [this] {
        Q_EMIT fileChosen();   // 文件选择对话框由 main 统一弹
    });
    connect(zone, &DropZone::filesDropped, this, [this](const QStringList &paths) {
        if (paths.size() > 1) {
            // 协议一次只走一路传输，别让用户以为多选的都发出去了。
            qCInfo(lcFiles) << "一次仅发送一个文件，已取" << paths.first()
                            << "，忽略其余" << paths.size() - 1 << "个";
        }
        offerFile(paths.first());
    });
    return zone;
}

QWidget *FilesPanel::buildRecords()
{
    const Palette &p = Theme::instance().palette();
    auto *box = new QWidget(this);
    auto *vl = new QVBoxLayout(box);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(tokens::kS1);

    auto *title = new QLabel(tr("传输记录"), box);
    title->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    title->setStyleSheet(
        QStringLiteral("color:%1;text-transform:uppercase;letter-spacing:0.4px").arg(css(p.text3)));
    vl->addWidget(title);

    auto *list = new QWidget(box);
    recordsLayout_ = new QVBoxLayout(list);
    recordsLayout_->setContentsMargins(0, 0, 0, 0);
    recordsLayout_->setSpacing(0);
    applyBoxStyle(list,
                  QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                      .arg(css(p.line2)).arg(tokens::kRadiusCard).arg(css(p.surface)));
    vl->addWidget(list);
    return box;
}

QWidget *FilesPanel::buildFooter()
{
    auto *foot = new QWidget(this);
    auto *hl = new QHBoxLayout(foot);
    hl->setContentsMargins(tokens::kContentPadding, 10, tokens::kContentPadding, 10);
    hl->setSpacing(tokens::kS2);
    const Palette &p = Theme::instance().palette();
    auto *icon = new QLabel(foot);
    icon->setFixedSize(14, 14);
    icon->setPixmap(icons::icon(QStringLiteral("shield"), 14, p.text3).pixmap(14, 14));
    auto *hint = new QLabel(tr("SHA-256 校验后交付"), foot);
    hint->setFont(uiFont(tokens::kFont11));
    hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    hl->addWidget(icon);
    hl->addWidget(hint);
    hl->addStretch(1);

    auto *openDir = new FgButton(tr("接收目录"), FgButton::Variant::Secondary, true, foot);
    openDir->setIcon(QStringLiteral("folder"));
    connect(openDir, &FgButton::clicked, this, [foot] {
        const QString dir = QDir::homePath();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
            qCInfo(lcFiles) << "无法打开接收目录" << dir;
        }
    });
    hl->addWidget(openDir);

    auto *close = new FgButton(tr("关闭"), FgButton::Variant::Ghost, true, foot);
    connect(close, &FgButton::clicked, this, &Panel::hide);
    hl->addWidget(close);
    return foot;
}

int FilesPanel::offerFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcFiles) << "无法读取" << path << f.errorString();
        return -1;
    }
    const QByteArray data = f.readAll();
    f.close();

    // 协议层的边界在这里先挡一道，避免把必然失败的传输丢给 Go。
    if (data.size() < kMinFileSize || static_cast<qint64>(data.size()) > kMaxFileSize) {
        qCWarning(lcFiles) << "文件超出协议上限，已标为超限：" << path << data.size();
        return -1;
    }

    quint16 transferId = 0;
    const int rc = sender_.offer(QFileInfo(path).fileName(), data, &transferId);
    if (rc != 0) {
        qCWarning(lcFiles) << "发起传输失败，错误码" << rc;
        return rc;
    }
    XferEntry e;
    e.name = QFileInfo(path).fileName();
    e.peer = tr("对端");
    e.inbound = false;
    e.totalBytes = data.size();
    entries_.append(e);
    rebuildTransfers();
    return 0;
}

void FilesPanel::pollSessions()
{
    bool dirty = false;

    // 发送侧进度。协议一次只有一路传输，所以对第一条进行中的条目。
    proto::FileSender::State state = proto::FileSender::State::Idle;
    int sent = 0;
    int total = 0;
    int code = 0;
    if (sender_.poll(&state, &sent, &total, &code)) {
        for (XferEntry &e : entries_) {
            if (e.inbound || e.done || e.failed) {
                continue;
            }
            e.sentBytes = sent;
            e.totalBytes = total;
            if (state == proto::FileSender::State::Done) {
                e.done = true;
                e.verified = true;   // Go 侧在交付前已做 SHA-256
                e.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
            } else if (state == proto::FileSender::State::Failed) {
                e.failed = true;
                e.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
                qCWarning(lcFiles) << "传输失败，错误码" << code;
            }
            dirty = true;
            break;
        }
    }

    // 接收侧：待确认 offer 与已完成文件
    proto::FileOffer offer;
    if (receiver_.takeOffer(&offer)) {
        XferEntry e;
        e.name = offer.name;
        e.peer = tr("对端");
        e.inbound = true;
        e.totalBytes = static_cast<int>(offer.size);
        entries_.append(e);
        // 设计稿：提议未确认 30s 失效；这里先按默认接受，真正的确认弹窗要等
        // 传输层能区分「谁在发起」之后才有意义。
        const int rc = receiver_.accept(offer.transferId);
        if (rc != 0) {
            qCWarning(lcFiles) << "接受传输失败，错误码" << rc;
            e.failed = true;
            e.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
        }
        dirty = true;
    }

    QString name;
    QByteArray data;
    int err = 0;
    if (receiver_.takeComplete(&name, &data, &err)) {
        for (XferEntry &e : entries_) {
            if (e.inbound && e.name == name && !e.done && !e.failed) {
                e.done = err == 0;
                e.verified = err == 0;
                e.totalBytes = data.size();
                e.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
                if (err != 0) {
                    e.failed = true;
                    qCWarning(lcFiles) << "接收失败" << name << "错误码" << err;
                }
                dirty = true;
                break;
            }
        }
    }

    if (dirty) {
        rebuildTransfers();
        rebuildRecords();
    }
}

void FilesPanel::rebuildTransfers()
{
    if (transferLayout_ == nullptr) {
        return;
    }
    while (transferLayout_->count() > 0) {
        QLayoutItem *item = transferLayout_->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    const Palette &p = Theme::instance().palette();
    auto *host = transferLayout_->parentWidget();

    int active = 0;
    for (const XferEntry &e : entries_) {
        if (e.done || e.failed || e.rejected) {
            continue;   // 终态条目归入「传输记录」
        }
        ++active;

        // ui.css .xfer：padding 9px 12px / gap 12 / 底部分隔线
        auto *card = new QWidget(host);
        auto *hl = new QHBoxLayout(card);
        hl->setContentsMargins(tokens::kS3, 9, tokens::kS3, 9);
        hl->setSpacing(tokens::kS3);

        hl->addWidget(xferIcon(e.inbound ? QStringLiteral("download")
                                         : QStringLiteral("upload"),
                               {p.surface3, p.surface3, p.text2}, card),
                      0, Qt::AlignTop);

        auto *body = new QWidget(card);
        auto *vl = new QVBoxLayout(body);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(4);

        auto *top = new QHBoxLayout;
        top->setSpacing(tokens::kS2);
        auto *name = new QLabel(e.name, body);
        name->setFont(uiFont(tokens::kFont12, QFont::Medium));
        name->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));
        // 长文件名必须省略号，否则会把整行撑破、右侧百分比被挤出可视区。
        name->setTextInteractionFlags(Qt::NoTextInteraction);
        name->setMinimumWidth(0);
        const int pct = e.totalBytes > 0 ? int(qint64(e.sentBytes) * 100 / e.totalBytes) : 0;
        auto *pctLabel = new QLabel(tr("%1%").arg(pct), body);
        pctLabel->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
        pctLabel->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text2)));
        pctLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pctLabel->setFixedWidth(34);   // ui.css .xfer__pct 的 min-width
        top->addWidget(name, 1);
        top->addWidget(pctLabel, 0, Qt::AlignTop);
        vl->addLayout(top);

        auto *bar = new ProgressTrack(body);
        bar->setProgress(pct);
        vl->addWidget(bar);

        auto *meta = new QLabel(
            tr("%1 · %2").arg(humanSize(e.sentBytes), e.peer), body);
        meta->setFont(uiFont(tokens::kFont11));
        meta->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        vl->addWidget(meta);

        hl->addWidget(body, 1);

        auto *side = new IconButton(QStringLiteral("close"), tr("取消"), card);
        connect(side, &IconButton::clicked, this, [this, name = e.name] {
            // 取消需要协议层的 abort 帧；传输层未落地前只提示，不假装取消成功。
            qCInfo(lcFiles) << "取消传输" << name << "需要传输层落地后才能生效";
        });
        hl->addWidget(side, 0, Qt::AlignTop);

        applyBoxStyle(card, QStringLiteral("border-bottom:1px solid %1").arg(css(p.hairline)));
        transferLayout_->addWidget(card);
    }

    if (active == 0) {
        // 卡片留白来自 .xfer / .list__row 自己的 padding，空列表没有行，
        // 不给的话文字会贴着卡片边框。
        auto *empty = new QLabel(tr("暂无进行中的传输"), host);
        empty->setFont(uiFont(tokens::kFont12));
        empty->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        empty->setAlignment(Qt::AlignCenter);
        empty->setContentsMargins(0, tokens::kS5, 0, tokens::kS5);
        transferLayout_->addWidget(empty);
    }
}

void FilesPanel::rebuildRecords()
{
    if (recordsLayout_ == nullptr) {
        return;
    }
    while (recordsLayout_->count() > 0) {
        QLayoutItem *item = recordsLayout_->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    const Palette &p = Theme::instance().palette();
    auto *host = recordsLayout_->parentWidget();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int shown = 0;
    // 最新的在前：设计稿第一条是 3m、第二条 12m。
    for (int i = entries_.size() - 1; i >= 0; --i) {
        const XferEntry &e = entries_.at(i);
        if (!e.done && !e.failed && !e.rejected) {
            continue;
        }
        ++shown;

        auto *row = new QWidget(host);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(tokens::kS3, tokens::kS2, tokens::kS3, tokens::kS2);
        hl->setSpacing(tokens::kS2);

        const bool ok = e.done && !e.failed && !e.rejected;
        hl->addWidget(xferIcon(ok ? QStringLiteral("check") : QStringLiteral("close"),
                               ok ? p.ok : p.warn, row),
                      0, Qt::AlignVCenter);

        auto *body = new QWidget(row);
        auto *vl = new QVBoxLayout(body);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);

        auto *name = new QLabel(e.name, body);
        name->setFont(uiFont(tokens::kFont12));
        name->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));
        name->setMinimumWidth(0);
        vl->addWidget(name);

        // 设计稿文案：「收到 · Linux Workstation · 2.1 MB · SHA-256 已校验」
        QString hint = e.inbound ? tr("收到") : tr("已发送");
        hint += QStringLiteral(" · ") + e.peer + QStringLiteral(" · ") + humanSize(e.totalBytes);
        if (ok) {
            hint += tr(" · SHA-256 已校验");
        } else if (e.rejected) {
            hint += tr(" · 对端已拒绝");
        }
        auto *sub = new QLabel(hint, body);
        sub->setFont(uiFont(tokens::kFont11));
        sub->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        sub->setMinimumWidth(0);
        vl->addWidget(sub);

        hl->addWidget(body, 1);

        auto *badge = new Badge(
            ok ? tr("完成") : (e.rejected ? tr("已拒绝") : tr("失败")),
            ok ? Badge::Variant::Ok : (e.rejected ? Badge::Variant::Warn
                                                      : Badge::Variant::Danger));
        hl->addWidget(badge, 0, Qt::AlignVCenter);

        auto *ago = new QLabel(agoText(now - e.finishedAtMs), row);
        ago->setFont(uiFont(tokens::kFont11));
        ago->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        ago->setFixedWidth(34);
        ago->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hl->addWidget(ago, 0, Qt::AlignVCenter);

        recordsLayout_->addWidget(row);
    }

    if (shown == 0) {
        auto *empty = new QLabel(tr("暂无传输记录"), host);
        empty->setFont(uiFont(tokens::kFont11));
        empty->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        empty->setAlignment(Qt::AlignCenter);
        empty->setContentsMargins(0, tokens::kS5, 0, tokens::kS5);
        recordsLayout_->addWidget(empty);
    }
}

} // namespace fg
