#include "clipboardpanel.h"
#include "core/designtokens.h"
#include "core/hostinfo.h"
#include "core/settings.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QLoggingCategory>

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QStringList>
#include <QTextLayout>
#include <QTimer>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcClipboard, "flowgrid.clipboard")

namespace fg {
namespace {

// 设计稿明确给出的分段上限（PROTOCOL.md §5.3）：单条超过 63,240 B 即超限，
// 历史里标记但不入库，也不推送。
constexpr int kClipSegLimit = 63240;

// 预览正文显示几行。设计稿 .preview__body 给的是 max-height:92px + 内部滚动，
// 减去上下 12px 内边距还剩 68px，按 --lh-13 折正好 3 行。
constexpr int kPreviewLines = 3;

// 条目归类：分段控件的五个档位就是靠它过滤的。协议帧只给 MIME + 载荷
// （PROTOCOL.md §5.3），所以分类只能从这两样推，推不出就归「文本」，
// 不去猜来源应用的语义。
QString kindOf(const QString &mime, const QByteArray &raw)
{
    if (mime.startsWith(QLatin1String("image/"))) {
        return QStringLiteral("image");
    }
    if (mime == QLatin1String("text/uri-list")) {
        return QStringLiteral("link");
    }
    if (mime.contains(QLatin1String("html"))) {
        return QStringLiteral("code");
    }
    const QString text = QString::fromUtf8(raw).trimmed();
    if (text.startsWith(QLatin1String("http://"))
        || text.startsWith(QLatin1String("https://"))) {
        return QStringLiteral("link");
    }
    return QStringLiteral("text");
}

QString iconOfKind(const QString &kind)
{
    if (kind == QLatin1String("image")) {
        return QStringLiteral("image");
    }
    if (kind == QLatin1String("code")) {
        return QStringLiteral("code");
    }
    if (kind == QLatin1String("link")) {
        return QStringLiteral("link");
    }
    return QStringLiteral("type");
}

// 只看 MIME 的粗分类。超限条目的载荷可能有几百 MB：为了分个类把整包解成
// QString 就是几百 MB 的额外内存，纯属白扔。
QString kindOfMime(const QString &mime)
{
    if (mime.startsWith(QLatin1String("image/"))) {
        return QStringLiteral("image");
    }
    if (mime == QLatin1String("text/uri-list")) {
        return QStringLiteral("link");
    }
    if (mime.contains(QLatin1String("html"))) {
        return QStringLiteral("code");
    }
    return QStringLiteral("text");
}

// 列表主文本压成一行：换行会让 ClipRow 的双行结构失效。
QString oneLine(const QString &text)
{
    QString s = text;
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('\t'), QLatin1Char(' '));
    s = s.simplified();
    return s;
}

// 条目年龄：从产生时刻算，不是从入库时抄下来的那个数。
// 没有时间戳的（协议侧入站的旧条目）退回 agoSecs。
int ageSecsOf(const ClipEntry &e)
{
    if (e.atMs <= 0) {
        return e.agoSecs;
    }
    return static_cast<int>((QDateTime::currentMSecsSinceEpoch() - e.atMs) / 1000);
}

// 页脚那行载荷规格。设计稿写的是「62 B · 单帧发送 · 未分段」——那是 62 字节
// 示例数据下的结论，不是任何载荷的通用事实：单帧能装多少由 MIME 长度决定
// （Go 侧 MaxClipboardChunk = 255 - 7 - len(mime)，§5.3），text/plain 时超过
// 238 B 就必然多帧。这里按协议同一条公式算段数，不无条件宣称「未分段」。
//
// 多帧那半句跟 mac 端对齐：「分段发送 · N 段」（不是「分 N 帧发送」）。
QString specText(const ClipEntry &e)
{
    const qint64 size = e.sizeBytes > 0 ? e.sizeBytes : e.raw.size();
    if (e.oversize) {
        // 超限的条目没有载荷可写可推，直说它超在哪，别拿「暂无内容」搪塞。
        return ClipboardPanel::tr("%1 · 超出单条上限，不同步").arg(humanSize(size));
    }
    if (size <= 0) {
        // 空内容时照实说「暂无内容」。
        //
        // 早先这里写的是 "--"，界面上就只剩一个信息图标后面跟着两个横杠：
        // 看不出是「没有内容」还是「有内容但取不到大小」，更看不出该按钮能不能
        // 按。按「不假装已生效」的要求，空就是空，写明白。
        return ClipboardPanel::tr("暂无内容");
    }
    const int perFrame = 248 - e.mime.toUtf8().size();
    if (perFrame <= 0) {
        return ClipboardPanel::tr("%1 · MIME 名过长，协议会拒绝发送").arg(humanSize(size));
    }
    const int frames = static_cast<int>((size + perFrame - 1) / perFrame);
    return frames <= 1
               ? ClipboardPanel::tr("%1 · 单帧发送 · 未分段").arg(humanSize(size))
               : ClipboardPanel::tr("%1 · 分段发送 · %2 段")
                     .arg(humanSize(size))
                     .arg(frames);
}

// 超限条目的行首方片：warn 图标 + warn-soft 底。普通条目是 surface-3 底 +
// text-2 图标（ClipRow 默认那套），超限那条要一眼看得出不一样。
QLabel *oversizeChip(QWidget *parent)
{
    const Palette &p = Theme::instance().palette();
    auto *box = new QLabel(parent);
    box->setFixedSize(tokens::kBadgeHeight, tokens::kBadgeHeight);
    box->setAlignment(Qt::AlignCenter);
    box->setPixmap(icons::icon(QStringLiteral("warn"), tokens::kIconXs, p.warn.text)
                       .pixmap(tokens::kIconXs, tokens::kIconXs));
    applyBoxStyle(box, QStringLiteral("border-radius:%1px;background:%2")
                           .arg(QString::number(tokens::kRadiusChip), cssColor(p.warn.soft)));
    return box;
}

// 「标签 + 开关」一行：ui.css .row.between + .switch。
QWidget *switchRow(const QString &label, bool checked, const QString &hint, QWidget *parent)
{
    const Palette &p = Theme::instance().palette();
    auto *w = new QWidget(parent);
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
        hintLabel->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
        hintLabel->setWordWrap(true);
        col->addWidget(hintLabel);
    }
    hl->addLayout(col, 1);
    auto *toggle = new ToggleSwitch(w);
    toggle->setChecked(checked);
    hl->addWidget(toggle, 0, Qt::AlignVCenter);
    return w;
}

} // namespace

