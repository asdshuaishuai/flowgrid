// 托盘浮层：300px 主入口，对应 design/unified/screens/tray.html。
//
// 分区顺序与设计稿逐条一致（当前设备 / 快速切换 / 连接 / 数据与同步 /
// 快捷开关 / 键位映射 / 设置 / 退出）。本轮只接通「设备与连接」与
// 「网络延迟」两块浮板，其余入口按设计稿摆位、点击时由 main 统一提示
// 尚未接通，而不是留一个不响的死按钮。
#ifndef FLOWGRID_TRAYPOPUP_H
#define FLOWGRID_TRAYPOPUP_H

#include "core/devicemanager.h"

#include <QWidget>

class QVBoxLayout;
class QHBoxLayout;
class QLabel;

namespace fg {

class LatencyChip;

class TrayPopup : public QWidget {
    Q_OBJECT
public:
    explicit TrayPopup(QWidget *parent = nullptr);

    void refresh();

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
    void paintEvent(QPaintEvent *) override;

private:
    QWidget *buildHead();
    QWidget *buildCurrentDevice();
    QWidget *buildQuickSwitch();
    QWidget *buildGroup(const QString &label, const QList<QWidget *> &rows);
    QWidget *buildNavRow(const QString &iconName, const QString &label, QWidget *trailing,
                         const QString &kind);
    QWidget *buildToggleRow(const QString &label, bool on);
    QWidget *buildSeparator();

    DeviceManager &manager_;
    QVBoxLayout *root_ = nullptr;
    QHBoxLayout *quickRow_ = nullptr;

    QLabel *headLatency_ = nullptr;
    LatencyChip *trayLatency_ = nullptr;

    QLabel *deviceIcon_ = nullptr;
    QLabel *deviceName_ = nullptr;
    QLabel *deviceMeta_ = nullptr;
};

} // namespace fg

#endif // FLOWGRID_TRAYPOPUP_H
