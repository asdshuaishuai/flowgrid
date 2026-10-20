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
#include <QLineEdit>
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
    if (bytes < 1024LL * 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

// 速率（字节/秒 → 可读文本），与 humanSize 同一套单位。
QString humanRate(double bytesPerSec)
{
    if (bytesPerSec < 1024) {
        return QStringLiteral("%1 B").arg(qRound(bytesPerSec));
    }
    if (bytesPerSec < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytesPerSec / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(bytesPerSec / (1024.0 * 1024.0), 0, 'f', 1);
}

// 剩余时间。速率为 0 时不编一个「剩余 —」以外的数，直接给下限 1s。
QString etaText(int remainBytes, double bytesPerSec)
{
    if (bytesPerSec <= 0) {
        return QStringLiteral("--");
    }
    const int secs = qMax(1, int(qRound(remainBytes / bytesPerSec)));
    if (secs < 60) {
        return QStringLiteral("%1s").arg(secs);
    }
    if (secs < 3600) {
        return QStringLiteral("%1m").arg(secs / 60);
    }
    return QStringLiteral("%1h").arg(secs / 3600);
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
    box->setFixedSize(tokens::kXferIconSize, tokens::kXferIconSize);
    box->setAlignment(Qt::AlignCenter);
    box->setPixmap(icons::icon(name, tokens::kIconSm, c.text).pixmap(tokens::kIconSm, tokens::kIconSm));
    box->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px")
                           .arg(css(c.soft), QString::number(tokens::kRadiusCtl)));
    return box;
}

// 「还在进行中」只有一种定义：没到终态，也没被上限挡下。超限条目根本没上线
// （offerFile 直接记进传输记录），它若算进行中，胶囊与托盘徽标会永远显示一条
// 在跑、而「进行中」列表空空如也；更糟的是轮询会把它当成正在传的那一路，把
// 真正的传输进度全写到这条看不见的条目上。
bool isActive(const XferEntry &e)
{
    return !e.done && !e.failed && !e.rejected && !e.oversize;
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
                           css(Theme::instance().palette().card)));
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

    {
        auto *pill = new QWidget(this);
        // 钉死高度：药丸的圆角是「高的一半」，高度不定就没法算。
        pill->setFixedHeight(tokens::kStatusPillHeight);
        auto *pl = new QHBoxLayout(pill);
        pl->setContentsMargins(tokens::kStatusPillHoriPad, 0, tokens::kStatusPillHoriPad, 0);
        pl->setSpacing(6);
        pillDot_ = new StatusDot(pill);
        pillLabel_ = new QLabel(pill);
        pillLabel_->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
        pl->addWidget(pillDot_);
        pl->addWidget(pillLabel_);
        pill_ = pill;
        setTitleBarWidget(pill);
    }

    body->addWidget(buildTransferList());
    body->addWidget(buildDropZone());
    body->addWidget(buildRecords());
    body->addWidget(buildSettings());
    setFooterWidget(buildFooter());

    // 首帧就要画出空状态，否则只有两个小标题、下面什么都没有，看着像加载坏了。
    rebuildTransfers();
    rebuildRecords();
    Q_EMIT transfersChanged();

    // 会话轮询早先只有 pollSessions() 这个方法、没有任何调用点，等于整块列表
    // 是死的。这里按 500ms 拉一次：Go 侧是非阻塞出队，没数据时开销可忽略，
    // 而面板「按需唤出」的形态也要求唤出瞬间状态就是新的。
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &FilesPanel::pollSessions);
    timer->start(500);

    // 明暗切换后胶囊是本屏唯一直接写样式表的地方，不重刷会留一个旧底色。
    connect(&Theme::instance(), &Theme::changed, this, &FilesPanel::refreshPill);
    refreshPill();
}

