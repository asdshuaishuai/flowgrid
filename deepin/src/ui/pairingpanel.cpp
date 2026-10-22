#include "pairingpanel.h"

#include "core/designtokens.h"
#include "core/laniddiscovery.h"
#include "core/settings.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QVBoxLayout>

namespace fg {

namespace {

Q_LOGGING_CATEGORY(lcPairing, "flowgrid.pairing")

// 样式表里的颜色：全不透明走 #RRGGBB，带 alpha 走 #AARRGGBB。
// 柔和底色（surface2 / accent-soft / ok-soft）都是带 alpha 的，用 HexRgb
// 会把 alpha 压成实色——半透明的底画出来比设计稿深一档。
QString css(const QColor &c)
{
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

// mac 端 PairingView 的卡片：surface2 底 + line2 描边 + 圆角 + s3 内边距。
// 这里抽成一个函数，三张卡片共用，免得三处各写一遍样式、以后漂移。
QString cardStyle()
{
    const Palette &p = Theme::instance().palette();
    return QStringLiteral("background:%1;border:1px solid %2;border-radius:%3px")
        .arg(css(p.surface2))
        .arg(css(p.line2))
        .arg(tokens::kRadiusCard);
}

// mac 的 labeledField：11px 半粗标签（text2）+ 间距 6px + 输入框。
QWidget *labeledField(const QString &label, QLineEdit *edit, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *vl = new QVBoxLayout(box);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(6);

    auto *text = new QLabel(label, box);
    text->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    text->setStyleSheet(QStringLiteral("color:%1").arg(css(Theme::instance().palette().text2)));
    vl->addWidget(text);
    vl->addWidget(edit);
    return box;
}

} // namespace

PairingPanel::PairingPanel(QWidget *parent) : Panel(tr("添加设备"), parent)
{
    auto *body = qobject_cast<QVBoxLayout *>(content()->layout());

    // 顶部横幅：把「怎么连」一句话说清楚。mac 用 accent 档（这里对应 Info）。
    body->addWidget(
        banner(tr("在两台设备上使用相同的配对码即可建立 DirectLink 链路（TCP 24801，帧级 HMAC 校验）。"),
               this, BannerVariant::Info));

    body->addWidget(buildNearbyCard());
    body->addWidget(buildConnectCard());
    body->addWidget(buildLocalCodeCard());

    // 底部横幅：说明什么时候能自动发现、什么时候得手填地址。
    //
    // mac 原文写的是 mDNS `_flowgrid._tcp`，但 deepin 端跑的发现机制是
    // **智能连接（LAN 私有链）**：UDP 多播 239.255.42.99:24801，签名通告 +
    // 信任链（§3.1）。这里照抄 mDNS 会是句假话——本机根本没有 mDNS 浏览器，
    // 说「mDNS 自动发现」用户按着找也找不到。
    body->addWidget(
        banner(tr("同一局域网会自动互相发现（智能连接 · LAN 私有链）；跨网段请用下方地址直连。"),
               this, BannerVariant::Neutral));

    setFooterWidget(buildFooter());

    // 「附近的设备」跟发现器联动：对端列表一变就重建。
    auto &discovery = LanIdDiscovery::instance();
    connect(&discovery, &LanIdDiscovery::peersChanged, this, &PairingPanel::rebuildPeers);
    rebuildPeers();
}

// ── 卡片一：附近的设备 ──────────────────────────────────────────────────
QWidget *PairingPanel::buildNearbyCard()
{
    auto *card = makeCard();
    auto *vl = new QVBoxLayout(card);
    vl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    vl->setSpacing(tokens::kS2);

    // 标题行：左标题，右一枚说明来源的小徽章。
    auto *head = new QWidget(card);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS2);

