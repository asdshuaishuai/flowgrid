// 设备与连接浮板（对齐 mac 端 ConnectionsView.swift）。
//
// 这一屏照 mac 的结构重排：头部状态胶囊 + 「连接设备」按钮 →（有对端且已连接
// 时）接管键鼠行 → 设备列表 → 快捷入口行 → 页脚。
//
// 之前这一屏是五个可折叠区（键位映射 / 网络延迟 / 剪贴板同步 / 文件传输 /
// 设备布局）竖着堆着，照的是 design/unified/screens/connections.html 那版原型。
// mac 实现时换成了现在这套「设备列表 + 快捷入口」，细节留给各自的浮板——
// 折叠区里的内容并没有丢，它们本来就各有各的浮板，入口从托盘和这里的快捷
// 入口两处都能进。
#include "connectionspanel.h"

#include "core/designtokens.h"
#include "core/devicemanager.h"
#include "core/laniddiscovery.h"
#include "core/peercontroller.h"
#include "core/theme.h"
#include "icons.h"
#include "widgets.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcConnections, "flowgrid.connections")

namespace fg {
namespace {

// 设备行的副标题（对齐 mac ConnectionsView.meta(for:)）。
//
// 本机写「本机 · <系统版本>」，在线的对端写「<系统版本> · DirectLink · 已连接」，
// 离线的对端写上次见到的时刻。
QString metaFor(const Device &d)
{
    if (d.isLocal) {
        return QObject::tr("本机 · %1").arg(d.platformLabel);
    }
    if (!d.online) {
        if (d.lastSeenMs <= 0) {
            return QObject::tr("已断开");
        }
        const qint64 secs = (QDateTime::currentMSecsSinceEpoch() - d.lastSeenMs) / 1000;
        return agoText(qMax<qint64>(0, secs));
    }
    return QObject::tr("%1 · DirectLink · 已连接").arg(d.platformLabel);
}

// 取对端的连接端点：地址 + 平台字节。
//
// 平台字节不能省：键鼠重映射要知道对端是 macOS（Cmd）还是 Windows/Linux
//（Ctrl），否则 Ctrl+C 过去就变成了 Cmd+C，对端按的键完全不对。
bool peerEndpoint(const QString &name, QString *outAddress, quint8 *outPlatform)
{
    const QList<LanIdDiscovery::Peer> peers = LanIdDiscovery::instance().peers();
    for (const LanIdDiscovery::Peer &peer : peers) {
        if (peer.name != name) {
            continue;
        }
        if (outAddress != nullptr) {
            *outAddress = peer.address;
        }
        if (outPlatform != nullptr) {
            *outPlatform = peer.platformId;
        }
        return !peer.address.isEmpty();
    }
    return false;
}

// 本机有没有接管键鼠所需的权限。
//
// mac 上这一步是「辅助功能权限」，Linux 这边对应的是 uinput：
// 捕获端要能打开 /dev/input/event*，注入端要能写 /dev/uinput。
// deepin 侧靠 /etc/udev/rules.d/99-flowgrid-input.rules 授予（见 deepin/
// install.sh），规则装完需要重新登录才生效。
bool inputCapturePermitted()
{
    // 只看「能不能写 /dev/uinput」：注入端权限是硬门槛，没它连自测都做不了。
    // 捕获端（读 event*）的规则由同一条 udev 规则一并授予，这里不重复判断，
    // 免得两处规则不同步时界面上给出互相矛盾的结论。
    return QFileInfo::exists(QStringLiteral("/dev/uinput"))
           && QFileInfo(QStringLiteral("/dev/uinput")).isWritable();
}

} // namespace

ConnectionsPanel::ConnectionsPanel(QWidget *parent) : Panel(tr("设备与连接"), parent)
{
    contentLayout_ = qobject_cast<QVBoxLayout *>(content()->layout());

    contentLayout_->addWidget(buildHeader());
    contentLayout_->addWidget(buildInviteRow());
    contentLayout_->addWidget(buildDeviceList());

    // 传输层的状态变化统一走 PeerController 的信号，界面只管刷新。
    PeerController &controller = PeerController::instance();
    connect(&controller, &PeerController::stateChanged, this, [this]() {
        captureEnabled_ = PeerController::instance().captureEnabled();
        refresh();
    });
    connect(&controller, &PeerController::inviteReceived, this,
            [this](const QString &name, const QString &address) {
                if (inviteRow_ != nullptr) {
                    inviteRow_->setVisible(true);
                }
                if (inviteLabel_ != nullptr) {
                    inviteLabel_->setText(tr("%1 请求连接本机").arg(name));
                }
                qCInfo(lcConnections) << "收到连接请求" << name << address;
                toast(tr("%1 请求连接本机，请在设备与连接里应答").arg(name), ToastKind::Info);
            });
    connect(&controller, &PeerController::errorRaised, this,
            [](const QString &message) {
                toast(message, ToastKind::Danger);
            });
    contentLayout_->addWidget(buildQuickLinks());
    setFooterWidget(buildFooter());

    // 接管行只在「有已连接的对端」时才有意义（mac 同款条件）。这一行是按需
    // 插入的：平时不占位，出现时插在头部之后。
    captureRow_ = buildCaptureRow();
    captureRow_->setVisible(false);
    contentLayout_->insertWidget(1, captureRow_);

    auto &manager = DeviceManager::instance();
    connect(&manager, &DeviceManager::devicesChanged, this, &ConnectionsPanel::refresh);
    connect(&manager, &DeviceManager::sampled, this, &ConnectionsPanel::onSampled);
    // 发现器起来了 / 起了新的对端，状态胶囊上「监听 24801」那句要跟着变。
    connect(&LanIdDiscovery::instance(), &LanIdDiscovery::peersChanged, this,
            &ConnectionsPanel::refresh);

    refresh();
}

// ── 头部：状态胶囊 + 「连接设备」 ──────────────────────────────────────
QWidget *ConnectionsPanel::buildHeader()
{
    auto *row = new QWidget(this);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS3);