void FilesPanel::refreshPill()
{
    const Palette &p = Theme::instance().palette();
    const bool on = Settings::instance().fileTransfer();
    int active = 0;
    for (const XferEntry &e : entries_) {
        if (isActive(e)) {
            ++active;
        }
    }
    // 设计稿写的是「1 项进行中」——胶囊讲的是此刻有几路在跑，不是功能开关。
    pillLabel_->setText(active == 0 ? tr("空闲") : tr("%1 项进行中").arg(active));
    pillDot_->setPulsing(on && active > 0);
    pillDot_->setQuality(on && active > 0 ? Quality::Good : Quality::Idle);
    pill_->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px")
                             .arg(css(on && active > 0 ? p.ok.soft : p.idle.soft),
                                  QString::number(tokens::pillRadius(tokens::kStatusPillHeight))));
}

QWidget *FilesPanel::buildTransferList()
{
    const Palette &p = Theme::instance().palette();
    auto *list = new QWidget(this);
    transferLayout_ = new QVBoxLayout(list);
    transferLayout_->setContentsMargins(0, 0, 0, 0);
    transferLayout_->setSpacing(0);   // ui.css .xfer 用 border-bottom 分隔，不留间隙
    // 设计稿里「进行中」和「传输记录」都是 .list 卡片（1px line-2 描边 + r-card
    // 圆角 + surface 底），和剪贴板屏的同步历史是同一个容器样式；分组标题也是
    // 卡片里的头一行，不是浮在卡片外的小标题。
    transferLayout_->addWidget(groupTitle(tr("进行中"), list));
    transferRows_ = new QVBoxLayout;
    transferRows_->setContentsMargins(0, 0, 0, 0);
    transferRows_->setSpacing(0);
    transferLayout_->addLayout(transferRows_);
    applyBoxStyle(list,
                  QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                      .arg(css(p.line2)).arg(tokens::kRadiusCard).arg(css(p.card)));
    return list;
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
    icon->setPixmap(icons::icon(QStringLiteral("upload"), tokens::kIconLg, p.text3)
                        .pixmap(tokens::kIconLg, tokens::kIconLg));

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
    auto *list = new QWidget(this);
    recordsLayout_ = new QVBoxLayout(list);
    recordsLayout_->setContentsMargins(0, 0, 0, 0);
    recordsLayout_->setSpacing(0);
    recordsLayout_->addWidget(groupTitle(tr("传输记录"), list));
    recordRows_ = new QVBoxLayout;
    recordRows_->setContentsMargins(0, 0, 0, 0);
    recordRows_->setSpacing(0);
    recordsLayout_->addLayout(recordRows_);
    applyBoxStyle(list,
                  QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                      .arg(css(p.line2)).arg(tokens::kRadiusCard).arg(css(p.card)));
    return list;
}

