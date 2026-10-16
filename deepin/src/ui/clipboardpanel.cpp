#include "clipboardpanel.h"
#include "core/designtokens.h"
#include "core/settings.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace fg {
namespace {

// 设计稿明确给出的分段上限（PROTOCOL.md §5.3）：单条超过 63,240 B 即超限，
// 历史里标记但不入库，也不推送。
constexpr int kClipSegLimit = 63240;

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

QString mimeIcon(const QString &mime)
{
    if (mime.startsWith(QLatin1String("image/"))) {
        return QStringLiteral("image");
    }
    if (mime.contains(QLatin1String("html"))) {
        return QStringLiteral("code");
    }
    if (mime.startsWith(QLatin1String("text/"))) {
        return QStringLiteral("type");
    }
    return QStringLiteral("type");
}

QString previewOf(const QByteArray &raw)
{
    QString text = QString::fromUtf8(raw);
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (text.size() > 80) {
        text = text.left(78) + QStringLiteral("…");
    }
    return text;
}

QString agoText(int secs)
{
    if (secs < 60) {
        return QStringLiteral("%1s").arg(secs);
    }
    if (secs < 3600) {
        return QStringLiteral("%1m").arg(secs / 60);
    }
    return QStringLiteral("%1h").arg(secs / 3600);
}

} // namespace

ClipboardPanel::ClipboardPanel(QWidget *parent) : Panel(tr("剪贴板"), parent)
{
    auto *body = qobject_cast<QVBoxLayout *>(content()->layout());
    auto &settings = Settings::instance();

    // 标题栏右侧：同步状态胶囊
    {
        auto *pill = new QWidget(this);
        auto *pl = new QHBoxLayout(pill);
        pl->setContentsMargins(8, 0, 8, 0);
        pl->setSpacing(6);
        auto *dot = new StatusDot(pill);
        dot->setPulsing(true);
        auto *label = new QLabel(settings.clipboardSync() ? tr("同步中") : tr("已关闭"), pill);
        label->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
        pl->addWidget(dot);
        pl->addWidget(label);
        pill->setStyleSheet(QStringLiteral("background:%1;border-radius:10px")
                                .arg(css(Theme::instance().palette().ok.soft)));
        setTitleBarWidget(pill);
    }

    body->addWidget(buildCurrent());
    body->addWidget(buildHistory());
    body->addWidget(buildSettings());
    setFooterWidget(buildFooter());

    reloadHistory();
}

QWidget *ClipboardPanel::buildCurrent()
{
    const Palette &p = Theme::instance().palette();
    auto *box = new QWidget(this);
    auto *vl = new QVBoxLayout(box);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(tokens::kS2);

    auto *title = new QLabel(tr("当前剪贴板"), box);
    title->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    title->setStyleSheet(
        QStringLiteral("color:%1;text-transform:uppercase;letter-spacing:0.4px").arg(css(p.text3)));
    vl->addWidget(title);

    auto *card = new QWidget(box);
    auto *hl = new QHBoxLayout(card);
    hl->setContentsMargins(tokens::kS3, tokens::kS2, tokens::kS3, tokens::kS2);
    hl->setSpacing(tokens::kS2);

    auto *typeIcon = new QLabel(card);
    typeIcon->setFixedSize(18, 18);
    typeIcon->setAlignment(Qt::AlignCenter);
    typeIcon->setPixmap(icons::icon(QStringLiteral("type"), 12, p.text2).pixmap(12, 12));
    typeIcon->setStyleSheet(QStringLiteral("background:%1;border-radius:4px")
                                .arg(css(p.surface3)));

    auto *col = new QVBoxLayout;
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);
    auto *mime = new QLabel(QStringLiteral("text/plain"), card);
    mime->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    auto *from = new QLabel(tr("来自 本机"), card);
    from->setFont(uiFont(tokens::kFont11));
    from->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    col->addWidget(mime);
    col->addWidget(from);

    hl->addWidget(typeIcon);
    hl->addLayout(col);
    hl->addStretch(1);

    auto *size = new QLabel(QStringLiteral("--"), card);
    size->setFont(uiFont(tokens::kFont11));
    size->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    hl->addWidget(size);

    applyBoxStyle(card,
                  QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                      .arg(css(p.line2)).arg(tokens::kRadiusCard).arg(css(p.surface)));
    vl->addWidget(card);

    auto *actions = new QWidget(box);
    auto *al = new QHBoxLayout(actions);
    al->setContentsMargins(0, 0, 0, 0);
    al->setSpacing(tokens::kS2);
    auto *write = new FgButton(tr("写入本地"), FgButton::Variant::Secondary, true, actions);
    QObject::connect(write, &FgButton::clicked, this, [this] {
        if (!history_.isEmpty()) {
            Q_EMIT localWriteRequested(history_.first());
        }
    });
    auto *push = new FgButton(tr("推送"), FgButton::Variant::Accent, true, actions);
    QObject::connect(push, &FgButton::clicked, this, [this] {
        if (history_.isEmpty()) {
            return;
        }
        const ClipEntry &e = history_.first();
        // 真实发送：交给 Go 的 ClipboardSession 分段，UI 不碰帧结构。
        session_.send(e.mime.toUtf8(), e.raw);
        Q_EMIT pushRequested(e);
    });
    al->addWidget(write);
    al->addWidget(push);
    al->addStretch(1);
    vl->addWidget(actions);

    currentLayout_ = vl;
    return box;
}