ClipboardPanel::ClipboardPanel(QWidget *parent) : Panel(tr("剪贴板"), parent)
{
    auto *body = qobject_cast<QVBoxLayout *>(content()->layout());
    auto &settings = Settings::instance();

    // 标题栏右侧：同步状态胶囊
    {
        auto *pill = new QWidget(this);
        pill->setFixedHeight(tokens::kStatusPillHeight);  // 圆角取高的一半，高度得先定
        auto *pl = new QHBoxLayout(pill);
        pl->setContentsMargins(tokens::kStatusPillHoriPad, 0, tokens::kStatusPillHoriPad, 0);
        pl->setSpacing(6);
        auto *dot = new StatusDot(pill);
        dot->setPulsing(settings.clipboardSync());
        auto *label = new QLabel(settings.clipboardSync() ? tr("同步中") : tr("已关闭"), pill);
        label->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
        pl->addWidget(dot);
        pl->addWidget(label);
        pill->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px")
                                .arg(cssColor(Theme::instance().palette().ok.soft),
                                     QString::number(tokens::pillRadius(tokens::kStatusPillHeight))));
        setTitleBarWidget(pill);
        pill_ = pill;
        pillDot_ = dot;
        pillLabel_ = label;
    }

    body->addWidget(buildCurrent());
    body->addWidget(buildFilterRow());
    body->addWidget(buildHistory());
    body->addWidget(buildSettings());
    setFooterWidget(buildFooter());

    QObject::connect(&settings, &Settings::changed, this, [this](const QString &key) {
        if (key.startsWith(QLatin1String("sync/"))) {
            refreshSyncState();
        }
    });
    // 胶囊是本屏唯一直接写样式表的地方，明暗切换后不重刷就会留一个旧底色。
    QObject::connect(&Theme::instance(), &Theme::changed, this, &ClipboardPanel::refreshSyncState);
    refreshSyncState();
    reloadHistory();

    // 本机剪贴板：用户复制一次，这里就多一条**真实**记录。
    // dataChanged 在「写入本地」那条路径上也会触发（那正是我们要的：写入后
    // 历史里应该出现这条），去重靠下面 captureLocalClipboard 里的内容比对。
    if (QClipboard *clip = QApplication::clipboard()) {
        connect(clip, &QClipboard::dataChanged, this, &ClipboardPanel::captureLocalClipboard);
    }
    captureLocalClipboard();

    // 「12s」这类时间标记是算出来的，得有人按秒去重算。整块历史重建一次
    // 成本不高（列表上限 50 条），但只在浮板可见时做，藏着的时候不浪费。
    auto *ageTimer = new QTimer(this);
    ageTimer->setInterval(5000);
    connect(ageTimer, &QTimer::timeout, this, [this] {
        if (isVisible() && !history_.isEmpty()) {
            rebuildHistory();
        }
    });
    ageTimer->start();
}

