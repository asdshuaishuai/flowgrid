// FlowGrid deepin client entry: DTK application, tray-resident main window.
#include "core_client.h"
#include "latency_panel.h"
#include "main_window.h"

#include <DTitlebar>
#include <DGuiApplicationHelper>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QMenu>
#include <QStyleFactory>
#include <QStyle>
#include <QTimer>
#include <QSystemTrayIcon>

#include <DApplication>
#include <DGuiApplicationHelper>

DWIDGET_USE_NAMESPACE

int main(int argc, char *argv[]) {
    DApplication app(argc, argv);
    app.setOrganizationName("flowgrid");
    app.setApplicationName("FlowGrid");
    app.setApplicationDisplayName("FlowGrid");
    app.setProductIcon(QIcon(":/icons/flowgrid-deepin.svg"));
    app.setApplicationVersion("1.0.0");
    app.setQuitOnLastWindowClosed(false); // tray-resident

    // In a bare session (no dde platform-theme integration) DApplication does
    // not install the DTK style/palette by itself; apply the standard DTK
    // palette for the detected theme so widgets paint consistently light/dark.
    // Deepin 25 exposes the theme as Appearance1/GlobalTheme ("bloom.dark");
    // DGuiApplicationHelper::themeType() does not read it, so probe directly
    // and follow live changes through PropertiesChanged.
    auto applyHostTheme = [theApp = &app] {
        QDBusInterface appearance("org.deepin.dde.Appearance1",
                                  "/org/deepin/dde/Appearance1",
                                  "org.freedesktop.DBus.Properties");
        QDBusReply<QString> theme = appearance.call(
            "Get", "org.deepin.dde.Appearance1", "GlobalTheme");
        const bool dark = theme.isValid() && theme.value().contains(".dark");
        const auto type = dark ? DGuiApplicationHelper::DarkType
                               : DGuiApplicationHelper::LightType;
        theApp->setPalette(DGuiApplicationHelper::instance()->standardPalette(type));
        qInfo("host GlobalTheme=%s -> %s", qPrintable(theme.isValid() ? theme.value() : QString("?")),
              dark ? "dark" : "light");
    };

    QDBusConnection::sessionBus().connect(
        "org.deepin.dde.Appearance1", "/org/deepin/dde/Appearance1",
        "org.freedesktop.DBus.Properties", "PropertiesChanged",
        applyHostTheme);
    applyHostTheme();

    CoreClient::instance().start();

    MainWindow window;
    window.show();

    // Debug hook: FLOWGRID_SNAPSHOT=/path.png saves an offscreen render of the
    // main window (QWidget::grab works regardless of occlusion/desktop).
    if (qEnvironmentVariableIsSet("FLOWGRID_SNAPSHOT")) {
        const QString path = qEnvironmentVariable("FLOWGRID_SNAPSHOT");
        QTimer::singleShot(1500, &window, [&window, path] {
            window.grab().save(path);
            qInfo("snapshot saved: %s", qPrintable(path));
        });
    }

    LatencyPanel latencyPanel;

    // dde-dock (DDE6) serves its tray area over the StatusNotifierItem
    // protocol. QSystemTrayIcon registers through the SNI watcher on X11;
    // marking the context menu and title makes dde-dock render a permanent
    // entry (ItemIsMenu) with a live latency tooltip.
    QIcon trayIcon(":/icons/flowgrid-deepin.svg");
    QSystemTrayIcon tray(trayIcon);
    auto *menu = new QMenu;
    auto *showAction = menu->addAction(QObject::tr("显示主窗口"));
    auto *latencyAction = menu->addAction(QObject::tr("延迟监控"));
    auto *captureAction = menu->addAction(QObject::tr("控制其他设备"));
    captureAction->setCheckable(true);
    captureAction->setChecked(CoreClient::instance().capturing());
    menu->addSeparator();
    auto *quitAction = menu->addAction(QObject::tr("退出 FlowGrid"));
    quitAction->setObjectName("quitAction");
    tray.setContextMenu(menu);
    tray.setToolTip("FlowGrid");
    // Title/Category/Id are exposed as SNI item properties via the platform
    // theme; setting them keeps the entry stable across dock restarts.
    tray.setProperty("Title", "FlowGrid");
    tray.setProperty("Id", "flowgrid-deepin");
    tray.setProperty("ItemIsMenu", true);
    tray.show();

    auto refreshTray = [&] {
        const QString latency = CoreClient::instance().latency();
        tray.setToolTip(QObject::tr("FlowGrid · %1").arg(latency));
        const bool connected = latency != QLatin1String("--");
        tray.setIcon(connected ? trayIcon : QIcon(":/icons/flowgrid-deepin.svg"));
    };
    QObject::connect(&CoreClient::instance(), &CoreClient::latencyChanged, refreshTray);
    refreshTray();

    QObject::connect(showAction, &QAction::triggered, &window, [&] {
        window.show();
        window.raise();
        window.activateWindow();
    });
    QObject::connect(latencyAction, &QAction::triggered, &latencyPanel,
                     [&] {
                         latencyPanel.show();
                         latencyPanel.raise();
                     });
    QObject::connect(captureAction, &QAction::toggled, &window, [&](bool checked) {
        if (!CoreClient::instance().setCapturing(checked)) {
            QSignalBlocker blocker(captureAction);
            captureAction->setChecked(!checked);
        }
    });
    QObject::connect(&CoreClient::instance(), &CoreClient::capturingChanged, captureAction,
                     [&](bool enabled) {
                         QSignalBlocker blocker(captureAction);
                         captureAction->setChecked(enabled);
                     });
    QObject::connect(quitAction, &QAction::triggered, &app, [] { std::exit(0); });
    QObject::connect(&tray, &QSystemTrayIcon::activated, &window,
                     [&](QSystemTrayIcon::ActivationReason reason) {
                         if (reason == QSystemTrayIcon::Trigger) {
                             window.isVisible() ? window.hide() : (window.show(), window.raise());
                         }
                     });

    return app.exec();
}
