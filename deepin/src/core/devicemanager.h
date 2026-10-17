// 设备与连接管理层。
//
// 这一层只管「有哪些设备、各自什么状态、延迟多少」，不碰界面，也不碰传输
// 实现。数据源抽象成 DeviceSource：现在传输层（L3，DTLS/UDP）还没实现，
// 默认挂的是本地采样源，但它产出的延迟读数走的是真正的 Go LatencyMonitor
// （protocol/cgo），所以采样、窗口、抖动、连续劣化计数都不是假数字。
// 传输落地时只需换掉 DeviceSource，管理层与三块界面都不用动。
#ifndef FLOWGRID_DEVICE_MANAGER_H
#define FLOWGRID_DEVICE_MANAGER_H

#include "protocolapi.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

namespace fg {

// 链路命名沿用设计稿：近距离高速直连 = IntelligentLink，
// 局域网直连 = DirectLink（与 PROTOCOL.md 一致，不要改回 NearLink）。
enum class Link { IntelligentLink, DirectLink, Offline };

QString linkName(Link link);

struct Device {
    QString id;
    QString name;
    QString abbrev;          // 设备卡方块里的两字标识，设计稿里由设备自带
    QString platformLabel;   // 「macOS 15」「Windows 11」…
    // 行首类型图标名：laptop / monitor / tablet。与配对屏的 DiscoveredDevice::icon
    // 同一套取值，由数据源给出——设备种类是发现层的信息，界面从 platformLabel
    // 反推只会把「KDE Plasma 6」这类字符串猜成桌面的概率和规则写死在 UI 里。
    QString icon;
    Link link = Link::Offline;
    bool isLocal = false;
    bool isActive = false;
    bool online = false;
    int hotkeyIndex = 0;     // 1..3 快速切换位；0 表示未分配
    double latencyMs = 0.0;
    double packetLoss = 0.0;
    qint64 lastSeenMs = 0;   // 0 表示从未连上
};

// 数据源：管理层只依赖这个接口，不依赖任何具体传输。
class DeviceSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual QList<Device> discover() = 0;
    virtual void start() {}
    virtual void stop() {}

    // 本次采样是否产生了一次成功的 ping/pong 往返。
    virtual bool sample(Device *device, proto::LatencyMonitor &monitor) { Q_UNUSED(device); Q_UNUSED(monitor); return false; }
};

// 传输层落地前的数据源：延迟读数仍经 Go 的 LatencyMonitor 计算。
DeviceSource *makeLocalSampleSource(QObject *parent);

class DeviceManager : public QObject {
    Q_OBJECT
public:
    static DeviceManager &instance();

    // 接管数据源所有权。传 nullptr 表示显式进入离线演示态（设备列表为空）。
    void setSource(DeviceSource *source);

    const QList<Device> &devices() const { return devices_; }
    Device *activeDevice();
    const Device *activeDevice() const;
    Device *deviceById(const QString &id);

    // 当前控制端最近一次延迟；无设备或离线返回 -1。
    double activeLatencyMs() const;

    // 当前控制端 60 秒窗口内的均值，取自 Go 的 LatencyMonitor.Stats()；
    // 无设备 / 离线 / 窗口为空返回 -1。均值不在界面层算：界面显示的窗口
    // （12 根柱子）和监控窗口（约 30 个采样）本来就不是同一个。
    double activeLatencyAvgMs() const;

    // 把控制端切到指定设备；离线设备不可切。返回是否成功。
    bool setActive(const QString &id);

Q_SIGNALS:
    // 设备列表或任一设备状态变化后发出。界面整体重取 model 即可，
    // 单屏数据量（个位数设备）不值得做增量 diff。
    void devicesChanged();
    // 每次采样周期结束，无论是否取到新读数都会发，驱动柱状图滚动。
    void sampled();

private slots:
    void onSampleTick();

private:
    DeviceManager();

    QList<Device> devices_;
    DeviceSource *source_ = nullptr;
    QTimer timer_;
    QList<proto::LatencyMonitor *> monitors_;   // 与 devices_ 同序
};

} // namespace fg

#endif // FLOWGRID_DEVICE_MANAGER_H
