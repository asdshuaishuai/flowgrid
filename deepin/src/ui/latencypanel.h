// 网络延迟浮板：对应 design/unified/screens/latency.html。
//
// 大数字读数 + 近 60 秒柱状图 + 分设备列表 + 告警阈值折叠区。读数与均值
// 全部来自 Go 的 LatencyMonitor，不在本层做任何延迟计算。
#ifndef FLOWGRID_LATENCYPANEL_H
#define FLOWGRID_LATENCYPANEL_H

#include "ui/panel.h"

class QVBoxLayout;

namespace fg {

class StatusDot;
class BarChart;
class Badge;
class LatencyChip;

class LatencyPanel : public Panel {
    Q_OBJECT
public:
    explicit LatencyPanel(QWidget *parent = nullptr);

    void refresh();

public Q_SLOTS:
    void onSampled();

private:
    QWidget *buildPerDeviceList(QWidget *body);
    QWidget *buildThresholdSection();
    QWidget *buildFooter();

    // 柱状图徽章：写「近 60 秒均值」，值取自 Go 的 LatencyMonitor。
    void refreshChartBadge();

    QVBoxLayout *contentLayout_ = nullptr;
    QVBoxLayout *deviceListLayout_ = nullptr;
    // 外层装分组标题、内层装行：重建只清内层，标题留在原位。
    QVBoxLayout *deviceRows_ = nullptr;

    StatusDot *titleDot_ = nullptr;
    LatencyChip *titleLatency_ = nullptr;
    StatusDot *bigDot_ = nullptr;
    QLabel *bigValue_ = nullptr;
    QLabel *linkLabel_ = nullptr;
    // 读数右侧的链路图标：连上了是 lan，没连上是 wifi（mac 端同款）。
    QLabel *linkIcon_ = nullptr;
    // 读数底下那行小字：说明这个数**量的是谁**（传输层没落地时是本机到
    // 默认网关的真实往返，不是到某台对端的延迟）。
    QLabel *measureNote_ = nullptr;
    BarChart *chart_ = nullptr;
    Badge *chartBadge_ = nullptr;
    // 图表下面那行提示：没有设备可采样时写「连接设备后开始采样」
    // （mac 端 FGEmptyHint）。
    QWidget *chartHint_ = nullptr;
};

} // namespace fg

#endif // FLOWGRID_LATENCYPANEL_H