void ClipboardPanel::captureLocalClipboard()
{
    QClipboard *clip = QApplication::clipboard();
    if (clip == nullptr) {
        return;
    }
    const QMimeData *mime = clip->mimeData();
    if (mime == nullptr) {
        return;
    }

    // MIME 的挑法：优先纯文本——富文本剪贴板里同时有 text/plain 与
    // text/html，存纯文本那一路，用户看到的就是他复制的东西本身；
    // 其次是链接（text/uri-list），再是 html；都没有才退到任意一种格式。
    // 「任意一种」也只取一个：一次复制在历史里只算一条。
    const QStringList formats = mime->formats();
    QString type;
    for (const QString &candidate : {QStringLiteral("text/plain"), QStringLiteral("text/uri-list"),
                                     QStringLiteral("text/html")}) {
        if (formats.contains(candidate)) {
            type = candidate;
            break;
        }
    }
    if (type.isEmpty()) {
        for (const QString &f : formats) {
            if (!mime->data(f).isEmpty()) {
                type = f;
                break;
            }
        }
    }
    if (type.isEmpty()) {
        return;
    }

    const QByteArray raw = mime->data(type);
    if (raw.isEmpty()) {
        return;
    }

    // 同一条内容不重复记。剪贴板一次复制常连发好几个 dataChanged
    // （应用逐段设置 mime），不去重的话历史里会连着出现三条一样的。
    // 超限条目不留载荷，只能拿「类型 + 体积」当指纹。
    if (!history_.isEmpty()) {
        const ClipEntry &last = history_.first();
        const bool same = last.mime == type
            && (raw.size() > kClipSegLimit
                    ? last.oversize && last.sizeBytes == raw.size()
                    : last.raw == raw);
        if (same) {
            return;
        }
    }

    ClipEntry e;
    e.mime = type;
    e.source = hostinfo::hostName();   // 真实来源：这台机器
    e.atMs = QDateTime::currentMSecsSinceEpoch();
    e.agoSecs = 0;
    e.fromPeer = false;
    e.sizeBytes = raw.size();
    e.oversize = raw.size() > kClipSegLimit;
    if (e.oversize) {
        // 超限的条目推不出去（协议 §5.3 一条 63,240 B），但**要留在历史里**
        // 并标成「超限」：mac 端与设计稿 clipboard.html 都有这一行。直接丢掉
        // 的话，用户复制了一张大图，屏幕上什么也不变，只能以为没复制成功。
        //
        // 载荷不留——一次复制可能几百 MB，留在历史里就是几十张图的内存；只留
        // 体积和一小段预览，够认出「我刚才复制的是哪一坨」。
        e.raw.clear();
        e.kind = kindOfMime(type);
        qCInfo(lcClipboard) << "本机剪贴板条目超单条上限" << raw.size()
                            << QStringLiteral("B，标记为超限，不推送");
    } else {
        e.raw = raw;
        e.kind = kindOf(type, raw);
    }
    // 纯文本给原文；别的类型给「类型 + 体积」，总比一句假摘要强。
    if (type == QLatin1String("text/plain") || type == QLatin1String("text/html")
        || type == QLatin1String("text/uri-list")) {
        e.text = oneLine(e.oversize
                             ? QString::fromUtf8(raw.left(240)).left(60)
                             : QString::fromUtf8(raw));
    } else {
        e.text = tr("%1 · %2").arg(type, humanSize(raw.size()));
    }

    // 历史的不变量是 history_[0] 最新（顶部预览取 first()，列表也按原序渲染）。
    history_.prepend(e);
    trimHistory();
    rebuildHistory();
    Q_EMIT transfersChanged();
}

