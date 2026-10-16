#include "devicemanager.h"
#include "designtokens.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QRandomGenerator>

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
    return (d != nullptr && d->online) ? d->latencyMs : -1.0;
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
        connect(source_, &DeviceSource::destroyed, this, [this] { source_ = nullptr; });
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

void DeviceManager::onSampleTick()
{
    if (source_ == nullptr || monitors_.size() != devices_.size()) {
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
// 传输层尚未实现时的数据源。它唯一「假」的地方是网络往返本身：
// 延迟读数仍然经 Go 的 LatencyMonitor 完成记录与统计，UI 拿到的
// 采样窗口、抖动、连续劣化次数都是协议库的真实计算结果。
namespace fg {

class LocalSampleSource : public DeviceSource {
public:
    using DeviceSource::DeviceSource;

    QList<Device> discover() override
    {
        // 与设计稿 screens/connections.html 的设备清单一致，便于逐屏比对。
        QList<Device> list;
        list.append(makeDevice(QStringLiteral("local"), QStringLiteral("deepin 工作站"),
                               QStringLiteral("DP"), QStringLiteral("deepin 25"),
                               Link::IntelligentLink, true, true, 2));
        list.append(makeDevice(QStringLiteral("win"), QStringLiteral("Windows 11 Desktop"),
                               QStringLiteral("PC"), QStringLiteral("Windows 11"),
                               Link::IntelligentLink, false, true, 1));
        list.append(makeDevice(QStringLiteral("lx"), QStringLiteral("Linux Workstation"),
                               QStringLiteral("LX"), QStringLiteral("KDE Plasma 6"),
                               Link::DirectLink, false, true, 3));
        list.append(makeDevice(QStringLiteral("tablet"), QStringLiteral("deepin Tablet"),
                               QStringLiteral("TP"), QStringLiteral("deepin 25"),
                               Link::Offline, false, false, 0));
        return list;
    }

    bool sample(Device *device, proto::LatencyMonitor &monitor) override
    {
        // DirectLink 走局域网、基线更高；链路不同抖动区间不同，
        // 与设计稿里 DirectLink 11.6ms / IntelligentLink 2~3ms 的观感一致。
        const bool slow = device->link == Link::DirectLink;
        // 协议侧 LatencyMonitor 取的是「半 RTT」（heartbeat.go: latencyMs =
        // rttUs / 2000），所以这里要按 2 倍单程延迟去造往返，才能得到设计稿
        // 期望的读数（2.4 / 3.1 / 11.6ms）。
        const qint64 oneWayUs = slow ? 11600 : (device->isLocal ? 2400 : 3100);
        const qint64 baseUs = oneWayUs * 2;
        const qint64 jitterUs = QRandomGenerator::global()->bounded(-600, 1400);

        // RTT 模型：ping 记在 sendUs，pong 原样回显该时间戳，而 nowUs 是
        // 「收到 pong 的那一刻」——延迟由协议库用两者相减算出，这里不能自己算。
        const quint64 sendUs = static_cast<quint64>(
            QDateTime::currentMSecsSinceEpoch() * 1000LL);
        const quint64 nowUs = static_cast<quint64>(
            qMax<qint64>(1, static_cast<qint64>(sendUs) + baseUs + jitterUs));

        monitor.recordPing(sendUs);
        double ms = 0.0;
        double jitter = 0.0;
        if (!monitor.recordPong(sendUs, nowUs, &ms, &jitter)) {
            // pong 被协议库判为不合理：本轮不算样本，原读数保留。
            return false;
        }
        device->latencyMs = ms;
        device->packetLoss = slow ? 0.4 : 0.0;
        device->lastSeenMs = sendUs / 1000;
        return true;
    }

private:
    static Device makeDevice(const QString &id, const QString &name, const QString &abbrev,
                             const QString &platform, Link link, bool isLocal, bool online,
                             int hotkey)
    {
        Device d;
        d.id = id;
        d.name = name;
        d.abbrev = abbrev;
        d.platformLabel = platform;
        d.link = link;
        d.isLocal = isLocal;
        d.online = online;
        d.isActive = online && isLocal;
        d.hotkeyIndex = hotkey;
        return d;
    }
};

} // namespace fg

namespace fg {

DeviceSource *makeLocalSampleSource(QObject *parent)
{
    return new LocalSampleSource(parent);
}

} // namespace fg
