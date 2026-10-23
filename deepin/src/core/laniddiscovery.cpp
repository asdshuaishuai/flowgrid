#include "laniddiscovery.h"

#include "input/peer_session.h"

#include "lanidlink.h"
#include "protocolapi.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QNetworkDatagram>
#include <QStandardPaths>
#include <QSysInfo>

namespace fg {

namespace {

Q_LOGGING_CATEGORY(lcLanId, "flowgrid.lanid")

// ── 通告参数（§3.1.2） ────────────────────────────────────────────────
//
// platform：协议层的 Platform 枚举（protocol/platform.go）。
//   MacOS=0x01 / Windows=0x02 / Linux=0x03 / Android=0x04 / iOS=0x05
constexpr std::uint8_t kPlatformLinux = 0x03;

// capabilities：CapabilityFlags 位掩码（protocol/platform.go）。
//   CapIntelligentLink=0x01 CapDirectLink=0x02 CapWiFiDirect=0x04
//   CapDTLS13=0x08 CapClipboard=0x10 CapFileTransfer=0x20
//
// 这里取 DirectLink|Clipboard|FileTransfer，与 mac 端 DirectLinkManager
// 起的 Announcer **完全一致**（mac 那份也没有置 CapIntelligentLink）。
// 三端通告的能力位要一样，否则对端会按能力分流，同一网段上的 mac 与
// deepin 可能互相看不见——那是协议层的事，不在 UI 这条线上自作主张改。
constexpr std::uint8_t kCapsDirectLink = 0x02;
constexpr std::uint8_t kCapsClipboard = 0x10;
constexpr std::uint8_t kCapsFileTransfer = 0x20;
constexpr std::uint8_t kCaps =
    static_cast<std::uint8_t>(kCapsDirectLink | kCapsClipboard | kCapsFileTransfer);

// 通告里报的是**会话端口**（对端据此 dial 建连接），不是 LAN ID 的组播
// UDP 端口 24801——那个端口是收发通告用的，TCP 连过去没人监听。
// 与 web demo 的做法一致（demo 通告 httpPort+20000）。
constexpr quint16 kAnnouncedPort = input::kPeerSessionPort;

// 平台字节 → 界面上的名字。跟 mac 的 Platform.displayName 同一套叫法。
QString platformName(std::uint8_t platform)
{
    switch (platform) {
    case 0x01:
        return QStringLiteral("macOS");
    case 0x02:
        return QStringLiteral("Windows");
    case 0x03:
        return QStringLiteral("Linux");
    case 0x04:
        return QStringLiteral("Android");
    case 0x05:
        return QStringLiteral("iOS");
    default:
        return QString();
    }
}

// 原始 IP 字节 → 「192.168.1.50」。
QString addressFromBytes(const QByteArray &raw)
{
    if (raw.size() == 4) {
        return QStringLiteral("%1.%2.%3.%4")
            .arg(static_cast<quint8>(raw[0]))
            .arg(static_cast<quint8>(raw[1]))
            .arg(static_cast<quint8>(raw[2]))
            .arg(static_cast<quint8>(raw[3]));
    }
    if (raw.size() == 16) {
        // IPv4 映射地址（::ffff:a.b.c.d）：Go 的 net.IP 对 IPv4 来源常以 16 字节
        // 的映射形式给出。直接按 IPv6 展开会得到
        // 「0000:0000:…:ffff:c0a8:1f90」这种没人看得懂的串——界面上要的是
        // 「192.168.31.144」。所以先认出映射前缀，再按 IPv4 渲染。
        bool mapped = true;
        for (int i = 0; i < 10 && mapped; ++i) {
            mapped = raw[i] == 0;
        }
        if (mapped && static_cast<quint8>(raw[10]) == 0xFF && static_cast<quint8>(raw[11]) == 0xFF) {
            return addressFromBytes(raw.mid(12));
        }
        QStringList parts;
        for (int i = 0; i < 16; i += 2) {
            parts << QStringLiteral("%1")
                         .arg(static_cast<quint16>((static_cast<quint8>(raw[i]) << 8)
                                                  | static_cast<quint8>(raw[i + 1])),
                              4, 16, QLatin1Char('0'));
        }
        return parts.join(QLatin1Char(':'));
    }
    return QString();
}

// 身份私钥的落盘位置：~/.config/flowgrid/identity.key。
//
// 用 XDG 目录而不是 DConfig：DConfig 的值是给「设置项」用的，私钥这种
// 二进制密钥塞进去既不好读也不好备份，而且 DConfig 的存储路径由系统决定，
// 换台机器迁移身份时找不到。XDG 目录是用户能自己拷走的地方。
QString identityKeyPath()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/flowgrid");
    return dir + QStringLiteral("/identity.key");
}

} // namespace

LanIdDiscovery &LanIdDiscovery::instance()
{
    static LanIdDiscovery inst;
    return inst;
}

LanIdDiscovery::LanIdDiscovery() = default;

LanIdDiscovery::~LanIdDiscovery()
{
    stop();
}

QByteArray LanIdDiscovery::loadIdentityKey() const
{
    QFile f(identityKeyPath());
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray key = f.readAll();
    f.close();
    return key.isEmpty() ? QByteArray() : key;
}