    // 胶囊：20px 高、左右 8px 内边距、圆角取高的一半。
    statusPill_ = new QWidget(row);
    statusPill_->setFixedHeight(tokens::kStatusPillHeight);
    auto *pl = new QHBoxLayout(statusPill_);
    pl->setContentsMargins(tokens::kStatusPillHoriPad, 0, tokens::kStatusPillHoriPad, 0);
    pl->setSpacing(6);
    statusDot_ = new StatusDot(statusPill_);
    statusLabel_ = new QLabel(statusPill_);
    statusLabel_->setFont(uiFont(tokens::kFont11, QFont::DemiBold));
    pl->addWidget(statusDot_);
    pl->addWidget(statusLabel_);
    hl->addWidget(statusPill_);
    hl->addStretch(1);

    auto *addBtn = new FgButton(tr("连接设备"), FgButton::Variant::Accent, false, row);
    addBtn->setIcon(QStringLiteral("plus"));
    connect(addBtn, &FgButton::clicked, this, [this] { Q_EMIT addRequested(); });
    hl->addWidget(addBtn);

    return row;
}

// ── 接管对端键鼠（仅在有已连接的对端时出现） ───────────────────────────
QWidget *ConnectionsPanel::buildCaptureRow()
{
    const Palette &p = Theme::instance().palette();
    auto *card = new QWidget(this);
    auto *vl = new QVBoxLayout(card);
    vl->setContentsMargins(tokens::kS3, tokens::kS3, tokens::kS3, tokens::kS3);
    vl->setSpacing(tokens::kS2);
    // mac 端这块是 surface2 底 + 1px line2 描边 + 卡片圆角。
    applyBoxStyle(card, QStringLiteral("background:%1;border:1px solid %2;border-radius:%3px")
                                .arg(cssColor(p.surface2), cssColor(p.line2),
                                     QString::number(tokens::kRadiusCard)));

    auto *row = new QWidget(card);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS3);

    auto *textCol = new QVBoxLayout;
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(2);
    auto *title = new QLabel(tr("接管对端键鼠"), row);
    title->setFont(uiFont(tokens::kFont12, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text1)));
    captureHint_ = new QLabel(row);
    captureHint_->setFont(uiFont(tokens::kFont11));
    captureHint_->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
    textCol->addWidget(title);
    textCol->addWidget(captureHint_);
    hl->addLayout(textCol, 1);

    captureSwitch_ = new ToggleSwitch(row);
    captureSwitch_->setToolTip(tr("把本机的键鼠事件发送到对端"));
    hl->addWidget(captureSwitch_, 0, Qt::AlignVCenter);
    connect(captureSwitch_, &ToggleSwitch::toggled, this,
            [this](bool on) { setCaptureEnabled(on); });
    vl->addWidget(row);

    // 权限提示。mac 是「辅助功能权限」，Linux 这边是 uinput 权限——同形不同事，
    // 说明文字按本平台的实际做法写，不能照抄成「辅助功能」。
    auto *permRow = new QWidget(card);
    auto *permHl = new QHBoxLayout(permRow);
    permHl->setContentsMargins(0, 0, 0, 0);
    permHl->setSpacing(tokens::kS2);
    permHl->addWidget(banner(tr("需要 uinput 权限才能接管键鼠（安装 99-flowgrid-input.rules "
                                "后重新登录）。"),
                             permRow, BannerVariant::Warn),
                       1);
    auto *fixBtn = new FgButton(tr("去处理"), FgButton::Variant::Secondary, true, permRow);
    connect(fixBtn, &FgButton::clicked, this, [this] {
        // 系统里没有对应 uinput 的图形开关（它是一条 udev 规则 + 重新登录），
        // 所以这里给的是可直接照做的命令，而不是一个点开什么都没有的面板。
        qCInfo(lcConnections) << "接管键鼠缺少 uinput 权限，提示用户安装 udev 规则";
        toast(tr("请安装 99-flowgrid-input.rules 后重新登录"), ToastKind::Info);
    });
    permHl->addWidget(fixBtn, 0, Qt::AlignVCenter);
    vl->addWidget(permRow);

    return card;
}

