#include "devicemanager.h"
#include "designtokens.h"
#include "hostinfo.h"
#include "laniddiscovery.h"
#include "linkprobe.h"

#include <QDateTime>
#include <QHash>
#include <QElapsedTimer>

namespace fg {

QString linkName(Link link)
{
    switch (link) {
    case Link::IntelligentLink:
        return QStringLiteral("IntelligentLink");
    case Link::DirectLink:
        return QStringLiteral("DirectLink");
    case Link::Offline:
        break;
    }
    return QStringLiteral("离线");
}

DeviceManager &DeviceManager::instance()
{
    static DeviceManager m;
    return m;
}

DeviceManager::DeviceManager()
{
    timer_.setInterval(tokens::kLatencySampleMs);
    connect(&timer_, &QTimer::timeout, this, &DeviceManager::onSampleTick);
}

Device *DeviceManager::activeDevice()
{
    for (auto &d : devices_) {
        if (d.isActive) {
            return &d;
        }
    }
    return nullptr;
}

const Device *DeviceManager::activeDevice() const
{
    for (const auto &d : devices_) {
        if (d.isActive) {
            return &d;
        }
    }
    return nullptr;
}

Device *DeviceManager::deviceById(const QString &id)
{
    for (auto &d : devices_) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

double DeviceManager::activeLatencyMs() const
{
    const Device *d = activeDevice();
    // latencyMs < 0 表示「还没有真实读数」（没测到 / 设备离线）。
    // 这种时候必须返回 -1 而不是 0：0 会被界面显示成「0.0ms」，
    // 那是一个从没量过的数。
    if (d == nullptr || !d->online || d->latencyMs < 0.0) {
        return -1.0;
    }
    return d->latencyMs;
}

double DeviceManager::activeLatencyAvgMs() const
{
    const Device *d = activeDevice();
    if (d == nullptr || !d->online) {
        return -1.0;
    }
    const int idx = static_cast<int>(d - devices_.constData());
    if (idx < 0 || idx >= monitors_.size() || monitors_.at(idx) == nullptr) {
        return -1.0;
    }
    double minMs = 0.0;
    double maxMs = 0.0;
    double avgMs = 0.0;
    if (!monitors_.at(idx)->stats(&minMs, &maxMs, &avgMs)) {
        return -1.0;   // 窗口还没有样本
    }
    return avgMs;
}

bool DeviceManager::setActive(const QString &id)
{
    Device *target = deviceById(id);
    if (target == nullptr || !target->online) {
        return false;
    }
    for (auto &d : devices_) {
        d.isActive = (d.id == id);
    }
    Q_EMIT devicesChanged();
    return true;
}

void DeviceManager::setSource(DeviceSource *source)
{
    if (source_ == source) {
        return;
    }
    if (source_ != nullptr) {
        source_->stop();
        source_->disconnect(this);
        source_->deleteLater();
    }
    source_ = source;

    for (auto *m : monitors_) {
        delete m;
    }
    monitors_.clear();

    if (source_ != nullptr) {
        connect(source_, &DeviceSource::destroyed, this, [this, source] {
            // 只有被销毁的还是**当前**那个源时才收尾。
            //
            // 「只有当前那个」这层判断不是为了防「换源」——换源时旧源是
            // deleteLater() 延迟销毁的，它的 destroyed 会晚于新源装好才触发；
            // 之所以眼下没出事，是因为 setSource 里的 disconnect(this) 恰好先把
            // 这条连接摘掉了。留着判断是为了不让「谁销毁了源」这件事依赖调用
            // 顺序：将来谁调整那句 disconnect 的位置，这里不会跟着塌。
            if (source_ != source) {
                return;
            }
            source_ = nullptr;

            // 收尾动作**推到事件循环里**做，不在这里直接做。
            //
            // 源是 QApplication 的子对象：应用退出时它跟各块浮板一起被销毁，
            // 而销毁顺序不确定。若在这里直接 emit devicesChanged()，浮板可能
            // 已经处在「销毁到一半」的状态（子控件已经没了、对象还在），它的
            // refresh() 一跑就踩到悬空指针——实测退出时段错误（崩在 Go 运行
            // 时的信号转发里，栈上看不出是哪一行）。
            //
            // 推给事件循环之后：正常运行时下一轮就收拾干净；退出时事件循环
            // 早就停了，这段根本不会执行，谁也不会被碰到。
            QMetaObject::invokeMethod(
                this,
                [this, source] {
                    // 这期间可能已经换过源了：那时该保留新源的数据，不能再清。
                    if (source_ != nullptr) {
                        return;
                    }
                    // 源没了，列表里那些设备就是假的了。
                    //
                    // 原先只把 source_ 置空：devices_ 里那一批还留着，采样器
                    // 却已经没得可采（onSampleTick 早退）。于是界面永远显示着
                    // 一批已经不在的对端，延迟读数停在最后一帧——看着像「连不
                    // 上但也不掉线」。
                    devices_.clear();
                    for (auto *m : monitors_) {
                        delete m;
                    }
                    monitors_.clear();
                    timer_.stop();
                    Q_EMIT devicesChanged();
                },
                Qt::QueuedConnection);
        });
        devices_ = source_->discover();
        monitors_.reserve(devices_.size());
        for (int i = 0; i < devices_.size(); ++i) {
            monitors_.append(new proto::LatencyMonitor(tokens::kChartBars + 18));
        }
        source_->start();
        timer_.start();
        // 立即取一次样本：采样周期 2s，若等第一次 tick，首屏读数会是 0.0ms，
        // 与设计稿「进入即见真实延迟」不符。
        onSampleTick();
    } else {
        devices_.clear();
        timer_.stop();
    }
    Q_EMIT devicesChanged();
}

void DeviceManager::refreshDevices()
{
    if (source_ == nullptr) {
        return;
    }
    const QList<Device> fresh = source_->discover();

    // 保留上一轮的延迟历史：id 相同就是同一台机器，延迟曲线不该因为
    // 「重新发现」而断掉。
    QHash<QString, Device> previous;
    previous.reserve(devices_.size());
    for (const Device &d : devices_) {
        previous.insert(d.id, d);
    }

    QList<Device> merged;
    merged.reserve(fresh.size());
    for (const Device &d : fresh) {
        Device item = d;
        const auto it = previous.constFind(d.id);
        if (it != previous.constEnd()) {
            item.latencyMs = it->latencyMs;
            item.packetLoss = it->packetLoss;
            item.lastSeenMs = it->lastSeenMs;
        }
        merged.append(item);
    }

    // 设备集合变了没有（数量或顺序/身份）？变了才重建采样器并通知界面。
    bool changed = merged.size() != devices_.size();
    if (!changed) {
        for (int i = 0; i < merged.size(); ++i) {
            if (merged.at(i).id != devices_.at(i).id) {
                changed = true;
                break;
            }
        }
    }

    devices_ = merged;
    if (!changed) {
        return;
    }

    while (monitors_.size() > devices_.size()) {
        delete monitors_.takeLast();
    }
    while (monitors_.size() < devices_.size()) {
        monitors_.append(new proto::LatencyMonitor(tokens::kChartBars + 18));
    }
    Q_EMIT devicesChanged();
}

void DeviceManager::onSampleTick()
{
    if (source_ == nullptr) {
        return;
    }
    // 先重新发现：设备集合变了的话，采样器数量要跟着变。
    refreshDevices();
    if (monitors_.size() != devices_.size()) {
        return;
    }

    // 协议帧 Timestamp 与 LatencyMonitor 都用微秒，与 PROTOCOL.md §2 同量纲。
    bool any = false;
    for (int i = 0; i < devices_.size(); ++i) {
        Device &d = devices_[i];
        if (!d.online) {
            continue;
        }
        if (source_->sample(&d, *monitors_.at(i))) {
            any = true;
        }
    }

    if (any) {
        Q_EMIT devicesChanged();
    }
    Q_EMIT sampled();
}

} // namespace fg

// ── 本地采样源 ────────────────────────────────────────────────────────
//
// 传输层（L3：DTLS/UDP）还没落地，所以「有哪些对端」这件事现在无从谈起。
// 这一层给的是**本机**的真实身份与本机链路的真实延迟：
//   · 设备清单里只有本机一条，名字、发行版、IP 都是真的；
//   · 延迟是 ping 一次默认网关量出来的真实往返（见 linkprobe.cpp），
//     统计照旧交给 Go 的 LatencyMonitor——采样窗口、抖动、连续劣化次数
//     仍然是协议库的真实计算结果。
//
// 早先这一层给的是设计稿里的四台示例机（deepin 工作站 / Windows 11 Desktop /
// Linux Workstation / deepin Tablet）与随机摇出来的延迟（2.4 / 3.1 / 11.6ms）。
// 那些数字在用户自己的机器上一条都不成立：托盘里摆着四台他不认识的电脑，
// 「网络延迟」跳着一个从没量过的数。用户看到的是一份假清单，不是一份清单。
namespace fg {

class LocalSampleSource : public DeviceSource {
public:
    using DeviceSource::DeviceSource;