QWidget *ClipboardPanel::buildHistory()
{
    const Palette &p = Theme::instance().palette();
    auto *box = new QWidget(this);
    auto *vl = new QVBoxLayout(box);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(tokens::kS2);

    auto *title = new QLabel(tr("同步历史 · 保留最近 %1 条")
                                 .arg(Settings::instance().clipboardHistory()),
                             box);
    title->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    title->setStyleSheet(
        QStringLiteral("color:%1;text-transform:uppercase;letter-spacing:0.4px").arg(css(p.text3)));
    vl->addWidget(title);

    auto *list = new QWidget(box);
    historyLayout_ = new QVBoxLayout(list);
    historyLayout_->setContentsMargins(0, 0, 0, 0);
    historyLayout_->setSpacing(0);
    applyBoxStyle(list,
                  QStringLiteral("border:1px solid %1;border-radius:%2px;background:%3")
                      .arg(css(p.line2)).arg(tokens::kRadiusCard).arg(css(p.surface)));
    vl->addWidget(list);
    return box;
}

QWidget *ClipboardPanel::buildSettings()
{
    // 设计稿的折叠说明区：把协议层的真实边界值直接展示给用户
    const Palette &p = Theme::instance().palette();
    auto *body = new QWidget(this);
    auto *vl = new QVBoxLayout(body);
    vl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    vl->setSpacing(tokens::kS1);
    vl->addWidget(new ListRow(tr("单条上限"), tr("%1 B，超限不入库").arg(kClipSegLimit), nullptr, this));
    vl->addWidget(new ListRow(tr("重组超时"), tr("5 秒无进展丢弃整份"), nullptr, this));
    vl->addWidget(new ListRow(tr("并发"), tr("同刻仅一路传输"), nullptr, this));
    for (int i = 0; i < 3; ++i) {
        // ListRow 的 hint 是 label，补上前景色使其与其它行一致
        auto *row = vl->itemAt(i)->widget();
        for (QLabel *l : row->findChildren<QLabel *>()) {
            if (l->font().pixelSize() == tokens::kFont11) {
                l->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
            }
        }
    }
    return new Collapsible(tr("同步设置"), QStringLiteral("settings"), body, nullptr);
}

QWidget *ClipboardPanel::buildFooter()
{
    auto *foot = new QWidget(this);
    auto *hl = new QHBoxLayout(foot);
    hl->setContentsMargins(tokens::kContentPadding, 10, tokens::kContentPadding, 10);
    hl->setSpacing(tokens::kS2);
    const Palette &p = Theme::instance().palette();
    auto *icon = new QLabel(foot);
    icon->setFixedSize(14, 14);
    icon->setPixmap(icons::icon(QStringLiteral("shield"), 14, p.text3).pixmap(14, 14));
    auto *hint = new QLabel(tr("端到端加密 · 不落盘"), foot);
    hint->setFont(uiFont(tokens::kFont11));
    hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    hl->addWidget(icon);
    hl->addWidget(hint);
    hl->addStretch(1);

    auto *clear = new FgButton(tr("清空历史"), FgButton::Variant::Secondary, true, foot);
    clear->setIcon(QStringLiteral("trash"));
    QObject::connect(clear, &FgButton::clicked, this, &ClipboardPanel::clearHistory);
    hl->addWidget(clear);

    auto *close = new FgButton(tr("关闭"), FgButton::Variant::Ghost, true, foot);
    QObject::connect(close, &FgButton::clicked, this, &Panel::hide);
    hl->addWidget(close);
    return foot;
}

void ClipboardPanel::clearHistory()
{
    history_.clear();
    rebuildHistory();
    Q_EMIT clearHistoryRequested();
}

void ClipboardPanel::reloadHistory()
{
    history_.clear();
    // 会话可用时，先把已经在 Go 侧重组完成、但还没被取走的剪贴板收进来。
    QString mime;
    QByteArray data;
    while (session_.takePending(&mime, &data)) {
        ClipEntry e;
        e.mime = mime;
        e.raw = data;
        e.text = previewOf(data);
        e.source = tr("对端");
        e.agoSecs = 0;
        e.oversize = data.size() > kClipSegLimit;
        if (!e.oversize) {
            history_.append(e);
        }
    }
    rebuildHistory();
}

void ClipboardPanel::rebuildHistory()
{
    if (historyLayout_ == nullptr) {
        return;
    }
    while (historyLayout_->count() > 0) {
        QLayoutItem *item = historyLayout_->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    const Palette &p = Theme::instance().palette();
    auto *host = historyLayout_->parentWidget();

    if (history_.isEmpty()) {
        // .list 卡片在设计稿里是靠 .list__row 的内边距撑出留白的；空列表没有行，
        // 直接 addWidget 会让文字贴着卡片左边框。这里给一层上下留白 + 居中。
        auto *empty = new QLabel(tr("暂无历史条目"), host);
        empty->setFont(uiFont(tokens::kFont12));
        empty->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        empty->setAlignment(Qt::AlignCenter);
        empty->setContentsMargins(0, tokens::kS5, 0, tokens::kS5);
        historyLayout_->addWidget(empty);
        return;
    }

    for (const ClipEntry &e : history_) {
        auto *ago = new QLabel(agoText(e.agoSecs), host);
        ago->setFont(uiFont(tokens::kFont11));
        ago->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        auto *hint = new QLabel(tr("%1 · %2 B").arg(e.source).arg(e.raw.size()), host);
        hint->setFont(uiFont(tokens::kFont11));
        hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        auto *row = new NavRow(mimeIcon(e.mime), e.text, ago, host);
        QObject::connect(row, &NavRow::clicked, this,
                         [this, e] { Q_EMIT localWriteRequested(e); });
        historyLayout_->addWidget(row);
        Q_UNUSED(hint);
    }
}

} // namespace fg