// ── 接收设置（设计稿 .section「接收设置」）─────────────────────────────
//
// 四个控件里只有「自动接收」接到 Settings（复用 sync/file_transfer_enabled，
// 设置屏那一个开关），其余三个协议/传输层还没给出对应的控制口：逐条如实标注
// 或记日志，不做点了没反应、也不假装已经在生效的开关。
QWidget *FilesPanel::buildSettings()
{
    const Palette &p = Theme::instance().palette();
    auto &settings = Settings::instance();

    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    vl->setSpacing(tokens::kS2);

    auto row = [body, &p](const QString &label, QWidget *control, const QString &hint) {
        auto *w = new QWidget(body);
        auto *hl = new QHBoxLayout(w);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(tokens::kS2);
        auto *col = new QVBoxLayout;
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(2);
        auto *text = new QLabel(label, w);
        text->setFont(uiFont(tokens::kFont12));
        col->addWidget(text);
        if (!hint.isEmpty()) {
            auto *hintLabel = new QLabel(hint, w);
            hintLabel->setFont(uiFont(tokens::kFont11));
            hintLabel->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
            hintLabel->setWordWrap(true);
            col->addWidget(hintLabel);
        }
        hl->addLayout(col, 1);
        hl->addWidget(control, 0, Qt::AlignVCenter);
        return w;
    };

    auto *autoToggle = new ToggleSwitch(body);
    autoToggle->setChecked(settings.fileTransfer());
    connect(autoToggle, &ToggleSwitch::clicked, this, [this, autoToggle] {
        Settings::instance().setFileTransfer(autoToggle->isChecked());
        refreshPill();
    });
    vl->addWidget(row(tr("自动接收来自已配对设备的文件"), autoToggle, QString()));

    auto *askToggle = new ToggleSwitch(body);
    askToggle->setChecked(false);
    connect(askToggle, &ToggleSwitch::clicked, this, [askToggle] {
        qCInfo(lcFiles) << "收到前先询问" << (askToggle->isChecked() ? "已开启" : "已关闭")
                        << QStringLiteral("但协议未提供逐条确认口，当前仍自动接受");
    });
    vl->addWidget(row(tr("收到前先询问"), askToggle,
                      tr("协议暂无逐条确认口，当前一律自动接受")));

    // 设计稿这一格是 190px 的 <input value="~/Downloads/FlowGrid">。早先直接填
    // QDir::homePath() 全路径（/home/xxx/Downloads/FlowGrid），190px 装不下，
    // QLineEdit 又会把光标停在末尾，于是界面上只剩「erShuai/Downloads/FlowGrid」
    // 这种从中间截断的残句——用户根本看不出目录在哪。改成家目录缩写，
    // 用户手动改完再把 ~ 展开回去。
    const QString defaultDir = QDir::homePath() + QStringLiteral("/Downloads/FlowGrid");
    const auto shortenDir = [](const QString &path) {
        const QString home = QDir::homePath();
        return path.startsWith(home) ? QStringLiteral("~") + path.mid(home.size()) : path;
    };
    const auto expandDir = [](QString text) {
        text = text.trimmed();
        if (text == QLatin1String("~")) {
            return QDir::homePath();
        }
        if (text.startsWith(QLatin1String("~/"))) {
            return QDir::homePath() + text.mid(1);
        }
        return text;
    };

    auto *dirEdit = new QLineEdit(body);
    dirEdit->setText(shortenDir(defaultDir));
    dirEdit->setFixedWidth(190);
    dirEdit->setFixedHeight(tokens::kControlHeight);
    dirEdit->setFont(uiFont(tokens::kFont12));
    applyInputStyle(dirEdit);
    // 失焦即规范化：留着一段没展开的 ~ 会让「当前接收目录」名不副实。
    connect(dirEdit, &QLineEdit::editingFinished, dirEdit, [dirEdit, expandDir] {
        const QString full = expandDir(dirEdit->text());
        if (full != dirEdit->text()) {
            dirEdit->setText(full);
        }
    });
    auto *dirBox = new QWidget(body);
    auto *dirHl = new QHBoxLayout(dirBox);
    dirHl->setContentsMargins(0, 0, 0, 0);
    dirHl->setSpacing(tokens::kS2);
    dirHl->addWidget(dirEdit);
    auto *changeDir = new FgButton(tr("更改"), FgButton::Variant::Secondary, true, dirBox);
    changeDir->setIcon(QStringLiteral("folder"));
    connect(changeDir, &FgButton::clicked, this, [dirEdit, defaultDir] {
        dirEdit->setText(defaultDir);
        qCInfo(lcFiles) << "接收目录已改回默认路径，协议落地前不持久化";
    });
    dirHl->addWidget(changeDir);
    vl->addWidget(row(tr("接收目录"), dirBox, QString()));

    auto *clearToggle = new ToggleSwitch(body);
    clearToggle->setChecked(false);
    connect(clearToggle, &ToggleSwitch::clicked, this, [clearToggle] {
        qCInfo(lcFiles) << "退出时清空记录" << (clearToggle->isChecked() ? "已开启" : "已关闭")
                        << QStringLiteral("但记录目前只在进程内，退出本就没了");
    });
    vl->addWidget(row(tr("清空传输记录"), clearToggle, QString()));

    // 设计稿把协议给死的四条边界收在这一条里，值只有一个出处。
    vl->addWidget(banner(tr("单文件 1 B – 16 MiB，分块 249 B；提议 30 秒内未确认即失效，"
                            "传输中 10 秒无进展判失败；校验通过前不写入目标路径；"
                            "同一方向同时仅允许一路。"),
                         body, BannerVariant::Neutral));

    auto *badgeHost = new QWidget(this);
    auto *bhl = new QHBoxLayout(badgeHost);
    bhl->setContentsMargins(0, 0, 0, 0);
    bhl->addWidget(new Badge(settings.fileTransfer() ? tr("自动接收已开") : tr("自动接收已关"),
                             settings.fileTransfer() ? Badge::Variant::Ok : Badge::Variant::Neutral,
                             badgeHost));
    return new Collapsible(tr("接收设置"), QStringLiteral("settings"), body, badgeHost, this);
}