    QList<Device> discover() override
    {
        QList<Device> list;

        Device d;
        d.id = QStringLiteral("local");
        d.name = hostinfo::hostName();
        d.abbrev = abbrevOf(d.name);
        d.platformLabel = hostinfo::osLabel();
        d.address = hostinfo::primaryIPv4();
        // 设备种类图标：本机既不是笔记本也不是平板，用「显示器」这个通用形态。
        d.icon = QStringLiteral("monitor");
        d.isLocal = true;
        d.isActive = true;
        // 「在线」的意思是这台机器接在网里。没有可用网卡时标成在线是不对的——
        // 那一栏的文案是「0 台在线」，数字得对得上。
        const bool hasNet = !hostinfo::primaryIPv4().isEmpty();
        d.online = hasNet;
        // 链路类型：走的是本机这块网卡，也就是局域网直连。
        d.link = hasNet ? Link::DirectLink : Link::Offline;
        // 本机不是「快速切换」的目标——切的是对端，不是自己。
        d.hotkeyIndex = 0;
        // -1 = 还没有真实读数。0 会被界面显示成「0.0ms」，那是个假读数。
        d.latencyMs = -1.0;
        d.packetLoss = -1.0;
        list.append(d);
        return list;
    }

    bool sample(Device *device, proto::LatencyMonitor &monitor) override
    {
        // 真实往返时间：ping 一次默认网关就是一次真实往返，丢包也是真丢的。
        // 协议侧 LatencyMonitor 取的是「半 RTT」（heartbeat.go: latencyMs =
        // rttUs / 2000），所以喂进去的是完整 RTT，读数自然是单程时延。
        const qint64 rttUs = LinkProbe::instance()->lastRttUs();
        if (rttUs < 0) {
            // 还没测到（没有网关 / ping 不可用）：这一轮不产生样本。
            // 界面保持「--」，而不是拿 0 顶上去。
            return false;
        }

        // RTT 模型：ping 记在 sendUs，回包原样回显该时间戳，而 nowUs 是
        // 「收到回包的那一刻」——延迟由协议库用两者相减算出，这里不能自己算。
        const quint64 nowUs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch() * 1000LL);
        const quint64 sendUs = nowUs > static_cast<quint64>(rttUs) ? nowUs - rttUs : 1;