// ── 当前剪贴板（设计稿 .preview：head / body / foot 三段）────────────────
QWidget *ClipboardPanel::buildCurrent()
{
    const Palette &p = Theme::instance().palette();
    auto *card = new QWidget(this);
    auto *vl = new QVBoxLayout(card);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // __head：surface 底 + 下 hairline，与 body 的 surface-2 底形成一条缝。
    auto *head = new QWidget(card);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(7, tokens::kS3, tokens::kS3, 7);
    hl->setSpacing(tokens::kS2);
    auto *typeIcon = new QLabel(head);
    typeIcon->setFixedSize(tokens::kBadgeHeight, tokens::kBadgeHeight);
    typeIcon->setAlignment(Qt::AlignCenter);
    typeIcon->setPixmap(icons::icon(QStringLiteral("type"), tokens::kIconXs, p.text2).pixmap(tokens::kIconXs, tokens::kIconXs));
    applyBoxStyle(typeIcon,
                  QStringLiteral("border-radius:%1px;background:%2")
                      .arg(QString::number(tokens::kRadiusChip), cssColor(p.surface3)));
    typeIcon_ = typeIcon;
    auto *title = new QLabel(tr("当前剪贴板"), head);
    title->setFont(uiFont(tokens::kFont12, QFont::DemiBold));
    auto *mimeBadge = new Badge(QStringLiteral("--"), Badge::Variant::Neutral, head);
    mimeBadge_ = mimeBadge;
    hl->addWidget(typeIcon);
    hl->addWidget(title);
    hl->addWidget(mimeBadge);
    hl->addStretch(1);
    // 右侧「来自 …」：mac 端只有**收到对端**的条目才画这一块（fromDevice 为
    // 空时整块不画），本机复制的那条不画。早先这里写死一句「来自 本机」，
    // 收到对端条目时它还照旧写着「来自 本机」——那是在说假话。
    auto *fromBox = new QWidget(head);
    auto *fromHl = new QHBoxLayout(fromBox);
    fromHl->setContentsMargins(0, 0, 0, 0);
    fromHl->setSpacing(tokens::kS1);
    auto *fromIcon = new QLabel(fromBox);
    fromIcon->setFixedSize(tokens::kIconSm, tokens::kIconSm);
    fromIcon->setPixmap(icons::icon(QStringLiteral("laptop"), tokens::kIconSm, p.text3).pixmap(tokens::kIconSm, tokens::kIconSm));
    fromLabel_ = new QLabel(fromBox);
    fromLabel_->setFont(uiFont(tokens::kFont11));
    fromLabel_->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
    fromHl->addWidget(fromIcon);
    fromHl->addWidget(fromLabel_);
    hl->addWidget(fromBox);
    fromBox_ = fromBox;
    applyBoxStyle(head,
                  QStringLiteral("background:%1;border-bottom:1px solid %2;"
                                 "border-top-left-radius:%3px;border-top-right-radius:%3px")
                      .arg(cssColor(p.card), cssColor(p.hairline))
                      .arg(tokens::kRadiusCard));
    vl->addWidget(head);

    // __body：等宽字体原文，最多 3 行，超出省略——设计稿给的是 max-height 92
    // 加滚动，浮板里再套一个滚动区会把整屏高度算错，所以改成按行截断。
    // 截断必须自己做：QLabel 只要开着 wordWrap 就会一路撑高，没有 maxHeight
    // 它能排到几千行，一条 63 KB 的剪贴板能把整块浮板顶出屏幕。
    auto *bodyBox = new QWidget(card);
    auto *bl = new QVBoxLayout(bodyBox);
    bl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    preview_ = new QLabel(tr("暂无剪贴板内容"), bodyBox);
    preview_->setFont(uiMonoFont(tokens::kFont12));
    preview_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    preview_->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text1)));
    preview_->setFixedHeight(QFontMetrics(preview_->font()).lineSpacing());
    bl->addWidget(preview_);
    applyBoxStyle(bodyBox, QStringLiteral("background:%1").arg(cssColor(p.well)));
    vl->addWidget(bodyBox);

    // __foot：surface 底 + 上 hairline，左边讲这条载荷的规格，右边两个动作。
    auto *foot = new QWidget(card);
    auto *fl = new QHBoxLayout(foot);
    fl->setContentsMargins(6, tokens::kS3, tokens::kS3, 6);
    fl->setSpacing(tokens::kS2);
    auto *info = new QLabel(foot);
    info->setFixedSize(tokens::kIconSm, tokens::kIconSm);
    info->setPixmap(icons::icon(QStringLiteral("info"), tokens::kIconSm, p.text3).pixmap(tokens::kIconSm, tokens::kIconSm));
    spec_ = new QLabel(tr("--"), foot);
    spec_->setFont(uiFont(tokens::kFont11));
    spec_->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
    fl->addWidget(info);
    fl->addWidget(spec_);
    fl->addStretch(1);
    auto *write = new FgButton(tr("写入本地"), FgButton::Variant::Ghost, true, foot);
    write->setIcon(QStringLiteral("copy"));
    writeBtn_ = write;
    connect(write, &FgButton::clicked, this, [this] {
        if (current_.raw.isEmpty()) {
            return;
        }
        Q_EMIT localWriteRequested(current_);
        toast(tr("已写入本地剪贴板"), ToastKind::Ok);
    });
    auto *push = new FgButton(tr("推送"), FgButton::Variant::Secondary, true, foot);
    push->setIcon(QStringLiteral("arrowRight"));
    pushBtn_ = push;
    connect(push, &FgButton::clicked, this, [this] {
        if (current_.raw.isEmpty()) {
            return;
        }
        const ClipEntry &e = current_;
        // 真实发送：交给 Go 的 ClipboardSession 分段，UI 不碰帧结构。
        //
        // send 的返回值必须检查：超 63,240 B 的载荷在 Go 侧 SegmentClipboard
        // 就被拒了（ErrInvalidPayload），一个字节都不会发出。早先这里丢掉返回
        // 值、无条件报「已推送」——用户看到成功提示，对端却什么都没收到，比
        // 报错更难查。
        const int err = session_.send(e.mime.toUtf8(), e.raw);
        if (err != 0) {
            toast(proto::errorString(err), ToastKind::Danger);
            return;
        }
        toast(tr("已推送"), ToastKind::Ok);
    });
    fl->addWidget(write);
    fl->addWidget(push);
    applyBoxStyle(foot,
                  QStringLiteral("background:%1;border-top:1px solid %2;"
                                 "border-bottom-left-radius:%3px;border-bottom-right-radius:%3px")
                      .arg(cssColor(p.card), cssColor(p.hairline))
                      .arg(tokens::kRadiusCard));
    vl->addWidget(foot);

    // 外框只补左右两侧：上下两段自带 hairline 与圆角，外框再画一圈会叠成 2px。
    applyBoxStyle(card,
                  QStringLiteral("border-left:1px solid %1;border-right:1px solid %1")
                      .arg(cssColor(p.line1)));
    return card;
}

