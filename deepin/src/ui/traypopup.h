// 托盘浮层：300px 主入口，对应 design/unified/screens/tray.html。
//
// 分区顺序与设计稿逐条一致（当前设备 / 快速切换 / 连接 / 数据与同步 /
// 快捷开关 / 键位映射 / 设置 / 退出）。本轮只接通「设备与连接」与
// 「网络延迟」两块浮板，其余入口按设计稿摆位、点击时由 main 统一提示
// 尚未接通，而不是留一个不响的死按钮。
#ifndef FLOWGRID_TRAYPOPUP_H
#define FLOWGRID_TRAYPOPUP_H

#include "core/devicemanager.h"
#include "panel.h"

#include <QWidget>

#include <functional>

class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QKeyEvent;

namespace fg {

class Badge;
class LatencyChip;
class ToggleSwitch;
class Surface;
class ShadowPlate;

class TrayPopup : public QWidget {
    Q_OBJECT
public:
    explicit TrayPopup(QWidget *parent = nullptr);

    void refresh();

    // 托盘只做索引：剪贴板条目数与传输路数由各自的浮板持有，托盘读它们的
    // 公开计数刷新徽标，不另存一份——两处各记一份必然会漂。
    void setCounts(int clipEntries, int activeTransfers);

    // 摆位与入场动画复用 panel.h 的自由函数，行为与 520px 浮板完全一致。
    void popupAt(const QPoint &anchor);

Q_SIGNALS:
    void openConnections();
    void openLatency();
    void openPairing();
    void openKeymap();
    void openSettings();
    void openClipboard();
    void openFiles();
    void quitRequested();

protected:
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    QWidget *buildHead();
    QWidget *buildCurrentDevice();
    QWidget *buildQuickSwitch();
    QWidget *buildGroup(const QString &label, const QList<QWidget *> &rows);
    QWidget *buildNavRow(const QString &iconName, const QString &label, QWidget *trailing,
                         const QString &kind);
    QWidget *buildToggleRow(const QString &label, bool on,
                            const std::function<void(bool)> &onWrite);
    QWidget *buildSeparator();
    void syncShadowPlate();
    void restyle();

    DeviceManager &manager_;
    ShellMargin margin_;
    ShellMargin baseMargin_;   // 未按任务栏方向调整前的基准余白
    QVBoxLayout *root_ = nullptr;
    QVBoxLayout *cardLayout_ = nullptr;
    Surface *card_ = nullptr;
    ShadowPlate *plate_ = nullptr;
    QHBoxLayout *quickRow_ = nullptr;

    QLabel *headLatency_ = nullptr;
    LatencyChip *trayLatency_ = nullptr;

    QLabel *deviceIcon_ = nullptr;
    QLabel *deviceName_ = nullptr;
    QLabel *deviceMeta_ = nullptr;
    // 「设备与连接 / 剪贴板 / 文件传输」右侧的徽标，分别跟随 DeviceManager 与
    // 对应浮板实时更新，不写死占位数字。
    Badge *deviceCount_ = nullptr;
    Badge *clipboardBadge_ = nullptr;
    Badge *filesBadge_ = nullptr;

    // 快捷开关：refresh() 拿它们跟随 Settings::changed（「恢复默认」改了配置后，
    // 托盘这几个开关要跟着回到默认值，否则两处状态会不一致）。
    QList<ToggleSwitch *> quickToggles_;
};

} // namespace fg

#endif // FLOWGRID_TRAYPOPUP_H