QWidget *FilesPanel::buildFooter()
{
    auto *foot = new QWidget(this);
    auto *hl = new QHBoxLayout(foot);
    hl->setContentsMargins(tokens::kContentPadding, 10, tokens::kContentPadding, 10);
    hl->setSpacing(tokens::kS2);
    const Palette &p = Theme::instance().palette();
    auto *icon = new QLabel(foot);
    icon->setFixedSize(tokens::kIconSm, tokens::kIconSm);
    icon->setPixmap(icons::icon(QStringLiteral("shield"), tokens::kIconSm, p.text3).pixmap(tokens::kIconSm, tokens::kIconSm));
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
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
            toast(tr("已打开接收目录"), ToastKind::Ok);
        } else {
            qCInfo(lcFiles) << "无法打开接收目录" << dir;
            toast(tr("无法打开接收目录"), ToastKind::Danger);
        }
    });
    hl->addWidget(openDir);

    auto *close = new FgButton(tr("关闭"), FgButton::Variant::Ghost, true, foot);
    connect(close, &FgButton::clicked, this, &Panel::hide);
    hl->addWidget(close);
    return foot;
}

int FilesPanel::activeCount() const
{
    int n = 0;
    for (const XferEntry &e : entries_) {
        if (isActive(e)) {
            ++n;
        }
    }
    return n;
}

QList<XferSummary> FilesPanel::transferSummary() const
{
    QList<XferSummary> out;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const XferEntry &e : entries_) {
        XferSummary s;
        s.name = e.name;
        s.peer = e.peer;
        s.inbound = e.inbound;
        s.totalBytes = e.totalBytes;
        s.pct = e.totalBytes > 0 ? int(e.sentBytes * 100 / e.totalBytes) : 0;
        s.agoSecs = int((now - e.finishedAtMs) / 1000);
        if (e.oversize) {
            s.state = XferSummary::State::Oversize;
        } else if (e.rejected) {
            s.state = XferSummary::State::Rejected;
        } else if (e.failed) {
            s.state = XferSummary::State::Failed;
        } else if (e.done) {
            s.state = XferSummary::State::Done;
        } else {
            s.state = XferSummary::State::Active;
        }
        out.append(s);
    }
    return out;
}