// ── 过滤 + 搜索（设计稿 .row.between：.seg + 150px .input）───────────────
QWidget *ClipboardPanel::buildFilterRow()
{
    auto *row = new QWidget(this);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS2);

    filter_ = new SegmentedControl({tr("全部"), tr("文本"), tr("代码"), tr("链接"), tr("图片")},
                                   row);
    connect(filter_, &SegmentedControl::currentIndexChanged, this,
            [this](int) { rebuildHistory(); });
    hl->addWidget(filter_, 0, Qt::AlignVCenter);

    hl->addStretch(1);

    search_ = new QLineEdit(row);
    search_->setPlaceholderText(tr("搜索历史"));
    search_->setFixedWidth(150);
    search_->setFixedHeight(tokens::kControlHeight);
    search_->setFont(uiFont(tokens::kFont12));
    applyInputStyle(search_);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString &) { rebuildHistory(); });
    hl->addWidget(search_, 0, Qt::AlignVCenter);
    return row;
}

QWidget *ClipboardPanel::buildHistory()
{
    const Palette &p = Theme::instance().palette();
    auto *list = new QWidget(this);
    historyLayout_ = new QVBoxLayout(list);
    historyLayout_->setContentsMargins(0, 0, 0, 0);
    historyLayout_->setSpacing(0);

    // 设计稿的 .list__group-title 是在 .list 卡片**里面**的头一行，不是浮在
    // 卡片外的小标题：surface-2 底 + 下 hairline，跟着列表一起滚动。五屏共用
    // widgets.cpp 的 groupTitle()，免得 padding 与底色各写一份、各漂一点。
    historyLayout_->addWidget(groupTitle(
        tr("同步历史 · 保留最近 %1 条").arg(Settings::instance().clipboardHistory()), list));

    rowsLayout_ = new QVBoxLayout;
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    rowsLayout_->setSpacing(0);
    historyLayout_->addLayout(rowsLayout_);

    applyBoxStyle(list,
                  QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                      .arg(cssColor(p.line2))
                      .arg(tokens::kRadiusCard)
                      .arg(cssColor(p.card)));
    return list;
}

// ── 同步设置（4 个真开关 + 一条协议边界说明）────────────────────────────
QWidget *ClipboardPanel::buildSettings()
{
    auto &s = Settings::instance();

    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    // 行距跟 mac 端对齐：FGSection 内容是 VStack(spacing: s3)，设计稿
    // .section__body 里也是 .col-3（gap 12）。这里原来写的是 s2(8)，四行开关
    // 比设计稿挤一档。
    vl->setSpacing(tokens::kS3);

    auto *sync = switchRow(tr("跨端同步"), s.clipboardSync(), QString(), body);
    auto *syncToggle = sync->findChild<ToggleSwitch *>();
    connect(syncToggle, &ToggleSwitch::clicked, this, [this, syncToggle] {
        Settings::instance().setClipboardSync(syncToggle->isChecked());
    });
    vl->addWidget(sync);

    auto *images = switchRow(tr("同步图片"), s.clipboardImages(), QString(), body);
    auto *imagesToggle = images->findChild<ToggleSwitch *>();
    connect(imagesToggle, &ToggleSwitch::clicked, this, [this, imagesToggle] {
        Settings::instance().setClipboardImages(imagesToggle->isChecked());
    });
    vl->addWidget(images);

    // 这一项协议给不出依据：CLIPBOARD 帧只有 MIME + 载荷（PROTOCOL.md §5.3），
    // 没有来源应用，客户端无从判断「这条是不是密码管理器复制出来的」。开关照
    // 设计稿摆上并落盘，但如实标注它当前不生效，也不假装已经在过滤。
    auto *ignore = switchRow(tr("忽略密码管理器内容"), s.clipboardIgnorePasswords(),
                             tr("协议帧不含来源应用标识，待协议补充后生效"), body);
    auto *ignoreToggle = ignore->findChild<ToggleSwitch *>();
    connect(ignoreToggle, &ToggleSwitch::clicked, this, [this, ignoreToggle] {
        Settings::instance().setClipboardIgnorePasswords(ignoreToggle->isChecked());
        qCInfo(lcClipboard) << "忽略密码管理器内容" << (ignoreToggle->isChecked() ? "已开启" : "已关闭")
                            << QStringLiteral("但协议帧不含来源应用标识，当前不产生过滤效果");
    });
    vl->addWidget(ignore);

    auto *autoWrite = switchRow(tr("接收后自动写入本地"), s.clipboardAutoWrite(), QString(),
                                body);
    auto *autoWriteToggle = autoWrite->findChild<ToggleSwitch *>();
    connect(autoWriteToggle, &ToggleSwitch::clicked, this, [this, autoWriteToggle] {
        Settings::instance().setClipboardAutoWrite(autoWriteToggle->isChecked());
    });
    vl->addWidget(autoWrite);

    // 设计稿把三个协议常量收在一条 neutral banner 里，比逐行摆成三行说明更
    // 紧凑，而且值只有一个出处——要改就改这里，不会和代码里的判断漂开。
    vl->addWidget(banner(tr("单条上限 %1 B（协议 §5.3 分段上限），超出不同步；"
                            "5 秒内未收齐则整份丢弃；同一时刻仅允许一路传输。")
                             .arg(kClipSegLimit),
                         body, BannerVariant::Neutral));
    return new Collapsible(tr("同步设置"), QStringLiteral("settings"), body, badgeHost(), this);
}

