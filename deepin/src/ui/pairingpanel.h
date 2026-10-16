// 添加设备浮板：对应 design/unified/screens/pairing.html。
// 三步向导：发现设备 → 一次性配对码 → 完成。
// 发现依赖 mDNS / BLE 传输层，尚未实现；本屏按设计稿把三步的结构与状态
// 摆出来，设备列表的数据源留成可替换接口。
#ifndef FLOWGRID_PAIRINGPANEL_H
#define FLOWGRID_PAIRINGPANEL_H

#include "ui/panel.h"

class QVBoxLayout;

namespace fg {

// 前向声明必须落在 fg 内：widgets.h 里定义的是 fg::Badge，声明在全局会
// 造出一个同名但毫无关系的 ::Badge，成员声明处直接报「Badge does not name a type」。
class Badge;

// 发现的设备条目
struct DiscoveredDevice {
    QString name;
    QString address;    // 「192.168.x.x」
    int signal = 0;     // 0..3
    int foundSecs = 0;
    bool paired = false;
};

class PairingPanel : public Panel {
    Q_OBJECT
public:
    explicit PairingPanel(QWidget *parent = nullptr);

    void setDevices(const QList<DiscoveredDevice> &devices);

Q_SIGNALS:
    void pairRequested(const QString &deviceName);
    // 页脚「完成」= 回到设备与连接。真正跳转要等传输层建立会话后才成立。
    void doneRequested();

private:
    QWidget *buildSteps();
    QWidget *buildPairCode();
    QWidget *buildDeviceList();
    QWidget *buildFooter();
    void rebuildDevices();

    QVBoxLayout *listLayout_ = nullptr;
    QVBoxLayout *stepsLayout_ = nullptr;
    Badge *signalBadge_ = nullptr;
    QList<DiscoveredDevice> devices_;
};

} // namespace fg

#endif // FLOWGRID_PAIRINGPANEL_H
