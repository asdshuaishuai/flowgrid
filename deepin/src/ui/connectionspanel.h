// 设备与连接浮板：对应 design/unified/screens/connections.html。
//
// 顶栏「已连接 X / 添加设备」、设备卡列表、三个可折叠区（键位映射 / 网络延迟 /
// 剪贴板同步）。折叠区的内容里，键位映射与剪贴板两屏尚未接通，先按设计稿摆出
// 静态结构并标注；网络延迟折叠区接的是真采样。
#ifndef FLOWGRID_CONNECTIONSPANEL_H
#define FLOWGRID_CONNECTIONSPANEL_H

#include "ui/panel.h"

class QVBoxLayout;

namespace fg {

// 这些类定义在 widgets.h，本头只需要指针，前向声明必须落在 fg 命名空间内，
// 否则声明出的是全局同名类，与实际类型对不上。
class DeviceCard;
class StatusDot;
class BarChart;
class Badge;

class ConnectionsPanel : public Panel {
    Q_OBJECT
public:
    explicit ConnectionsPanel(QWidget *parent = nullptr);

    void refresh();

public Q_SLOTS:
    // 每个采样周期推一根柱、更新均值徽章；与普通刷新分开，避免重复推点。
    void onSampled();

Q_SIGNALS:
    // 页脚「设置」按钮：切到设置浮板由 main 统一调度（浮板互斥逻辑在 main）。
    void settingsRequested();

private:
    QWidget *buildDeviceList();
    QWidget *buildKeymapSection();
    QWidget *buildLatencySection();
    QWidget *buildClipboardSection();
    QWidget *buildFooter();
    void rebuildDeviceCards();

    QVBoxLayout *contentLayout_ = nullptr;
    QVBoxLayout *deviceListLayout_ = nullptr;
    QWidget *deviceList_ = nullptr;
    StatusDot *statusDot_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    BarChart *chart_ = nullptr;
    Badge *latencyBadge_ = nullptr;
};

} // namespace fg

#endif // FLOWGRID_CONNECTIONSPANEL_H