// ── 设备列表 ───────────────────────────────────────────────────────────
QWidget *ConnectionsPanel::buildDeviceList()
{
    deviceList_ = new QWidget(this);
    deviceListLayout_ = new QVBoxLayout(deviceList_);
    deviceListLayout_->setContentsMargins(0, 0, 0, 0);
    deviceListLayout_->setSpacing(tokens::kS3);
    return deviceList_;
}

// ── 快捷入口 ───────────────────────────────────────────────────────────
QWidget *ConnectionsPanel::buildQuickLinks()
{
    auto *row = new QWidget(this);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tokens::kS2);

    struct QuickLink {
        const char *icon;
        QString text;
        void (ConnectionsPanel::*sig)();   // 不能叫 emit：那是 Qt 的宏
    };
    // 顺序与 mac 一致：键位映射 / 网络延迟 / 剪贴板 / 文件传输。
    const QuickLink links[] = {
        {"keyboard", tr("键位映射"), &ConnectionsPanel::keymapRequested},
        {"activity", tr("网络延迟"), &ConnectionsPanel::latencyRequested},
        {"clipboard", tr("剪贴板"), &ConnectionsPanel::clipboardRequested},
        {"file", tr("文件传输"), &ConnectionsPanel::filesRequested},
    };
    for (const QuickLink &link : links) {
        auto *btn = new FgButton(link.text, FgButton::Variant::Secondary, true, row);
        btn->setIcon(QString::fromLatin1(link.icon));
        connect(btn, &FgButton::clicked, this, [this, link] { Q_EMIT (this->*link.sig)(); });
        hl->addWidget(btn);
    }
    hl->addStretch(1);
    return row;
}