QWidget *ClipboardPanel::badgeHost()
{
    auto *host = new QWidget(this);
    auto *hl = new QHBoxLayout(host);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addWidget(new Badge(tr("4 项"), Badge::Variant::Neutral, host));
    return host;
}

QWidget *ClipboardPanel::buildFooter()
{
    auto *foot = new QWidget(this);
    auto *hl = new QHBoxLayout(foot);
    hl->setContentsMargins(tokens::kContentPadding, 10, tokens::kContentPadding, 10);
    hl->setSpacing(tokens::kS2);
    const Palette &p = Theme::instance().palette();
    auto *icon = new QLabel(foot);
    icon->setFixedSize(tokens::kIconSm, tokens::kIconSm);
    icon->setPixmap(icons::icon(QStringLiteral("shield"), tokens::kIconSm, p.text3).pixmap(tokens::kIconSm, tokens::kIconSm));
    auto *hint = new QLabel(tr("端到端加密 · 不落盘"), foot);
    hint->setFont(uiFont(tokens::kFont11));
    hint->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
    hl->addWidget(icon);
    hl->addWidget(hint);
    hl->addStretch(1);

    auto *clear = new FgButton(tr("清空历史"), FgButton::Variant::Secondary, true, foot);
    clear->setIcon(QStringLiteral("trash"));
    connect(clear, &FgButton::clicked, this, [this] {
        clearHistory();
        toast(tr("已清空同步历史"), ToastKind::Ok);
    });
    hl->addWidget(clear);

    auto *close = new FgButton(tr("关闭"), FgButton::Variant::Ghost, true, foot);
    connect(close, &FgButton::clicked, this, &Panel::hide);
    hl->addWidget(close);
    return foot;
}

void ClipboardPanel::refreshSyncState()
{
    const Palette &p = Theme::instance().palette();
    const bool on = Settings::instance().clipboardSync();
    pillLabel_->setText(on ? tr("同步中") : tr("已关闭"));
    pillDot_->setPulsing(on);
    pillDot_->setQuality(on ? Quality::Good : Quality::Idle);
    pill_->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px")
                             .arg(cssColor(on ? p.ok.soft : p.idle.soft),
                                  QString::number(tokens::pillRadius(tokens::kStatusPillHeight))));
}

void ClipboardPanel::clearHistory()
{
    history_.clear();
    current_ = ClipEntry();
    rebuildHistory();
    Q_EMIT clearHistoryRequested();
    Q_EMIT transfersChanged();
}

void ClipboardPanel::reloadHistory()
{
    // 会话可用时，先把已经在 Go 侧重组完成、但还没被取走的剪贴板收进来。
    QString mime;
    QByteArray data;
    while (session_.takePending(&mime, &data)) {
        // 「同步图片」关闭后，入站的 image/* 直接丢弃，不进历史。settings.h
        // 把这条过滤明确划给客户端——协议帧只带 MIME，不认识内容类型。
        if (mime.startsWith(QLatin1String("image/")) && !Settings::instance().clipboardImages()) {
            qCInfo(lcClipboard) << "已关闭图片同步，丢弃入站" << mime << "条目";
            continue;
        }
        ClipEntry e;
        e.mime = mime;
        e.raw = data;
        e.text = oneLine(QString::fromUtf8(data));
        e.kind = kindOf(mime, data);
        e.source = tr("对端");
        e.agoSecs = 0;
        e.atMs = QDateTime::currentMSecsSinceEpoch();
        e.sizeBytes = data.size();
        e.fromPeer = true;   // 收到的条目才配说「来自 对端」
        e.oversize = data.size() > kClipSegLimit;
        if (e.oversize) {
            // 超限的条目协议侧已经不会送进来，这里再挡一道只是兜底；
            // 真收到就记一笔，不往历史里塞一条永远不会同步成功的假记录。
            qCInfo(lcClipboard) << "剪贴板条目超单条上限" << data.size() << QStringLiteral("B，已丢弃");
            continue;
        }
        // 历史的不变量是 history_[0] 最新：上方预览取 first()，下面的列表也按
        // 原顺序渲染（设计稿 clipboard.html 的 .list 就是最新一条在最上）。
        // 此前这里是 append——入站条目越晚到越靠后，于是「当前剪贴板」显示的是
        // 历史里最旧的那条，列表也整个倒过来；而 trimHistory 又从头删，超上限
        // 时被挤掉的恰恰是刚到的那条。
        history_.prepend(e);
        // 「接收后自动写入本地」打开时，收一条就写回本机系统剪贴板一次。
        // 真正的 QClipboard 写入由 main 接 localWriteRequested 完成。
        if (Settings::instance().clipboardAutoWrite()) {
            Q_EMIT localWriteRequested(e);
        }
    }
    trimHistory();
    rebuildHistory();
    Q_EMIT transfersChanged();
}

