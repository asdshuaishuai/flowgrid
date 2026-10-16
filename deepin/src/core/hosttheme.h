// 宿主主题探测：deepin 25 的真实主题名只在 DDE 的 Appearance1 服务上，
// DGuiApplicationHelper::themeType() 读不到，必须直接问 org.deepin.dde.Appearance1。
//
// 这是旧 deepin 客户端踩出来的坑（见 git d1e71e4 main.cpp 的 applyHostTheme）：
// GlobalTheme 取值形如 "bloom.dark" / "bloom"，含 ".dark" 即深色。
// 主题在设置中心里被切换时通过 PropertiesChanged 推过来，这里跟着刷新，
// 否则应用会停留在旧主题直到重启。
#ifndef FLOWGRID_HOST_THEME_H
#define FLOWGRID_HOST_THEME_H

#include <QObject>
#include <QString>

namespace fg {

class HostTheme : public QObject {
    Q_OBJECT
public:
    static HostTheme &instance();

    // 深色为 true。探测不到时按 DTK 自己报告的 themeType 兜底，
    // 保证裸会话（没有 dde 桌面环境）下界面仍然是自洽的。
    //
    // 这是全应用明暗的**唯一**判据：Theme 不再自己去问 themeType()。
    bool isDark() const { return dark_; }

    // 强制指定明暗（--theme light|dark）。用于无头截图与主题对比；
    // forced_ 为 true 时 probe() 不再访问 D-Bus，也不再理会主题切换信号。
    void setForced(bool dark, bool forced);
    bool isForced() const { return forced_; }

    // 主动重新探测一次。
    void probe();

    // 当前 GlobalTheme 原值，日志与诊断导出用；探测失败为空串。
    QString globalTheme() const { return globalTheme_; }

private Q_SLOTS:
    // 设置中心切换主题时由 D-Bus 推来。QDBusConnection::connect 在 Qt6 下
    // 只接受 const char* 槽，没有 functor 重载，所以必须是真槽而非 lambda。
    void onAppearanceChanged();

private:
    HostTheme();

    bool dark_ = false;
    bool forced_ = false;
    QString globalTheme_;
};

} // namespace fg

#endif // FLOWGRID_HOST_THEME_H