// ── 页脚 ───────────────────────────────────────────────────────────────
QWidget *ConnectionsPanel::buildFooter()
{
    const Palette &p = Theme::instance().palette();
    auto *foot = new QWidget(this);
    auto *hl = new QHBoxLayout(foot);
    hl->setContentsMargins(tokens::kContentPadding, 10, tokens::kContentPadding, 10);
    hl->setSpacing(tokens::kS2);

    auto *icon = new QLabel(foot);
    icon->setFixedSize(tokens::kIconSm, tokens::kIconSm);
    icon->setPixmap(
        icons::icon(QStringLiteral("shield"), tokens::kIconSm, p.text3).pixmap(tokens::kIconSm,
                                                                               tokens::kIconSm));
    auto *hint = new QLabel(tr("DirectLink · 帧级 HMAC 校验"), foot);
    hint->setFont(uiFont(tokens::kFont11));
    hint->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
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

// ── 连接邀请 ───────────────────────────────────────────────────────────
//
// 对端发起连接后，这里冒出一条「XX 请求连接本机」+ 接受/拒绝。
// 放在连接屏是因为「应答连接请求」属于连接操作；而托盘浮层是常驻的、
// 一直在眼前，那边另有 toast 提示，用户点进来就能答。
QWidget *ConnectionsPanel::buildInviteRow()
{
    inviteRow_ = new QWidget(this);
    auto *hl = new QHBoxLayout(inviteRow_);
    hl->setContentsMargins(0, tokens::kS2, 0, tokens::kS2);
    hl->setSpacing(tokens::kS2);

    inviteLabel_ = new QLabel(inviteRow_);
    inviteLabel_->setFont(uiFont(tokens::kFont11));
    hl->addWidget(inviteLabel_, 1);

    auto *accept = new FgButton(tr("接受"), FgButton::Variant::Accent, true, inviteRow_);
    connect(accept, &FgButton::clicked, this, [this]() {
        PeerController::instance().respondInvite(true);
        if (inviteRow_ != nullptr) {
            inviteRow_->setVisible(false);
        }
    });
    hl->addWidget(accept);

    auto *reject = new FgButton(tr("拒绝"), FgButton::Variant::Ghost, true, inviteRow_);
    connect(reject, &FgButton::clicked, this, [this]() {
        PeerController::instance().respondInvite(false);
        if (inviteRow_ != nullptr) {
            inviteRow_->setVisible(false);
        }
    });
    hl->addWidget(reject);

    inviteRow_->setVisible(false);
    return inviteRow_;
}

// ── 对端判定 ───────────────────────────────────────────────────────────

const Device *ConnectionsPanel::activePeer() const
{
    // 连接状态只有 PeerController 知道（传输层在那儿）：按它报的对端名字
    // 回设备列表里取那一条。不在这里自己推断「有对端就算连上了」——
    // 那会让界面显示「已连接」，而实际上一次握手都没成功。
    const QString connected = PeerController::instance().peerName();
    if (connected.isEmpty()) {
        return nullptr;
    }
    for (const Device &d : DeviceManager::instance().devices()) {
        if (!d.isLocal && d.online && d.name == connected) {
            return &d;
        }
    }
    return nullptr;
}

bool ConnectionsPanel::hasAnyPeer() const
{
    for (const Device &d : DeviceManager::instance().devices()) {
        if (!d.isLocal && d.online) {
            return true;
        }
    }
    return false;
}

QString ConnectionsPanel::statusText() const
{
    // 状态文案由 PeerController 给（它知道会话到底处于哪个阶段：拨号中 /
    // 等对方接受 / 已连接 / 被拒）。界面不自己拼——拼出来的文案与真实
    // 会话状态对不上，比没有状态更糟。
    return PeerController::instance().statusText();
}

// ── 接管键鼠 ───────────────────────────────────────────────────────────

void ConnectionsPanel::setCaptureEnabled(bool on)
{
    if (captureSwitch_ == nullptr) {
        return;
    }
    if (!on) {
        captureEnabled_ = false;
        captureSwitch_->setChecked(false);
        if (captureHint_ != nullptr) {
            captureHint_->setText(tr("键鼠仍在本机"));
        }
        return;
    }

    // 打开接管要过三关：先有已连接的对端，再有 uinput 权限，最后还得有能把它
    // 发过去的通道。任何一关不过就把开关拨回去，并把原因说清楚——留一个
    // 「显示为开、实际没接管」的开关比不做还糟。
    const Device *peer = activePeer();
    if (peer == nullptr) {
        qCInfo(lcConnections) << "接管键鼠被拒：没有已连接的对端";
        toast(tr("未连接对端，无法接管键鼠"), ToastKind::Info);
        captureSwitch_->setChecked(false);
        return;
    }
    if (!inputCapturePermitted()) {
        qCInfo(lcConnections) << "接管键鼠被拒：缺少 uinput 权限";
        toast(tr("接管键鼠需要 uinput 权限"), ToastKind::Danger);
        captureSwitch_->setChecked(false);
        return;
    }
    // 三关都过了，真正去开捕获引擎并接上转发。
    // 失败原因由 PeerController 经 errorRaised 报出来（那边已经 toast 过了），
    // 这里只负责把开关拨回去。
    if (!PeerController::instance().setCaptureEnabled(true)) {
        captureEnabled_ = false;
        captureSwitch_->setChecked(false);
        return;
    }
    captureEnabled_ = true;
    if (captureHint_ != nullptr) {
        captureHint_->setText(tr("键鼠事件正发送到对端"));
    }
}

// ── 设备卡 ─────────────────────────────────────────────────────────────

// 设备卡按「是不是当前控制端」上主色底（DeviceCard 的 active_）。
//
// 但模型里**本机**恒为 active（延迟采样、切换热键都以它为锚），照搬过去
// 本机那行就是一块蓝底——在这屏里它什么也没说明：当前控制的是本机自己。
// mac 的口径是「isCurrent = 当前被控制的那台设备」，本机不是被控端，
// 所以本机那行是普通底色，只有接上了对端、正在控制它时才上主色。
//
// 这里出一份给界面用的副本：只有在线的对端才标 active。
Device ConnectionsPanel::displayDevice(const Device &d) const
{
    Device out = d;
    out.isActive = !d.isLocal && d.online;
    return out;
}

void ConnectionsPanel::rebuildDeviceCards()
{
    if (deviceListLayout_ == nullptr) {
        return;
    }
    clearLayout(deviceListLayout_);
    const Palette &p = Theme::instance().palette();
    const QList<Device> &devices = DeviceManager::instance().devices();

    for (const Device &raw : devices) {
        const Device d = displayDevice(raw);
        auto *card = new DeviceCard(d, deviceList_);
        connect(card, &DeviceCard::switchRequested, this, [](const QString &id) {
            DeviceManager::instance().setActive(id);
        });

        // 尾部插槽（对应 mac FGDeviceRow 的 trailing 闭包）：本机、对端、
        // 离线对端三者的右侧完全不是一回事，摊开写只能散在各处各写一遍。
        auto *trailing = new QWidget(deviceList_);
        auto *tl = new QHBoxLayout(trailing);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(tokens::kS2);

        if (d.isLocal) {
            tl->addWidget(new Badge(tr("本机"), Badge::Variant::Neutral, trailing));
        } else {
            if (d.online && captureEnabled_) {
                tl->addWidget(new Badge(tr("控制中"), Badge::Variant::Ok, trailing));
            }
            auto *lat = new LatencyChip(trailing);
            lat->setLatency(d.latencyMs, d.online && d.latencyMs >= 0.0);
            tl->addWidget(lat);

            // 连接 / 断开都走传输层（PeerSession → PeerController）。
            // 按钮形态跟着真实连接状态走：没连上是「连接」，连上了是「断开」。
            const bool connected = activePeer() != nullptr && activePeer()->name == d.name;
            if (connected) {
                auto *btn = new FgButton(tr("断开"), FgButton::Variant::Ghost, true, trailing);
                connect(btn, &FgButton::clicked, this, [this, d]() {
                    PeerController::instance().disconnectPeer();
                    qCInfo(lcConnections) << "已断开对端" << d.name;
                    toast(tr("已断开 %1").arg(d.name), ToastKind::Info);
                });
                tl->addWidget(btn);
            } else if (d.online) {
                auto *btn = new FgButton(tr("连接"), FgButton::Variant::Secondary, true, trailing);
                connect(btn, &FgButton::clicked, this, [this, d]() {
                    QString address;
                    quint8 platformId = proto::platform::kLinux;
                    if (!peerEndpoint(d.name, &address, &platformId)) {
                        qCInfo(lcConnections) << "无法连接" << d.name << "：发现层没有它的端点";
                        toast(tr("无法取得 %1 的连接地址").arg(d.name), ToastKind::Danger);
                        return;
                    }
                    PeerController::instance().connectTo(address, d.name, platformId);
                    qCInfo(lcConnections) << "向对端发起连接" << d.name << address;
                });
                tl->addWidget(btn);
            }
        }
        tl->addStretch(1);
        card->setTrailingWidget(trailing);

        // 副标题走 mac 的 meta(for:) 口径，不走 DeviceCard 自带的那套
        // 「系统 · 链路 · 地址 · 当前控制端」——那一套是给设备列表用的，
        // 与 mac 这一屏的行文不一致。
        card->setMetaText(metaFor(d));

        deviceListLayout_->addWidget(card);
    }

    // 一个对端都没发现时才补空态提示（mac 的 FGEmptyHint）。
    // 已经发现了对端还说「连接设备后，对端会出现在这里」是自相矛盾。
    if (!hasAnyPeer()) {
        auto *hint = new QWidget(deviceList_);
        auto *hl = new QHBoxLayout(hint);
        hl->setContentsMargins(0, tokens::kS2, 0, tokens::kS2);
        hl->setSpacing(tokens::kS1);
        auto *icon = new QLabel(hint);
        icon->setFixedSize(tokens::kIconXs, tokens::kIconXs);
        icon->setPixmap(icons::icon(QStringLiteral("info"), tokens::kIconXs, p.text3)
                            .pixmap(tokens::kIconXs, tokens::kIconXs));
        auto *text = new QLabel(tr("连接设备后，对端会出现在这里"), hint);
        text->setFont(uiFont(tokens::kFont11));
        text->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(p.text3)));
        hl->addWidget(icon);
        hl->addWidget(text);
        hl->addStretch(1);
        deviceListLayout_->addWidget(hint);
    }
}

