// 设置浮板：对应 design/unified/screens/settings.html。
//
// 布局与 mac 端 SettingsView.swift 同构：内容区顶部一条「即时生效」说明横幅，
// 下面一个分段控件切四个 tab（连接 / 输入 / 剪贴板 / 安全），每个 tab 一张
// 分组卡片；安全页里另带一条警告横幅——它解释的是**本页**那个开关的后果，
// 挂在整块面板底部就变成了「不管在看哪一页都在讲安全的事」。
// 所有项都即时写 DConfig，无需重启。
#ifndef FLOWGRID_SETTINGSPANEL_H
#define FLOWGRID_SETTINGSPANEL_H

#include "ui/panel.h"

class QLineEdit;
class QStackedWidget;
class QShowEvent;

namespace fg {

// 前向声明必须落在 fg 内，否则声明出的是全局同名类。
class FgSelect;
class ToggleSwitch;
class FgButton;
class FgKeycap;
class ListRow;
class SegmentedControl;

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

protected:
    // 每次打开都回到「连接」页：mac 端每开一次就是新建一个 SettingsView，
    // @State 的 tab 默认 .connection；这边浮板是常驻实例，状态会留着。
    void showEvent(QShowEvent *event) override;

private:
    // 四个 tab 页：页里排分组卡片（安全页另带警告横幅）。
    QWidget *buildConnectionPage();
    QWidget *buildInputPage();
    QWidget *buildClipboardPage();
    QWidget *buildSecurityPage();
    // 页内的分组卡片。
    QWidget *buildConnectionGroup();
    QWidget *buildInputGroup();
    QWidget *buildSyncGroup();
    QWidget *buildSecurityGroup();
    QWidget *buildFooter();

    // 切 tab：只换可见页，不重建控件——页里已填的输入框内容不丢。
    void switchTab(int index);

    SegmentedControl *tabs_ = nullptr;
    QStackedWidget *pages_ = nullptr;

    FgSelect *preferredLink_ = nullptr;
    ToggleSwitch *autoRouting_ = nullptr;
    ToggleSwitch *autoStart_ = nullptr;
    ToggleSwitch *smoothing_ = nullptr;
    QLineEdit *edgeThreshold_ = nullptr;
    QLineEdit *switchHotkey_ = nullptr;
    FgKeycap *switchKeycap_ = nullptr;
    ToggleSwitch *clipSync_ = nullptr;
    FgSelect *clipHistory_ = nullptr;
    ToggleSwitch *confirm_ = nullptr;
};

} // namespace fg

#endif // FLOWGRID_SETTINGSPANEL_H
