// 设置浮板：对应 design/unified/screens/settings.html。
// 四组：连接 / 输入 / 跨端同步 / 安全。所有项都即时写 DConfig，无需重启。
#ifndef FLOWGRID_SETTINGSPANEL_H
#define FLOWGRID_SETTINGSPANEL_H

#include "ui/panel.h"

class QLineEdit;

namespace fg {

// 前向声明必须落在 fg 内，否则声明出的是全局同名类。
class FgSelect;
class ToggleSwitch;
class FgButton;
class ListRow;

class SettingsPanel : public Panel {
    Q_OBJECT
public:
    explicit SettingsPanel(QWidget *parent = nullptr);

    void reload();

public Q_SLOTS:
    // 页脚「恢复默认」：把 12 个键写回 schema 里的默认值。
    void restoreDefaults();

Q_SIGNALS:
    // 输入组的「编辑」：跨屏跳转由 main 统一调度，浮板互斥逻辑只有一处。
    void keymapRequested();

private:
    QWidget *buildConnectionGroup();
    QWidget *buildInputGroup();
    QWidget *buildSyncGroup();
    QWidget *buildSecurityGroup();
    QWidget *buildFooter();

    FgSelect *preferredLink_ = nullptr;
    ToggleSwitch *autoRouting_ = nullptr;
    ToggleSwitch *autoStart_ = nullptr;
    ToggleSwitch *smoothing_ = nullptr;
    QLineEdit *edgeThreshold_ = nullptr;
    QLineEdit *switchHotkey_ = nullptr;
    ToggleSwitch *clipSync_ = nullptr;
    FgSelect *clipHistory_ = nullptr;
    ToggleSwitch *confirm_ = nullptr;
};

} // namespace fg

#endif // FLOWGRID_SETTINGSPANEL_H