        monitor.recordPing(sendUs);
        double ms = 0.0;
        double jitter = 0.0;
        if (!monitor.recordPong(sendUs, nowUs, &ms, &jitter)) {
            // pong 被协议库判为不合理：本轮不算样本，原读数保留。
            return false;
        }
        device->latencyMs = ms;
        device->packetLoss = LinkProbe::instance()->lossPercent();
        device->lastSeenMs = nowUs / 1000;
        return true;
    }

private:
    // 设备卡方块里的两字标识：从主机名里取前两个字母，凑不出就用「本」。
    static QString abbrevOf(const QString &name)
    {
        QString letters;
        for (const QChar &c : name) {
            if (c.isLetterOrNumber()) {
                letters.append(c);
                if (letters.size() >= 2) {
                    break;
                }
            }
        }
        return letters.isEmpty() ? QStringLiteral("本") : letters.toUpper();
    }
};

} // namespace fg

namespace fg {

DeviceSource *makeLocalSampleSource(QObject *parent)
{
    return new LocalSampleSource(parent);
}

// ── 真实数据源：本机 + LAN 私有链发现到的对端 ──────────────────────────
//
// 传输层落地后，「有哪些对端」不再靠猜：LAN 私有链发现到的每一台机器就是
// 一条设备记录（名字、平台、地址全是对端自己通告的，不是本机编的）。
//
// 对端的**延迟没有真值**：LinkProbe 只测「本机 → 默认网关」的 ICMP 往返，
// 到具体某台对端的往返要走传输层的心跳。所以这里留 -1（界面显示「--」），
// 而不是拿网关延迟冒充对端延迟——那会让用户以为量到了，其实量的是网关。
class LanIdDeviceSource : public DeviceSource {
public:
    using DeviceSource::DeviceSource;