void ClipboardPanel::trimHistory()
{
    const int keep = Settings::instance().clipboardHistory();
    while (history_.size() > keep) {
        // 队尾是最旧的（见 reloadHistory 的不变量），要淘汰的是它。
        history_.removeLast();
    }
}

// 段数：协议 §5.3，每帧最多 255 - 7 - len(MIME) 字节（Go 侧
// MaxClipboardChunk 是同一条公式），text/plain 时每帧 238 B。
int ClipboardPanel::segmentCountOf(const ClipEntry &e) const
{
    const qint64 size = e.sizeBytes > 0 ? e.sizeBytes : e.raw.size();
    const int perFrame = 248 - e.mime.toUtf8().size();
    if (perFrame <= 0 || size <= 0) {
        return 0;
    }
    return static_cast<int>((size + perFrame - 1) / perFrame);
}

// 历史行下面那行小字。mac 端的拼法是「来源 → 本机 · 体积 · MIME」，
// 其中「来源 → 本机」只有**收到对端**的条目才有（早先这里对每一条都写，
// 本机自己复制的那条会显示成「主机名 → 本机」）；多帧的再带一句「N 段」。
QString ClipboardPanel::historyHint(const ClipEntry &e) const
{
    if (e.oversize) {
        return tr("%1 · 超出单条上限，未同步").arg(humanSize(e.sizeBytes));
    }
    QStringList parts;
    if (e.fromPeer) {
        parts << tr("%1 → 本机").arg(e.source);
    }
    parts << humanSize(e.sizeBytes > 0 ? e.sizeBytes : e.raw.size());
    parts << e.mime;
    const int frames = segmentCountOf(e);
    if (frames > 1) {
        parts << tr("%1 段").arg(frames);
    }
    return parts.join(QStringLiteral(" · "));
}

void ClipboardPanel::setPreviewText(const QString &text)
{
    if (preview_ == nullptr) {
        return;
    }
    if (text.isEmpty()) {
        preview_->setText(tr("暂无剪贴板内容"));
        return;
    }
    const QFont f = preview_->font();
    const QFontMetrics fm(f);
    // 浮板宽度是定死的，所以正文可用宽度可以直接算：520 - 内容左右 14×2
    // - 卡片左右描边 1×2 - 正文左右内边距 12×2。与 Panel::sizeHint() 同一套
    // 算法，不依赖「浮板此刻显示没有」。再让 6px 余地给字形溢出。
    const int w = tokens::kPanelWidth - 2 * tokens::kContentPadding - 2 - 2 * tokens::kS3 - 6;

    // QFontMetrics 在这一版 Qt 里没有「按宽高同时省略」的重载，只能自己排：
    // 先按词宽排版取前 N 行，剩下的丢掉并把末行缩到省略号。
    QTextLayout layout(text, f);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WordWrap);
    layout.setTextOption(opt);
    layout.beginLayout();
    QVector<QTextLine> lines;
    qreal y = 0.0;
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(w);
        line.setPosition(QPointF(0.0, y));
        y += line.height();
        lines.append(line);
        if (lines.size() > kPreviewLines) {
            break;
        }
    }
    layout.endLayout();

    if (lines.isEmpty()) {
        preview_->clear();
        return;
    }
    const bool truncated = lines.size() > kPreviewLines;
    const int keep = qMin<int>(kPreviewLines, lines.size());
    // 用原文切片而不是把各行拼起来：显式换行符本来就含在 line.textLength() 里，
    // 逐行拼接会在已经带 \n 的地方再补一个，出现空行。
    const int head = lines.first().textStart();
    const int tail = lines.at(keep - 1).textStart() + lines.at(keep - 1).textLength();
    QString out = text.mid(head, tail - head);
    if (truncated) {
        while (out.endsWith(QLatin1Char('\n'))) {
            out.chop(1);
        }
        const int nl = out.lastIndexOf(QLatin1Char('\n'));
        QString last = out.mid(nl + 1);
        const int avail = w - fm.horizontalAdvance(QChar(0x2026));
        int cut = last.size();
        while (cut > 0 && fm.horizontalAdvance(last.left(cut)) > avail) {
            --cut;
        }
        out = out.left(nl + 1) + last.left(cut) + QChar(0x2026);
    }
    preview_->setText(out);
    // 设计稿是 max-height:92px——短内容时这块就该收紧，长内容才涨到上限。
    // 这里按实际行数定高（1~3 行），否则一条几十字节的剪贴板会在预览框里
    // 留下两行空白，下面的历史列表被顶下去一截。
    const int used = out.count(QLatin1Char('\n')) + 1;
    preview_->setFixedHeight(qBound(1, used, kPreviewLines) * fm.lineSpacing());
}

