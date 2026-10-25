#include "core/peercontroller.h"

#include "core/hostinfo.h"
#include "core/input/oscontrol.h"
#include "core/keyrules.h"
#include "core/laniddiscovery.h"
#include "core/settings.h"

#include <QHostAddress>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

Q_LOGGING_CATEGORY(lcPeer, "flowgrid.peer")

namespace fg {

namespace {

// 「ip:port」→ 主机与端口。解析不出来返回 false。
bool splitAddress(const QString &address, QString *outHost, quint16 *outPort)
{
    const int colon = address.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon + 1 >= address.size()) {
        return false;
    }
    const QString host = address.left(colon);
    bool ok = false;
    const uint port = address.mid(colon + 1).toUInt(&ok);
    if (!ok || port == 0 || port > 65535) {
        return false;
    }
    if (outHost != nullptr) {
        *outHost = host;
    }
    if (outPort != nullptr) {
        *outPort = static_cast<quint16>(port);
    }
    return true;
}

} // namespace

PeerController &PeerController::instance()
{
    static PeerController controller;
    return controller;
}

PeerController::PeerController()
{
    osControl_ = std::make_unique<input::OsControl>();
    session_ = std::make_unique<input::PeerSession>();

    connect(session_.get(), &input::PeerSession::stateChanged, this,
            &PeerController::onSessionState);
    connect(session_.get(), &input::PeerSession::frameReceived, this,
            &PeerController::onInboundFrame, Qt::QueuedConnection);
    connect(session_.get(), &input::PeerSession::inviteReceived, this,
            &PeerController::onInvite);
}

PeerController::~PeerController()
{
    stop();
}

void PeerController::start()
{
    LanIdDiscovery::instance().start();

    session_->setLocalIdentity(hostinfo::hostName(), proto::platform::kLinux);
    // 会话密钥由配对码派生：两端算不出同一把就收不到对方的帧。
    session_->setPairingCode(Settings::instance().pairingCode());

    if (!session_->listen(input::kPeerSessionPort)) {
        setError(tr("无法监听会话端口 %1").arg(input::kPeerSessionPort));
    }
}

void PeerController::stop()
{
    if (captureEnabled_) {
        setCaptureEnabled(false);
    }
    if (injectionEnabled_) {
        stopInjection();
    }
    session_->close();
}

void PeerController::connectTo(const QString &address, const QString &name,
                               std::uint8_t platform)
{
    QString host;
    quint16 port = 0;
    if (!splitAddress(address, &host, &port)) {
        setError(tr("对端地址无法解析：%1").arg(address));
        return;
    }
    peerAddress_ = address;
    peerName_ = name;
    peerPlatform_ = platform;
    initiator_ = true;
    // 重映射的方向取决于对端平台（§6.3 Ctrl⇄Meta），规则表由用户自己配。
    // 必须在开始捕获之前设：捕获一开就是按那时的对端平台换算的。
    applyKeyRules();

    session_->setLocalIdentity(hostinfo::hostName(), proto::platform::kLinux);
    session_->setPairingCode(Settings::instance().pairingCode());
    session_->connectTo(host, port);
    Q_EMIT stateChanged();
}

void PeerController::respondInvite(bool accept)
{
    session_->respondInvite(accept);
    if (!accept) {
        // 拒绝了就把这次连接的对端信息清掉：状态胶囊不该还显示着那个人。
        peerName_.clear();
        peerAddress_.clear();
        initiator_ = false;
        Q_EMIT stateChanged();
    }
}

void PeerController::disconnectPeer()
{
    if (captureEnabled_) {
        setCaptureEnabled(false);
    }
    session_->close();
    peerName_.clear();
    peerAddress_.clear();
    initiator_ = false;
    Q_EMIT stateChanged();
}

bool PeerController::setCaptureEnabled(bool on)
{
    if (!on) {
        captureEnabled_ = false;
        // 停捕获：里面含两件事——停捕获线程，以及让协议层把还按着的键松开
        // （松键的 keyUp 帧照样经发送回调送出去）。对端在另一台机器上，我们
        // 不再发松开帧的话那边的键会一直卡在按下态。
        osControl_->stopCapture();
        Q_EMIT stateChanged();
        return true;
    }

    // 三关：有对端 / 会话已通 / uinput 权限。缺哪关都说清楚。
    if (!session_->isConnected() || peerName_.isEmpty()) {
        setError(tr("未连接对端，无法接管键鼠"));
        return false;
    }

    // 捕获 → 重映射 → 组帧整条在协议层跑完，交给我们的已经是成品帧。
    // 这里只负责把它送上网线。
    QString error;
    const bool started = osControl_->startCapture(
        [this](std::uint8_t frameType, const QByteArray &payload) {
            if (QThread::currentThread() == thread()) {
                // 已经在主线程（停接管时排空队列）：直接发，不排队。
                if (session_->isConnected()) {
                    session_->sendFrame(frameType, payload);
                }
                return;
            }
            // 捕获线程 → 主线程：QTcpSocket 不是线程安全的，
            // 捕获线程直接 write 会出事，这一跳必须走事件循环队列。
            QMetaObject::invokeMethod(
                this,
                [this, frameType, payload]() {
                    if (session_->isConnected()) {
                        session_->sendFrame(frameType, payload);
                    }
                },
                Qt::QueuedConnection);
        },
        &error);
    if (!started) {
        setError(error.isEmpty() ? tr("捕获引擎启动失败") : error);
        captureEnabled_ = false;
        Q_EMIT stateChanged();
        return false;
    }
    captureEnabled_ = true;
    Q_EMIT stateChanged();
    return true;
}