    auto *title = new QLabel(tr("附近的设备"), head);
    title->setFont(uiFont(tokens::kFont12, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color:%1").arg(css(Theme::instance().palette().text1)));
    hl->addWidget(title);
    hl->addStretch(1);

    // mac 这里写的是「mDNS」，deepin 端走的是智能连接（LAN 私有链），
    // 徽章要如实写来源，不然用户按着「mDNS」去查防火墙规则会查错地方。
    nearbyBadge_ = new Badge(tr("智能连接"), Badge::Variant::Neutral, head);
    hl->addWidget(nearbyBadge_);

    vl->addWidget(head);

    peerLayout_ = new QVBoxLayout;
    peerLayout_->setContentsMargins(0, 0, 0, 0);
    peerLayout_->setSpacing(tokens::kS2);
    vl->addLayout(peerLayout_);

    rebuildPeers();
    return card;
}

QWidget *PairingPanel::buildPeerList()
{
    auto *box = new QWidget(this);
    auto *vl = new QVBoxLayout(box);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(tokens::kS1);

    const Palette &p = Theme::instance().palette();

    // 空态按 mac 的排法：**左对齐**的一行小字 + 一行更小的提示，
    // 不是居中的大图标空状态（那种是「列表没数据」，这里其实是
    // 「正在找」和「为什么找不到」，两回事）。
    auto *searching = new QWidget(box);
    auto *sh = new QHBoxLayout(searching);
    sh->setContentsMargins(0, 0, 0, 0);
    sh->setSpacing(tokens::kS2);

    auto *dot = new StatusDot(searching);
    dot->setPulsing(true);
    sh->addWidget(dot, 0, Qt::AlignVCenter);

    peerEmptyLabel_ = new QLabel(tr("正在搜索局域网内的 FlowGrid 设备…"), searching);
    peerEmptyLabel_->setFont(uiFont(tokens::kFont11));
    peerEmptyLabel_->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    sh->addWidget(peerEmptyLabel_);
    vl->addWidget(searching);

    peerStatusLabel_ = new QLabel(
        tr("看不到设备？请确认系统防火墙没有拦截 FlowGrid 的 UDP 多播（239.255.42.99:24801）。"),
        box);
    peerStatusLabel_->setWordWrap(true);
    peerStatusLabel_->setFont(uiFont(tokens::kFont11));
    peerStatusLabel_->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    vl->addWidget(peerStatusLabel_);

    return box;
}

void PairingPanel::rebuildPeers()
{
    if (peerLayout_ == nullptr) {
        return;
    }
    // 先把上一次摆进去的东西清掉再重摆。
    while (QLayoutItem *item = peerLayout_->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    const QList<LanIdDiscovery::Peer> peers = LanIdDiscovery::instance().peers();
    const Palette &p = Theme::instance().palette();

    if (peers.isEmpty()) {
        peerLayout_->addWidget(buildPeerList());
        return;
    }

    for (const LanIdDiscovery::Peer &peer : peers) {
        auto *row = new QWidget(this);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, tokens::kS1, 0, tokens::kS1);
        hl->setSpacing(tokens::kS2);

        auto *icon = new QLabel(row);
        icon->setFixedSize(tokens::kIconMd, tokens::kIconMd);
        icon->setPixmap(icons::icon(QStringLiteral("devices"), tokens::kIconMd, p.accent)
                            .pixmap(tokens::kIconMd, tokens::kIconMd));
        hl->addWidget(icon, 0, Qt::AlignVCenter);

        auto *texts = new QWidget(row);
        auto *tl = new QVBoxLayout(texts);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(1);

        auto *name = new QLabel(peer.name, texts);
        name->setFont(uiFont(tokens::kFont12, QFont::Medium));
        name->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));
        tl->addWidget(name);

        // 副标题：平台 + 端口。跟 mac 的 peerSubtitle 一致。
        QStringList parts;
        if (!peer.platform.isEmpty()) {
            parts << peer.platform;
        }
        if (peer.port != 0) {
            parts << tr("端口 %1").arg(peer.port);
        }
        auto *subtitle = new QLabel(parts.isEmpty() ? tr("DirectLink 设备")
                                                   : parts.join(QStringLiteral(" · ")),
                                    texts);
        subtitle->setFont(uiFont(tokens::kFont11));
        subtitle->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
        tl->addWidget(subtitle);

        hl->addWidget(texts, 1);
        hl->addStretch(1);

        auto *connect = new FgButton(tr("连接"), FgButton::Variant::Secondary, true, row);
        QObject::connect(connect, &FgButton::clicked, this, [this, peer] {
            Q_EMIT pairRequested(peer.name);
        });
        hl->addWidget(connect, 0, Qt::AlignVCenter);

        peerLayout_->addWidget(row);
    }
}

// ── 卡片二：连接到对端 ──────────────────────────────────────────────────
QWidget *PairingPanel::buildConnectCard()
{
    auto *card = makeCard();
    auto *vl = new QVBoxLayout(card);
    vl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    vl->setSpacing(tokens::kS3);

    addressEdit_ = new QLineEdit(card);
    addressEdit_->setPlaceholderText(QStringLiteral("192.168.1.50:24801"));
    applyInputStyle(addressEdit_);
    vl->addWidget(buildLabeledField(tr("设备地址"), addressEdit_));

    codeEdit_ = new QLineEdit(card);
    codeEdit_->setPlaceholderText(tr("对端「本机配对码」（缺省使用本机码）"));
    applyInputStyle(codeEdit_);
    vl->addWidget(buildLabeledField(tr("配对码"), codeEdit_));

    // 按钮行：连接（主按钮）+ 状态文字。
    //
    // mac 在已连接时会把主按钮换成「重新连接」并额外摆一个「断开」。
    // deepin 端还没有传输层，连接态根本到不了，所以这里只摆「连接」——
    // 摆一个永远不出现的「断开」不是对齐，是骗人。
    auto *row = new QWidget(card);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS2);

