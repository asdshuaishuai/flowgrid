// 设备与连接浮板：对应 design/unified/screens/connections.html。
//
// 顶栏「已连接 X / 添加设备」、设备卡列表、五个可折叠区（键位映射 / 网络延迟 /
// 剪贴板同步 / 文件传输 / 设备布局）。网络延迟折叠区接的是真采样，文件传输区
// 读的是 FilesPanel 的只读快照；键位映射与剪贴板两区尚未接通，按设计稿摆出
// 静态结构并标注；设备布局的格子可点但不可拖（没有可持久化的屏幕布局）。
#ifndef FLOWGRID_CONNECTIONSPANEL_H
#define FLOWGRID_CONNECTIONSPANEL_H

#include "ui/panel.h"

class QVBoxLayout;

namespace fg {

// 这些类定义在 widgets.h，本头只需要指针，前向声明必须落在 fg 命名空间内，
// 否则声明出的是全局同名类，与实际类型对不上。
class ClipboardPanel;
class DeviceCard;
class FilesPanel;
class StatusDot;
class BarChart;
class Badge;

class ConnectionsPanel : public Panel {
    Q_OBJECT
public:
    explicit ConnectionsPanel(QWidget *parent = nullptr);

    void refresh();

    // 剪贴板与文件传输两个折叠区的数据源。传进来之后本屏只读它们的公开状态，
    // 不另开会话、不另存一份计数——FileSender / FileReceiver 同时只有一个活动
    // 事务，两个面板各拿一份会互抢。
    void attachPanels(ClipboardPanel *clip, FilesPanel *files);

public Q_SLOTS:
    // 每个采样周期推一根柱、更新均值徽章；与普通刷新分开，避免重复推点。
    void onSampled();
    // 两个数据源各自的变化：剪贴板只改徽章，文件传输要重建整区。
    void refreshClipboardBadge();
    void refreshClipboardFootHint();
    void rebuildFileRows();

Q_SIGNALS:
    // 页脚「设置」按钮：切到设置浮板由 main 统一调度（浮板互斥逻辑在 main）。
    void settingsRequested();

    // 设计稿里键位映射区的「编辑」、网络延迟区的「详情」、剪贴板区的入口，
    // 同样是跨屏跳转，统一交给 main 调度。
    void keymapRequested();
    void latencyRequested();
    void clipboardRequested();
    void filesRequested();

private:
    QWidget *buildFileSection();
    QWidget *buildLayoutSection();
    QWidget *buildDeviceList();
    QWidget *buildKeymapSection();
    QWidget *buildLatencySection();
    QWidget *buildClipboardSection();
    QWidget *buildFooter();
    void rebuildDeviceCards();
    void updateLatencyBadge();

    QVBoxLayout *contentLayout_ = nullptr;
    QVBoxLayout *deviceListLayout_ = nullptr;
    QWidget *deviceList_ = nullptr;
    StatusDot *statusDot_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    BarChart *chart_ = nullptr;
    Badge *latencyBadge_ = nullptr;

    // 文件传输区：正文容器 + 行容器 + 徽章，外加两个数据源。
    ClipboardPanel *clipboard_ = nullptr;
    FilesPanel *files_ = nullptr;
    Badge *clipboardBadge_ = nullptr;
    // 剪贴板折叠区页脚那句「历史保留 N 条」，N 跟着 sync/clipboard_history 走。
    QLabel *clipboardFootHint_ = nullptr;
    QWidget *fileBody_ = nullptr;
    QVBoxLayout *fileRows_ = nullptr;
    Badge *fileBadge_ = nullptr;
};

} // namespace fg

#endif // FLOWGRID_CONNECTIONSPANEL_H