// ── 刷新 ───────────────────────────────────────────────────────────────

void ConnectionsPanel::refresh()
{
    const Palette &p = Theme::instance().palette();
    const Device *peer = activePeer();
    const bool connected = peer != nullptr;

    if (statusLabel_ != nullptr) {
        statusLabel_->setText(statusText());
        // 颜色跟着状态走：连上了是绿的，没连上是中性灰。mac 那边是「胶囊恒为
        // ok 底、靠圆点区分状态」，DDE 这里让胶囊跟着状态走，跟同屏其它
        // 状态色（徽章、延迟胶囊）的读法一致。
        const StatusColor sc = connected ? p.ok : p.idle;
        statusLabel_->setStyleSheet(QStringLiteral("color:%1").arg(cssColor(sc.text)));
        statusPill_->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px")
                                       .arg(cssColor(sc.soft),
                                            QString::number(tokens::pillRadius(
                                                tokens::kStatusPillHeight))));
        statusDot_->setQuality(connected ? Quality::Good : Quality::Idle);
        statusDot_->setPulsing(connected);
    }

    // 接管行：有已连接的对端才出现（mac 同款条件）。
    if (captureRow_ != nullptr) {
        captureRow_->setVisible(connected);
        if (captureHint_ != nullptr) {
            captureHint_->setText(captureEnabled_ ? tr("键鼠事件正发送到对端")
                                                   : tr("键鼠仍在本机"));
        }
    }

    rebuildDeviceCards();
}

void ConnectionsPanel::onSampled()
{
    // 采样结束后只更新延迟读数，不重建整张卡——每 2 秒重建一次列表会在
    // 事件循环里制造一批待销毁控件，那正是「残影」最容易冒出来的地方。
    if (deviceList_ == nullptr) {
        return;
    }
    for (DeviceCard *card : deviceList_->findChildren<DeviceCard *>()) {
        const Device *d = DeviceManager::instance().deviceById(card->deviceId());
        if (d != nullptr) {
            card->setDevice(displayDevice(*d));
        }
    }
}

} // namespace fg