    auto *connect = new FgButton(tr("连接"), FgButton::Variant::Accent, false, row);
    QObject::connect(connect, &FgButton::clicked, this, [this] {
        // 记地址，不记码：配对码是凭据，明文进日志等于把它抄进日志文件。
        const QString typed = codeEdit_->text().trimmed();
        qCInfo(lcPairing) << "手动连接" << addressEdit_->text() << "尚未接通（配对码来源："
                          << (typed.isEmpty() ? QStringLiteral("本机码") : QStringLiteral("用户输入"))
                          << "）";
        toast(tr("传输层尚未落地，连接暂不可用"), ToastKind::Info);
    });
    hl->addWidget(connect);

    hl->addStretch(1);

    connectStatusLabel_ = new QLabel(tr("未连接"), row);
    connectStatusLabel_->setFont(uiFont(tokens::kFont11));
    connectStatusLabel_->setStyleSheet(
        QStringLiteral("color:%1").arg(css(Theme::instance().palette().text3)));
    hl->addWidget(connectStatusLabel_);

    vl->addWidget(row);
    return card;
}

// ── 卡片三：本机配对码 ──────────────────────────────────────────────────
QWidget *PairingPanel::buildLocalCodeCard()
{
    const Palette &p = Theme::instance().palette();
    auto *card = makeCard();
    auto *vl = new QVBoxLayout(card);
    vl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    vl->setSpacing(tokens::kS3);

    // 标题行：左标题，右一句说明这份码给谁用。
    auto *head = new QWidget(card);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS2);

    auto *title = new QLabel(tr("本机配对码"), head);
    title->setFont(uiFont(tokens::kFont12, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text1)));
    hl->addWidget(title);
    hl->addStretch(1);

    auto *hint = new QLabel(tr("对端连接本机时需输入此码"), head);
    hint->setFont(uiFont(tokens::kFont11));
    hint->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    hl->addWidget(hint);
    vl->addWidget(head);

    // 配对码槽：四个 34×42 的数字槽（设计稿 .paircode）。
    //
    // 槽里放的是**本机真正在用的那组数字**：Settings::pairingCode() 读
    // ~/.config/flowgrid/paircode，读不到就现生成一组落盘。
    //
    // 早先这里摆的是「·」占位，理由写的是「配对码要真生成得靠传输层的密钥
    // 协商，传输层还没落地」——那个因果是反的：码是**本机生成、本机保存**
    // 的（mac 端就是 Int.random 出 4 位数字存进自己的设置文件，protocol/
    // 与 PROTOCOL.md 里根本没有配对码这个字段），产生它不依赖传输层。
    // 传输层只负责后半截：对端拿这组数字去校验并建链。
    auto *codeBox = new QWidget(card);
    auto *ch = new QHBoxLayout(codeBox);
    ch->setContentsMargins(0, 0, 0, 0);
    ch->setSpacing(tokens::kS2);
    ch->addStretch(1);
    for (int i = 0; i < kPairCodeDigits; ++i) {
        auto *slot = new QLabel(codeBox);
        slot->setFixedSize(tokens::kPairSlotW, tokens::kPairSlotH);
        slot->setAlignment(Qt::AlignCenter);
        slot->setFont(uiFont(tokens::kFont20, QFont::Bold));
        slot->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px;color:%3;"
                                          "border-bottom:1px solid %4")
                                .arg(css(p.card))
                                .arg(tokens::kRadiusCtl)
                                .arg(css(p.text1))
                                .arg(css(p.line1)));
        codeSlots_ << slot;
        ch->addWidget(slot);
    }
    ch->addStretch(1);
    vl->addWidget(codeBox);

    // 按钮行：复制 + 刷新（mac 同款，都靠右）。
    auto *row = new QWidget(card);
    auto *rh = new QHBoxLayout(row);
    rh->setContentsMargins(0, 0, 0, 0);
    rh->setSpacing(tokens::kS2);

    auto *copy = new FgButton(tr("复制"), FgButton::Variant::Secondary, true, row);
    copy->setIcon(QStringLiteral("copy"));
    connect(copy, &FgButton::clicked, this, [this] {
        if (pairCode_.isEmpty()) {
            return;
        }
        if (QClipboard *clip = QApplication::clipboard()) {
            clip->setText(pairCode_);
            toast(tr("配对码已复制"), ToastKind::Ok);
        }
    });
    rh->addWidget(copy);

    auto *refresh = new FgButton(tr("刷新"), FgButton::Variant::Secondary, true, row);
    refresh->setIcon(QStringLiteral("refresh"));
    connect(refresh, &FgButton::clicked, this, [this] {
        setPairCode(Settings::instance().refreshPairingCode());
        toast(tr("配对码已刷新"), ToastKind::Ok);
    });
    rh->addWidget(refresh);
    rh->addStretch(1);
    vl->addWidget(row);

    // 把边界说清楚：码是哪来的、还差什么。
    auto *warn = new QWidget(card);
    auto *wl = new QHBoxLayout(warn);
    wl->setContentsMargins(0, tokens::kS1, 0, 0);
    wl->setSpacing(tokens::kS2);
    auto *icon = new QLabel(warn);
    icon->setFixedSize(tokens::kIconSm, tokens::kIconSm);
    icon->setPixmap(icons::icon(QStringLiteral("info"), tokens::kIconSm, p.text3)
                        .pixmap(tokens::kIconSm, tokens::kIconSm));
    auto *text = new QLabel(tr("这组数字由本机生成并保存，对端在「连接到对端」的配对码里输入它；"
                               "建立链路的传输层尚未落地"),
                            warn);
    text->setWordWrap(true);
    text->setFont(uiFont(tokens::kFont11));
    text->setStyleSheet(QStringLiteral("color:%1").arg(css(p.text3)));
    wl->addWidget(icon, 0, Qt::AlignTop);
    wl->addWidget(text, 1);
    vl->addWidget(warn);

    // 首屏就把本机正在用的那组数字填进去。
    setPairCode(Settings::instance().pairingCode());

    return card;
}

