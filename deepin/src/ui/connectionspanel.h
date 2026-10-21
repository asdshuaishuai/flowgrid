// 设备与连接浮板（对齐 mac 端 ConnectionsView.swift）。
//
// 结构与 mac 同构：
//
//   头部        状态胶囊 + 「连接设备」按钮
//   接管行      仅在**有对端且已连接**时出现：接管对端键鼠开关 + 授权提示
//   设备列表    本机（带「本机」徽章）+ 对端（带延迟胶囊 + 断开按钮）
//               没有对端时补一句「连接设备后，对端会出现在这里」
//   快捷入口    键位映射 / 网络延迟 / 剪贴板 / 文件传输 四个小按钮
//   页脚        「DirectLink · 帧级 HMAC 校验」+ 设置 + 关闭
//
// 之前这一屏是照 design/unified/screens/connections.html 做的：五个可折叠区
// （键位映射 / 网络延迟 / 剪贴板同步 / 文件传输 / 设备布局）竖着堆在下面。
// 那是设计稿原型的布局，mac 实现时换成了上面这套——「设备列表 + 快捷入口」，
// 细节留给各自的浮板。
//
// 三端对齐以 mac 的实现为准，本屏按它重排：折叠区换成快捷入口，折叠区里
// 那些内容并没有丢，它们本来就各有各的浮板（延迟、剪贴板、文件传输、键位
// 映射），入口从托盘和这里的快捷入口两处都能进。
#ifndef FLOWGRID_CONNECTIONSPANEL_H
#define FLOWGRID_CONNECTIONSPANEL_H

#include "ui/panel.h"

class QLabel;
class QVBoxLayout;

namespace fg {

// 这些类定义在 widgets.h，本头只需要指针，前向声明必须落在 fg 命名空间内，
// 否则声明出的是全局同名类，与实际类型对不上。
class StatusDot;
class ToggleSwitch;

// Device 定义在 core/devicemanager.h。
struct Device;

class ConnectionsPanel : public Panel {
    Q_OBJECT
public:
    explicit ConnectionsPanel(QWidget *parent = nullptr);

    void refresh();

public Q_SLOTS:
    // 采样周期到了：刷新设备行上的延迟读数。
    void onSampled();

Q_SIGNALS:
    // 页脚「设置」按钮：切到设置浮板由 main 统一调度（浮板互斥逻辑在 main）。
    void settingsRequested();

    // 头部「连接设备」：切到配对浮板，同样交给 main 调度。
    void addRequested();

    // 快捷入口：跨屏跳到各自的浮板，统一交给 main 调度。
    void keymapRequested();
    void latencyRequested();
    void clipboardRequested();
    void filesRequested();

private:
    QWidget *buildHeader();
    QWidget *buildCaptureRow();
    QWidget *buildDeviceList();
    QWidget *buildQuickLinks();
    QWidget *buildFooter();

    void rebuildDeviceCards();

    // 当前控制的对端（在线的非本机设备）。没有对端时返回 nullptr。
    //
    // 「有没有对端」在这屏有三处要用：状态胶囊、接管行、设备列表的空态提示，
    // 各写一遍判断早晚会不一致，所以收成一个口子。
    const Device *activePeer() const;

    // 给界面用的设备副本：只有在线的对端才标成「当前控制端」。
    //
    // 模型里本机恒为 active（延迟采样以它为锚），照搬过来本机那行会是一块
    // 主色底——可在这屏里「当前控制的是本机自己」并不是要说明的事。
    Device displayDevice(const Device &d) const;

    // 状态胶囊的文案（对齐 mac ConnectionStatus.displayText）。
    QString statusText() const;

    // 接管对端键鼠：开关拨到 on 时真正去开捕获引擎。开不起来就如实回退，
    // 不留一个「显示为开、实际没接管」的开关。
    void setCaptureEnabled(bool on);

    QVBoxLayout *contentLayout_ = nullptr;
    QVBoxLayout *deviceListLayout_ = nullptr;
    QWidget *deviceList_ = nullptr;
    QWidget *captureRow_ = nullptr;
    QWidget *statusPill_ = nullptr;
    StatusDot *statusDot_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    ToggleSwitch *captureSwitch_ = nullptr;
    QLabel *captureHint_ = nullptr;
    bool captureEnabled_ = false;
};

} // namespace fg

#endif // FLOWGRID_CONNECTIONSPANEL_H