int FilesPanel::offerFile(const QString &path)
{
    // 有没有能接的对端。
    //
    // 传输层（L3）还没落地，界面上一个对端设备也发现不了。可这一段早先不看
    // 对端，直接建一条「进行中」的记录再提示「已开始发送」——那个提示是假的：
    // 没有任何链路，文件哪儿也没去，记录还会永远卡在「进行中」，托盘徽标
    // 也就永远显示「1 项进行中」。这里没有对端就直接说没有，不建假记录。
    int peers = 0;
    for (const Device &d : DeviceManager::instance().devices()) {
        if (d.online && !d.isLocal) {
            ++peers;
        }
    }
    if (peers == 0) {
        qCInfo(lcFiles) << "没有已连接的对端，传输未发起" << path;
        toast(tr("没有已连接的对端，文件未发送"), ToastKind::Info);
        return -1;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcFiles) << "无法读取" << path << f.errorString();
        return -1;
    }
    const QByteArray data = f.readAll();
    f.close();

    // 协议层的边界在这里先挡一道，避免把必然失败的传输丢给 Go。
    // 但条目要留在「传输记录」里并标成超限：设计稿明确给了这一档状态，
    // 直接 return 掉的话用户只会看到「点了没反应」，不知道是被 16 MiB 挡了。
    if (data.size() < kMinFileSize || static_cast<qint64>(data.size()) > kMaxFileSize) {
        qCWarning(lcFiles) << "文件超出协议上限，已标为超限：" << path << data.size();
        XferEntry e;
        e.name = QFileInfo(path).fileName();
        e.peer = tr("对端");
        e.totalBytes = data.size();
        e.oversize = true;
        e.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
        entries_.append(e);
        rebuildRecords();
        refreshPill();
        Q_EMIT transfersChanged();
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
    e.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    entries_.append(e);
    rebuildTransfers();
    refreshPill();
    Q_EMIT transfersChanged();
    toast(tr("已开始发送 %1").arg(e.name), ToastKind::Ok);
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
            if (e.inbound || !isActive(e)) {
                continue;
            }
            // 没有传输时 Go 侧一样是「成功」返回，报的就是 0/0（只有句柄失效才
            // 返回失败）。照单全收的话，每 500ms 就把条目的大小刷成「0 B」、
            // 进度条清零，还白重建一次列表——实测那条 4.2 GB 的记录会变成
            // 「0 B · 超出单文件上限」。
            const bool finished = state == proto::FileSender::State::Done
                                  || state == proto::FileSender::State::Failed;
            if (!finished && total <= 0) {
                break;
            }
            if (total > 0) {
                dirty = dirty || e.totalBytes != total || e.sentBytes != sent;
                e.totalBytes = total;
                e.sentBytes = sent;
            }
            if (state == proto::FileSender::State::Done) {
                e.done = true;
                e.verified = true;   // Go 侧在交付前已做 SHA-256
                e.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
                dirty = true;
            } else if (state == proto::FileSender::State::Failed) {
                // 对端拒绝（0x16）是「对端的决定」，不是数据坏了。协议用 errCode
                // 把两者分开了，记录里也该分档。
                e.errCode = code;
                e.rejected = code == proto::errorcode::kTransferRejected;
                e.failed = !e.rejected;
                e.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
                qCWarning(lcFiles) << "传输失败，错误码" << code;
                dirty = true;
            }
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
        e.startedAtMs = QDateTime::currentMSecsSinceEpoch();
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
            if (e.inbound && e.name == name && isActive(e)) {
                e.errCode = err;
                e.done = err == 0;
                e.verified = err == 0;
                if (err == 0) {
                    e.totalBytes = data.size();
                } else if (err == proto::errorcode::kTransferRejected) {
                    // 中途 abort 走 0x16，与「校验失败」不是一回事。
                    e.rejected = true;
                    qCWarning(lcFiles) << "接收被中止" << name << "错误码" << err;
                } else {
                    // 失败时 Go 侧不带数据回来（data 为空），这里保留 offer 里的
                    // 文件大小：写成 0 B 等于说这个文件是空的。
                    e.failed = true;
                    qCWarning(lcFiles) << "接收失败" << name << "错误码" << err;
                }
                e.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
                dirty = true;
                break;
            }
        }
    }

    if (dirty) {
        rebuildTransfers();
        rebuildRecords();
        refreshPill();
        Q_EMIT transfersChanged();
    }
}