// 把一组配对码填进四个槽。位数对不上就只填能填的，剩下的留空——
// 宁可少显示一位，也不要显示一个错的数字。
void PairingPanel::setPairCode(const QString &code)
{
    pairCode_ = code;
    for (int i = 0; i < codeSlots_.size(); ++i) {
        codeSlots_.at(i)->setText(i < code.size() ? code.mid(i, 1) : QString());
    }
}

// ── 页脚 ────────────────────────────────────────────────────────────────
QWidget *PairingPanel::buildFooter()
{
    auto *foot = new QWidget(this);
    auto *hl = new QHBoxLayout(foot);
    hl->setContentsMargins(tokens::kContentPadding, 10, tokens::kContentPadding, 10);
    hl->setSpacing(tokens::kS2);
    hl->addStretch(1);

    // mac 这屏的页脚只有「关闭」——之前 DDE 版摆的是「取消 / 第 2 步 共 3 步 /
    // 完成」，那是三步向导的脚手架，向导本身已经去掉了。
    auto *close = new FgButton(tr("关闭"), FgButton::Variant::Ghost, true, foot);
    connect(close, &FgButton::clicked, this, &Panel::hide);
    hl->addWidget(close);
    return foot;
}

// ── 小工具 ──────────────────────────────────────────────────────────────
QWidget *PairingPanel::makeCard()
{
    auto *card = new QWidget(this);
    applyBoxStyle(card, cardStyle());
    return card;
}

QWidget *PairingPanel::buildLabeledField(const QString &label, QLineEdit *edit)
{
    return labeledField(label, edit, this);
}

} // namespace fg
