// 托盘快捷键：设计稿 ui.css .tray__kbd 那一组键位提示的落地。
//
// 设计稿在托盘浮层里摆着键位提示（Ctrl+1/2/3 切设备、Ctrl+K 键位映射、
// Ctrl+, 设置、Ctrl+Q 退出）。提示只在浮层开着的时候看得见，所以快捷键按
// 「应用级」注册：FlowGrid 的任一窗口是活动的就生效。浮板全收起、只剩托盘
// 图标时按不到——那时候提示也不在屏幕上，界面上不会出现按不动的键位。
//
// 要做到「任何时候都能按」需要全局热键：X11 抓键在 Wayland 下只对 XWayland
// 客户端有效，真正的路子是 org.freedesktop.portal.GlobalShortcuts（要 DBus
// 会话 + 用户在系统弹窗里二次确认），超出 UI 范围，此处不铺。
//
// 修饰键跟着设置屏的「切换热键」走（1=Ctrl / 2=Alt）。托盘浮层显示键位提示
// 用的是同一个修饰键（switchModifierName），注册的和显示的必须是同一套——
// 否则界面上写着 Alt+K、实际按 Ctrl+K 才灵。
#ifndef FLOWGRID_HOTKEYS_H
#define FLOWGRID_HOTKEYS_H

#include <QKeySequence>
#include <QList>
#include <QObject>

#include <functional>

class QShortcut;
class QWidget;

namespace fg {

class TrayHotkeys {
public:
    // 切到热键序号对应的那台设备（1..3）。
    using SwitchFunc = std::function<void(int slot)>;
    // 打开某块浮板 / 退出。
    using ActionFunc = std::function<void()>;

    // 挂在某个窗口上：Qt 的快捷键表是**按窗口**分的，QShortcut 只有挂在某个
    // 部件上，才会出现在那个窗口所属的快捷键表里；挂在 QObject（比如
    // application）上则哪里都查不到，按了没反应。
    TrayHotkeys(QWidget *window, SwitchFunc onSwitch, ActionFunc onKeymap,
                ActionFunc onSettings, ActionFunc onQuit);

    // 换挂到别的窗口。浮板是彼此独立的顶层窗口，唤起哪一块就把快捷键挪到
    // 哪一块下面——否则在 A 板里注册的快捷键，在 B 板里按不到。
    void attachTo(QWidget *window);

    // 设置屏改了「切换热键」后重新绑定键位。
    void rebind();

    // 某个键当前绑定的键位。托盘上显示的键位提示要和它对得上，自检也用它核对。
    QKeySequence keyFor(Qt::Key key) const;

private:
    struct Binding {
        QShortcut *sc = nullptr;
        Qt::Key key = Qt::Key_unknown;
    };

    QList<Binding> bindings_;
    // 当前挂在哪个窗口上。
    QWidget *host_ = nullptr;
    SwitchFunc onSwitch_;
    ActionFunc onKeymap_;
    ActionFunc onSettings_;
    ActionFunc onQuit_;
};

} // namespace fg

#endif // FLOWGRID_HOTKEYS_H
