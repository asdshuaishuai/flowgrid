#include "hosttheme.h"
#include "theme.h"

#include <DGuiApplicationHelper>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcHostTheme, "flowgrid.theme")

// DGuiApplicationHelper 住在 Dtk::Gui 命名空间。
DGUI_USE_NAMESPACE

namespace fg {
namespace {

constexpr auto kAppearanceService = "org.deepin.dde.Appearance1";
constexpr auto kAppearancePath = "/org/deepin/dde/Appearance1";
constexpr auto kPropertiesIface = "org.freedesktop.DBus.Properties";

} // namespace

HostTheme::HostTheme()
{
    // 设置中心切主题后由这里驱动重刷。信号参数（接口名、变更属性、字典）
    // 我们一个都用不上，所以连槽都不接收——Appearance1 只改主题，其它属性
    // 变化（字号等）会顺带触发一次无害的重新探测。
    QDBusConnection::sessionBus().connect(
        kAppearanceService, kAppearancePath, kPropertiesIface,
        "PropertiesChanged", this, SLOT(onAppearanceChanged()));

    probe();
}

void HostTheme::setForced(bool dark, bool forced)
{
    forced_ = forced;
    const bool before = dark_;
    dark_ = dark;
    if (before != dark_) {
        Theme::instance().refresh();
    }
}

void HostTheme::onAppearanceChanged()
{
    if (forced_) {
        return;
    }
    const bool before = dark_;
    probe();
    if (before != dark_) {
        Theme::instance().refresh();
    }
}

HostTheme &HostTheme::instance()
{
    static HostTheme t;
    return t;
}

void HostTheme::probe()
{
    if (forced_) {
        qCInfo(lcHostTheme) << "已强制" << (dark_ ? "dark" : "light") << "，跳过探测";
        return;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(lcHostTheme) << "会话总线不可用:" << bus.lastError().message();
    } else {
        QDBusInterface appearance(kAppearanceService, kAppearancePath, kPropertiesIface,
                                  bus);
        if (!appearance.isValid()) {
            qCWarning(lcHostTheme) << kAppearanceService << "不可用:"
                                   << appearance.lastError().message();
        } else {
            // 必须收 QDBusVariant 而不是 QString：Properties.Get 的规范签名是
            // (gv)，实测 deepin 25 返回 (<'bloom.dark'>,)。按 QString 解会被 Qt
            // 判成「签名不符」而整个丢弃，于是深色主题在真实 DDE 上静默失效、
            // 一直回退到 DTK 的 light 判定——这个坑只有在真会话里才暴露。
            QDBusReply<QDBusVariant> reply = appearance.call(
                "Get", QLatin1String(kAppearanceService), QLatin1String("GlobalTheme"));
            if (reply.isValid()) {
                globalTheme_ = reply.value().variant().toString();
                dark_ = globalTheme_.contains(QLatin1String(".dark"));
                qCInfo(lcHostTheme) << "GlobalTheme=" << globalTheme_
                                    << "->" << (dark_ ? "dark" : "light");
                return;
            }
            qCWarning(lcHostTheme) << "读 GlobalTheme 失败:" << reply.error().message();
        }
    }

    // 裸会话（无 DDE 桌面，或服务未就绪）：退回 DTK 自己的判断，界面仍然自洽。
    globalTheme_.clear();
    dark_ = DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType;
    qCInfo(lcHostTheme) << "回退 DTK themeType ->" << (dark_ ? "dark" : "light");
}

} // namespace fg