    QList<Device> discover() override
    {
        QList<Device> list;

        // 本机：与 LocalSampleSource 同一份真实身份信息。
        Device local;
        local.id = QStringLiteral("local");
        local.name = hostinfo::hostName();
        local.abbrev = abbrevOf(local.name);
        local.platformLabel = hostinfo::osLabel();
        local.address = hostinfo::primaryIPv4();
        local.icon = QStringLiteral("monitor");
        local.isLocal = true;
        local.isActive = true;
        const bool hasNet = !hostinfo::primaryIPv4().isEmpty();
        local.online = hasNet;
        local.link = hasNet ? Link::DirectLink : Link::Offline;
        local.hotkeyIndex = 0;
        local.latencyMs = -1.0;
        local.packetLoss = -1.0;
        list.append(local);

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const QList<LanIdDiscovery::Peer> peers = LanIdDiscovery::instance().peers();
        for (const LanIdDiscovery::Peer &peer : peers) {
            Device d;
            // 指纹当 id：主机名会变，身份公钥不会。
            d.id = peer.fingerprint.isEmpty()
                       ? QStringLiteral("peer:") + peer.address
                       : peer.fingerprint;
            d.name = peer.name;
            d.abbrev = abbrevOf(peer.name);
            d.platformLabel = peer.platform;
            d.address = peer.address;
            d.icon = QStringLiteral("laptop");
            // 能被组播发现 = 这一轮它还在发通告 = 在线。
            d.link = Link::DirectLink;
            d.isLocal = false;
            d.isActive = false;
            d.online = true;
            d.hotkeyIndex = 0;
            d.latencyMs = -1.0;    // 没有真值：界面显示「--」
            d.packetLoss = -1.0;
            d.lastSeenMs = nowMs;
            list.append(d);
        }
        return list;
    }

    bool sample(Device *device, proto::LatencyMonitor &monitor) override
    {
        // 只给本机出样本：对端的往返要走传输层心跳，量不到就不报。
        if (!device->isLocal) {
            return false;
        }
        const qint64 rttUs = LinkProbe::instance()->lastRttUs();
        if (rttUs < 0) {
            return false;
        }
        const quint64 nowUs =
            static_cast<quint64>(QDateTime::currentMSecsSinceEpoch() * 1000LL);
        const quint64 sendUs = nowUs > static_cast<quint64>(rttUs) ? nowUs - static_cast<quint64>(rttUs) : 1;
        monitor.recordPing(sendUs);
        double ms = 0.0;
        double jitter = 0.0;
        if (!monitor.recordPong(sendUs, nowUs, &ms, &jitter)) {
            return false;
        }
        device->latencyMs = ms;
        device->packetLoss = LinkProbe::instance()->lossPercent();
        device->lastSeenMs = nowUs / 1000;
        return true;
    }

private:
    static QString abbrevOf(const QString &name)
    {
        QString letters;
        for (const QChar &c : name) {
            if (c.isLetterOrNumber()) {
                letters.append(c);
                if (letters.size() >= 2) {
                    break;
                }
            }
        }
        return letters.isEmpty() ? QStringLiteral("本") : letters.left(2);
    }
};

DeviceSource *makeLanIdDeviceSource(QObject *parent)
{
    return new LanIdDeviceSource(parent);
}

} // namespace fg