void FilesPanel::rebuildTransfers()
{
    if (transferLayout_ == nullptr) {
        return;
    }
    clearLayout(transferRows_);
    const Palette &p = Theme::instance().palette();
    auto *host = transferRows_->parentWidget();

    int active = 0;
    for (const XferEntry &e : entries_) {
        if (!isActive(e)) {
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
        // 长文件名必须省略号，否则会把整行撑破、右侧百分比被挤出可视区。
        auto *name = new ElidedLabel(e.name, body);
        name->setFont(uiFont(tokens::kFont12, QFont::Medium));
        name->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));
        const int pct =
            e.totalBytes > 0 ? int(e.sentBytes * 100 / e.totalBytes) : 0;
        auto *pctLabel = new QLabel(tr("%1%").arg(pct), body);
        // .xfer__pct 明确指定了 font-mono + tabular-nums：百分比每 500ms 跳一次，
        // 不等宽的话整行会左右抖。
        pctLabel->setFont(uiMonoFont(tokens::kFont11, QFont::DemiBold));
        pctLabel->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text2)));
        pctLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pctLabel->setFixedWidth(tokens::kXferPctWidth);   // ui.css .xfer__pct 的 min-width
        top->addWidget(name, 1);
        top->addWidget(pctLabel, 0, Qt::AlignTop);
        vl->addLayout(top);

        auto *bar = new ProgressTrack(body);
        bar->setProgress(pct);
        vl->addWidget(bar);

        // .xfer__meta：大小 · 方向 · 速率 · 剩余。入站方向没有速率可算——
        // FileReceiver 只导出 takeOffer / takeComplete，没有 FileSender::poll
        // 那样的进度口，拿不到已收字节数（已开 issue #3），所以如实显示「—」。
        // .xfer__meta：大小 · 方向 · 速率 · 剩余，合成一个可省略的单行。
        // 拆成七八个 QLabel 的话，每个的 minimumSizeHint 都按全文算，加起来的
        // 最小宽度会顶穿定宽浮板；ElidedLabel 的横向最小宽度是 0，超长时
        // 整行统一截断而不是把右侧挤出去。
        QStringList parts;
        parts << humanSize(e.totalBytes);
        const qint64 elapsed = e.startedAtMs > 0 ? QDateTime::currentMSecsSinceEpoch()
                                                   - e.startedAtMs
                                                  : 0;
        if (e.inbound) {
            // 入站没有速率可算：FileReceiver 只导出 takeOffer / takeComplete，
            // 没有 FileSender::poll 那样的进度口，拿不到已收字节数（已开 issue）。
            parts << tr("来自 %1").arg(e.peer) << tr("接收中 · 进度不可见");
        } else {
            // 设计稿这里是一个 arrowRight 图标；这里用同形的「→」字符，
            // 免得整行拆成七八个控件后又在宽度上打架。
            parts << QStringLiteral("→ %1").arg(e.peer);
            const double bps =
                elapsed > 0 && e.sentBytes > 0 ? double(e.sentBytes) * 1000.0 / double(elapsed) : 0;
            if (bps > 0) {
                parts << tr("%1/s").arg(humanRate(bps));
                if (e.totalBytes > e.sentBytes) {
                    parts << tr("剩余 %1").arg(etaText(e.totalBytes - e.sentBytes, bps));
                }
            }
        }
        auto *meta = new ElidedLabel(parts.join(QStringLiteral(" · ")), body);
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
        transferRows_->addWidget(card);
    }

    if (active == 0) {
        // 卡片留白来自 .xfer / .list__row 自己的 padding，空列表没有行，
        // 不给的话文字会贴着卡片边框。
        transferRows_->addWidget(new EmptyState(QStringLiteral("upload"),
                                                tr("没有正在传的文件"),
                                                tr("从下面的拖放区选一个文件开始"), host));
    }
}