void ClipboardPanel::rebuildHistory()
{
    if (rowsLayout_ == nullptr) {
        return;
    }
    clearLayout(rowsLayout_);
    const Palette &p = Theme::instance().palette();
    auto *host = rowsLayout_->parentWidget();

    // 当前预览取历史首条，也就是最新一条——Go 侧只给「刚到的那条」，没有
    // 「本机现在的剪贴板是什么」的查询口，所以这里和历史首条同源。
    current_ = history_.isEmpty() ? ClipEntry() : history_.first();
    const bool hasContent = current_.sizeBytes > 0 || !current_.raw.isEmpty();
    setPreviewText(hasContent ? current_.text : tr("暂无剪贴板内容"));
    if (mimeBadge_ != nullptr) {
        mimeBadge_->setText(hasContent ? current_.mime : QStringLiteral("--"));
    }
    if (typeIcon_ != nullptr) {
        // 类别图标跟着条目走（mac 端用 item.kind.iconName）：收到一张图还挂着
        // 文本图标，等于把图片说成文字。
        const QString iconName = current_.kind.isEmpty() ? QStringLiteral("type")
                                                          : iconOfKind(current_.kind);
        typeIcon_->setPixmap(icons::icon(iconName, tokens::kIconXs, p.text2)
                                 .pixmap(tokens::kIconXs, tokens::kIconXs));
    }
    if (fromBox_ != nullptr && fromLabel_ != nullptr) {
        const bool showFrom = current_.fromPeer && !current_.source.isEmpty();
        fromLabel_->setText(showFrom ? tr("来自 %1").arg(current_.source) : QString());
        fromBox_->setVisible(showFrom);
    }
    spec_->setText(specText(current_));
    // 没有内容时把两个动作按钮**禁用**。
    //
    // 早先它们一直是可点的，点了却在 lambda 里直接 return：既没有提示，也没
    // 有任何变化——一个点了没反应的控件。禁用比「点了没反应」诚实得多，
    // 而且按钮本身变灰，一眼就知道现在不能推。
    if (writeBtn_ != nullptr) {
        writeBtn_->setEnabled(hasContent && !current_.oversize);
    }
    if (pushBtn_ != nullptr) {
        pushBtn_->setEnabled(hasContent && !current_.oversize);
    }

    const QString kind = filter_ != nullptr ? filter_->currentData().toString()
                                            : QStringLiteral("全部");
    const QString query = search_ != nullptr ? search_->text().trimmed() : QString();
    const bool allKinds = kind.isEmpty() || kind == QStringLiteral("全部");

    QList<ClipEntry> shown;
    for (const ClipEntry &e : history_) {
        if (!allKinds && e.kind != kind) {
            continue;
        }
        if (!query.isEmpty() && !e.text.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        shown.append(e);
    }

    if (shown.isEmpty()) {
        rowsLayout_->addWidget(
            history_.isEmpty()
                ? new EmptyState(QStringLiteral("clipboard"), tr("还没有剪贴板记录"),
                                 // 这一屏现在也记**本机**复制的东西了（见
                                 // captureLocalClipboard），所以空态要照实说：
                                 // 复制一次就会出现在这里，不用等别的设备。
                                 tr("本机复制一次就会出现在这里；收到别的设备的剪贴板后也会自动记下"),
                                 host)
                : new EmptyState(QStringLiteral("search"), tr("没有匹配的条目"),
                                 tr("换个类型或关键字再试试"), host));
        return;
    }

    for (const ClipEntry &e : shown) {
        auto *trailing = new QWidget(host);
        auto *tl = new QHBoxLayout(trailing);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(tokens::kS1);
        if (e.oversize) {
            // mac 端超限那条：warn 图标 + 「超限」徽章 + 年龄，**没有**写入
            // 按钮——超限的条目既推不出去也写不回本机（载荷压根没留）。
            tl->addWidget(new Badge(tr("超限"), Badge::Variant::Warn, trailing), 0,
                          Qt::AlignVCenter);
        }
        auto *ago = new QLabel(compactDuration(ageSecsOf(e)), trailing);
        ago->setFont(uiFont(tokens::kFont11));
        ago->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
        tl->addWidget(ago, 0, Qt::AlignVCenter);
        if (!e.oversize) {
            auto *copy = new IconButton(QStringLiteral("copy"), tr("写入本地"), trailing);
            connect(copy, &IconButton::clicked, this, [this, e] {
                Q_EMIT localWriteRequested(e);
                toast(tr("已写入本地剪贴板"), ToastKind::Ok);
            });
            tl->addWidget(copy, 0, Qt::AlignVCenter);
        }

        auto *row = new ClipRow(iconOfKind(e.kind), e.text, historyHint(e), trailing, host,
                                e.oversize ? static_cast<QWidget *>(oversizeChip(host)) : nullptr);
        if (e.oversize) {
            // 点超限那一条要说清楚它为什么不能写、不能推，别让点击无声无息。
            connect(row, &ClipRow::clicked, this, [this, e] {
                toast(tr("该条目超出单条上限，未同步"), ToastKind::Danger);
            });
        } else {
            connect(row, &ClipRow::clicked, this, [this, e] { Q_EMIT localWriteRequested(e); });
        }
        rowsLayout_->addWidget(row);
    }
}

} // namespace fg