bool PeerController::isConnected() const
{
    return session_->isConnected();
}

QString PeerController::statusText() const
{
    if (session_->isConnected() && !peerName_.isEmpty()) {
        return tr("已连接 %1").arg(peerName_);
    }
    if (session_->state() == input::PeerSession::State::Connecting) {
        return tr("正在连接 %1").arg(peerAddress_);
    }
    if (session_->state() == input::PeerSession::State::AwaitingAccept) {
        return tr("等待对方接受连接");
    }
    if (LanIdDiscovery::instance().isRunning()) {
        return tr("未连接 · 监听 %1").arg(input::kPeerSessionPort);
    }
    return tr("未连接");
}

void PeerController::onSessionState(input::PeerSession::State state,
                                    const QString &detail)
{
    switch (state) {
    case input::PeerSession::State::Connected:
        // 被控端（收到邀请的一方）这时开始注入对端发来的键鼠。
        if (!initiator_) {
            startInjection();
        }
        break;
    case input::PeerSession::State::Rejected:
        peerName_.clear();
        peerAddress_.clear();
        initiator_ = false;
        setError(detail);
        break;
    case input::PeerSession::State::Failed:
        if (captureEnabled_) {
            setCaptureEnabled(false);
        }
        stopInjection();
        peerName_.clear();
        peerAddress_.clear();
        initiator_ = false;
        setError(detail);
        break;
    default:
        break;
    }
    Q_EMIT stateChanged();
}

void PeerController::onInboundFrame(std::uint8_t type, const QByteArray &payload)
{
    // 解码与注入都在协议层（DecodeInput → evdev）。这里只递进去。
    //
    // 非输入帧（剪贴板、文件）会失败——那是正常情况，不是错误，不上报。
    osControl_->injectFrame(type, payload);
}

void PeerController::onInvite(const QString &name, const QString &address)
{
    peerName_ = name;
    peerAddress_ = address;
    initiator_ = false;
    Q_EMIT inviteReceived(name, address);
    Q_EMIT stateChanged();
}

bool PeerController::startInjection()
{
    if (injectionEnabled_) {
        return true;
    }
    QString error;
    if (!osControl_->startInjection(&error)) {
        setError(error.isEmpty() ? tr("注入引擎启动失败") : error);
        injectionEnabled_ = false;
        return false;
    }
    injectionEnabled_ = true;
    return true;
}

void PeerController::stopInjection()
{
    if (!injectionEnabled_) {
        return;
    }
    // 停注入会先松开本机还按着的键：uinput 设备一销毁，之前注入的 keyDown
    // 就永远没有对应的 keyUp 了，目标机上那个键会一直卡着。
    osControl_->stopInjection();
    injectionEnabled_ = false;
}

// 把当前规则表接进重映射通路。
//
// 规则表坏了/没有都不拦着连接：按平台默认换算（§6.3 Ctrl⇄⌘）照样能用，
// 但要说清楚——静默按默认跑等于用户配的映射悄悄失效。
void PeerController::applyKeyRules()
{
    KeyRules &rules = KeyRules::instance();
    // 连接时**重新读一次**规则文件：用户改了 ~/.config/flowgrid/keymap.yaml
    // 之后重连就生效；读完会通知界面刷新，屏上画的与跑起来的是同一份。
    rules.reload();
    if (!rules.loaded()) {
        qCInfo(lcPeer) << tr("键位规则表未生效：%1").arg(rules.lastError());
    } else {
        qCInfo(lcPeer) << tr("键位规则表已加载：%1（%2 条）")
                          .arg(rules.sourcePath())
                          .arg(rules.table()->ruleCount());
    }
    // 规则表的句柄是协议层的；这里传的是**同一张表**，界面显示的与实际生效
    // 的因此永远是同一份。
    osControl_->setPeer(peerPlatform_, rules.table(), QString());
}

void PeerController::setError(const QString &message)
{
    lastError_ = message;
    Q_EMIT errorRaised(message);
}

} // namespace fg
