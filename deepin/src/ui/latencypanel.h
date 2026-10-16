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

    QVBoxLayout *contentLayout_ = nullptr;
    QVBoxLayout *deviceListLayout_ = nullptr;

    StatusDot *titleDot_ = nullptr;
    LatencyChip *titleLatency_ = nullptr;
    StatusDot *bigDot_ = nullptr;
    QLabel *bigValue_ = nullptr;
    QLabel *linkLabel_ = nullptr;
    BarChart *chart_ = nullptr;
    Badge *chartBadge_ = nullptr;
};

} // namespace fg

#endif // FLOWGRID_LATENCYPANEL_H