void FilesPanel::rebuildRecords()
{
    if (recordsLayout_ == nullptr) {
        return;
    }
    clearLayout(recordRows_);
    const Palette &p = Theme::instance().palette();
    auto *host = recordRows_->parentWidget();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int shown = 0;
    // 最新的在前：设计稿第一条是 3m、第二条 12m。
    for (int i = entries_.size() - 1; i >= 0; --i) {
        const XferEntry &e = entries_.at(i);
        if (isActive(e)) {
            continue;
        }
        ++shown;

        const bool ok = e.done && !e.failed && !e.rejected && !e.oversize;
        // 「校验失败」只留给校验/载荷类错误（0x12）。超时、未连接、发送失败
        // 也一律写成「SHA-256 不符，未交付」，是在替文件喊冤。
        const bool checksumFail =
            e.failed && e.errCode == proto::errorcode::kInvalidPayload;
        // 三种失败各有各的图标与配色：拒绝是对端的决定、校验失败是数据损坏、
        // 超限是根本没上线。对不上号只会让用户以为是同一种问题。
        const StatusColor tone =
            ok ? p.ok : (e.oversize ? p.danger : (e.rejected ? p.warn : p.danger));
        const QString iconName =
            ok ? QStringLiteral("check")
               : (e.oversize ? QStringLiteral("warn")
                             : (e.rejected ? QStringLiteral("close")
                                           : QStringLiteral("warn")));

        // 设计稿文案：「收到 · Linux Workstation · 2.1 MB · SHA-256 已校验」
        QString hint;
        if (e.oversize) {
            // 超限的文件压根没上线，说「已发送」是不对的。
            hint = tr("%1 · 超出单文件上限，未在链路上传输").arg(humanSize(e.totalBytes));
        } else {
            hint = e.inbound ? tr("收到") : tr("已发送");
            hint += QStringLiteral(" · ") + e.peer + QStringLiteral(" · ")
                  + humanSize(e.totalBytes);
            if (ok) {
                hint += tr(" · SHA-256 已校验");
            } else if (e.rejected) {
                hint += tr(" · 对端已拒绝");
            } else if (checksumFail) {
                hint += tr(" · SHA-256 不符，未交付");
            } else {
                hint += tr(" · 传输中断，未交付");
            }
        }

        // 设计稿 .list__row + .xfer__icon + .list__label，与剪贴板屏的历史行是
        // 同一套结构，复用 ClipRow 顺带把长文件名/长对端名的省略交给它处理
        // （这两行文字先前各写一遍标签布局，都是按全文算最小宽度，把内容区
        // 撑到 586px，超出定宽 520 的浮板后右半截直接被裁掉）。
        auto *trailing = new QWidget(host);
        auto *tl = new QHBoxLayout(trailing);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(tokens::kS1);
        auto *badge = new Badge(
            ok ? tr("完成")
               : (e.oversize ? tr("超限")
                             : (e.rejected ? tr("已拒绝")
                                           : (checksumFail ? tr("校验失败")
                                                           : tr("传输失败")))),
            ok ? Badge::Variant::Ok : (e.rejected ? Badge::Variant::Warn
                                                  : Badge::Variant::Danger),
            trailing);
        tl->addWidget(badge, 0, Qt::AlignVCenter);
        auto *ago = new QLabel(agoText(now - e.finishedAtMs), trailing);
        ago->setFont(uiFont(tokens::kFont11));
        ago->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        // 34 是「装得下「2分钟前」」的实用宽度，与 .xfer__pct 的 34 无血缘——
        // 那个是设计稿 min-width，这个是 Qt 侧按内容定的，别合并成一个令牌。
        ago->setFixedWidth(34);
        ago->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        tl->addWidget(ago, 0, Qt::AlignVCenter);

        auto *row = new ClipRow(QString(), e.name, hint, trailing, host,
                                xferIcon(iconName, tone, host));

        recordRows_->addWidget(row);
    }

    if (shown == 0) {
        recordRows_->addWidget(new EmptyState(QStringLiteral("file"), tr("还没有传输记录"),
                                              tr("传完或失败的文件会留在这里"), host));
    }
}

} // namespace fg