bool LanIdDiscovery::saveIdentityKey(const QByteArray &key) const
{
    const QString path = identityKeyPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    // 用 QSaveFile 而不是「自己写 .tmp 再 QFile::rename」：
    //
    //  1. 原子替换——写到一半掉电不会留下一个半截的密钥文件，下次启动读它会
    //     当成损坏、生成一把新身份，对端就认不出本机了。
    //  2. **能覆盖已存在的目标文件**。Qt 的 QFile::rename 不覆盖（POSIX 的
    //     rename() 覆盖，Qt 在这上面做了保护，实测 rename 到已存在文件返回
    //     false 且原文件原样不动）。原先那个写法于是只在「文件还不存在」时
    //     成功：一旦 identity.key 存在却坏了（空文件、被截断），这一轮生成
    //     的新身份存不下去，下次启动又重来一遍——本机对端永远认不出来，
    //     而日志上只有一句「未能落盘」。
    QSaveFile f(path);
    // 私钥必须是 0600：同机其它用户读到就能冒充本机发通告。
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(lcLanId) << "身份私钥写入失败" << path << f.errorString();
        return false;
    }
    if (f.write(key) != key.size()) {
        qCWarning(lcLanId) << "身份私钥写入不完整" << path << f.errorString();
        f.cancelWriting();
        return false;
    }
    if (!f.commit()) {
        qCWarning(lcLanId) << "身份私钥落盘失败" << path << f.errorString();
        return false;
    }
    return true;
}

void LanIdDiscovery::start()
{
    if (running_) {
        return;
    }
    lastError_.clear();

    // ── 身份 ──────────────────────────────────────────────────────────
    // 有存盘的身份就沿用：换了身份，对端看到的是另一台机器，信任链也断。
    const QByteArray saved = loadIdentityKey();
    if (!saved.isEmpty()) {
        identity_ = std::make_unique<proto::Identity>(saved);
        if (!identity_->isValid()) {
            qCWarning(lcLanId) << "存盘的身份私钥无法恢复，改用新身份";
            identity_.reset();
        }
    }
    if (!identity_) {
        identity_ = std::make_unique<proto::Identity>();
        if (!identity_->isValid()) {
            lastError_ = tr("无法生成身份密钥");
            qCWarning(lcLanId) << lastError_;
            return;
        }
        if (!saveIdentityKey(identity_->exportPrivateKey())) {
            // 存不下去不算致命：这一轮还能发现，只是下次启动会换身份。
            qCWarning(lcLanId) << "身份私钥未能落盘，下次启动将换一把新身份";
        }
    }

    // ── 通告器 / 监听器 / 信任链 ──────────────────────────────────────
    const QString hostName = QSysInfo::machineHostName().isEmpty()
                                 ? QStringLiteral("Deepin")
                                 : QSysInfo::machineHostName();
    announcer_ = std::make_unique<proto::Announcer>(*identity_, kPlatformLinux, kCaps,
                                                     kAnnouncedPort, hostName);
    if (!announcer_->isValid()) {
        lastError_ = tr("无法创建通告器");
        qCWarning(lcLanId) << lastError_;
        announcer_.reset();
        return;
    }

    listener_ = std::make_unique<proto::Listener>();
    if (!listener_->isValid()) {
        lastError_ = tr("无法创建监听器");
        qCWarning(lcLanId) << lastError_;
        listener_.reset();
        announcer_.reset();
        return;
    }

    trust_ = std::make_unique<proto::TrustChain>(*identity_);

    // ── 多播链路 ─────────────────────────────────────────────────────
    link_ = std::make_unique<proto::LanIDLink>(*announcer_, *listener_, identity_->publicKey());
    // LanIDLink 的 onPeersChanged 是**设置回调**的函数，不是 Qt 信号
    // （它内部是 std::function，不是 signals 段里的东西）。
    link_->setOnPeersChanged([this] { Q_EMIT peersChanged(); });
    link_->start();

    running_ = true;
    qCInfo(lcLanId) << "智能连接发现已启动" << hostName << "会话端口" << kAnnouncedPort;
}

void LanIdDiscovery::stop()
{
    if (link_) {
        link_->stop();
        link_.reset();
    }
    trust_.reset();
    announcer_.reset();
    listener_.reset();
    identity_.reset();
    running_ = false;
}

bool LanIdDiscovery::isRunning() const
{
    return running_;
}

QString LanIdDiscovery::lastError() const
{
    return lastError_;
}

QString LanIdDiscovery::selfFingerprint() const
{
    if (!identity_) {
        return QString();
    }
    return QString::fromLatin1(identity_->publicKey().toHex().left(32));
}

QList<LanIdDiscovery::Peer> LanIdDiscovery::peers() const
{
    QList<Peer> out;
    if (!link_) {
        return out;
    }
    const int total = link_->peerCount();
    for (int i = 0; i < total; ++i) {
        proto::LanIDPeer p;
        if (!link_->peer(i, &p)) {
            continue;
        }
        Peer peer;
        peer.name = p.name.isEmpty() ? tr("未知设备") : p.name;
        peer.platform = platformName(p.platform);
        peer.platformId = p.platform;
        peer.port = p.port;
        peer.fingerprint = p.fingerprint;
        peer.trusted = trust_ ? trust_->isTrusted(p.publicKey) : false;
        const QString ip = addressFromBytes(p.address);
        peer.address = ip.isEmpty() ? QString() : ip;
        if (!peer.address.isEmpty() && peer.port != 0) {
            peer.address += QStringLiteral(":%1").arg(peer.port);
        }
        out.append(peer);
    }
    return out;
}

} // namespace fg
