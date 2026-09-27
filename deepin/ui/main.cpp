// FlowGrid deepin client entry: DTK application, tray-resident main window
// with the rich tray popup (design/mac/tray.html) and SNI dock integration.
#include "add_device_dialog.h"
#include "core_client.h"
#include "latency_panel.h"
#include "main_window.h"
#include "tray_popup.h"

#include <DApplication>
#include <DTitlebar>
#include <daboutdialog.h>
#include <LogManager.h>

#include <DGuiApplicationHelper>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QGuiApplication>
#include <QMenu>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleFactory>
#include <QSystemTrayIcon>
#include <QTimer>
#include <memory>

DWIDGET_USE_NAMESPACE

int main(int argc, char *argv[]) {
    DApplication app(argc, argv);
    app.setOrganizationName("flowgrid");
    // Per dtk-development: applicationName should match the executable so
    // dde-application-manager maps the process to our desktop file identity.
    app.setApplicationName("flowgrid-deepin");
    app.setApplicationDisplayName("FlowGrid");
    DTK_CORE_NAMESPACE::DLogManager::registerConsoleAppender();
    DTK_CORE_NAMESPACE::DLogManager::registerFileAppender();

    // Single instance: the first process owns org.flowgrid.DDE; later launches
    // ask it to show the main window and exit.
    if (!QDBusConnection::sessionBus().registerService("org.flowgrid.DDE")) {
        QDBusInterface service("org.flowgrid.DDE", "/org/flowgrid/DDE",
                               "org.flowgrid.DDE");
        service.call("requestShowMainWindow");
        qInfo("FlowGrid already running; activated the existing window");
        return 0;
    }
    QDBusConnection::sessionBus().registerObject(
        "/org/flowgrid/DDE", &CoreClient::instance(),
        QDBusConnection::ExportAllContents);
    app.setProductIcon(QIcon(":/icons/flowgrid-deepin.svg"));
    app.setApplicationVersion("1.0.0");
    app.setQuitOnLastWindowClosed(false); // tray-resident

    // Follow the host theme: deepin 25 stores it in Appearance1/GlobalTheme
    // ("bloom.dark"), which DGuiApplicationHelper::themeType() misses here.
    QObject::connect(
        &CoreClient::instance(), &CoreClient::hostThemeChanged, &app,
        [](bool dark) {
            const auto type = dark ? DGuiApplicationHelper::DarkType
                                   : DGuiApplicationHelper::LightType;
            QGuiApplication::setPalette(
                DGuiApplicationHelper::instance()->standardPalette(type));
        });
    CoreClient::instance().applyHostTheme();
    CoreClient::instance().start();

    MainWindow window;
    window.show();

    // Debug hooks: FLOWGRID_SNAPSHOT=<file> saves the main window render;
    // FLOWGRID_SNAPSHOT_ALL=<dir> additionally shoots latency + tray pages.
    if (qEnvironmentVariableIsSet("FLOWGRID_SNAPSHOT")) {
        const QString path = qEnvironmentVariable("FLOWGRID_SNAPSHOT");
        QTimer::singleShot(1500, &window, [&window, path] {
            window.grab().save(path);
            qInfo("snapshot saved: %s", qPrintable(path));
        });
    }

    LatencyPanel latencyPanel;
    TrayPopup *trayPopupPanel = new TrayPopup;
    latencyPanel.setWindowTitle(QStringLiteral("延迟监控"));

    if (qEnvironmentVariableIsSet("FLOWGRID_SNAPSHOT_ALL")) {
        const QString dir = qEnvironmentVariable("FLOWGRID_SNAPSHOT_ALL");
        QTimer *shootTimer = new QTimer(&app);
        shootTimer->setSingleShot(false);
        shootTimer->setInterval(1200);
        // the lambda outlives this block: keep the counter on the heap
        auto phase = std::make_shared<int>(0);
        QObject::connect(shootTimer, &QTimer::timeout, &app, [shootTimer, phase, &latencyPanel, trayPopupPanel, dir] {
            qInfo("SNAPSHOT_ALL phase %d", *phase);
            if (*phase == 0) {
                latencyPanel.show();
                latencyPanel.raise();
            } else if (*phase == 1) {
                latencyPanel.grab().save(dir + "/shot_latency.png");
                latencyPanel.hide();
                trayPopupPanel->popup();
            } else if (*phase == 2) {
                trayPopupPanel->grab().save(dir + "/shot_tray.png");
                shootTimer->stop();
                qApp->exit(0);
            }
            ++(*phase);
        });
        shootTimer->start();
    }

    // dde-dock serves its tray over StatusNotifierItem; clicking the icon
    // opens the rich popup panel, matching design/mac/tray.html.
    QSystemTrayIcon tray(QIcon(":/icons/flowgrid-deepin.svg"));
    tray.setContextMenu(new QMenu); // keeps the SNI item registered as a menu item
    tray.setToolTip("FlowGrid");
    tray.setProperty("Title", "FlowGrid");
    tray.setProperty("Id", "flowgrid-deepin");
    tray.show();

    auto refreshTray = [&] {
        tray.setToolTip(QStringLiteral("FlowGrid · %1").arg(CoreClient::instance().latency()));
        // tray skill gotcha 4: light/dark icon variants, follow host theme
        tray.setIcon(CoreClient::instance().hostDark()
                         ? QIcon(":/icons/flowgrid-deepin-dark.svg")
                         : QIcon(":/icons/flowgrid-deepin.svg"));
    };
    QObject::connect(&CoreClient::instance(), &CoreClient::latencyChanged, refreshTray);
    QObject::connect(&CoreClient::instance(), &CoreClient::hostThemeChanged, refreshTray);
    refreshTray();

    QObject::connect(trayPopupPanel, &TrayPopup::showMainRequested, &window, [&] {
        window.show();
        window.raise();
        window.activateWindow();
    });
    QObject::connect(trayPopupPanel, &TrayPopup::latencyRequested, &latencyPanel, [&] {
        latencyPanel.show();
        latencyPanel.raise();
    });
    QObject::connect(trayPopupPanel, &TrayPopup::addDeviceRequested, &window, [&] {
        AddDeviceDialog dialog(CoreClient::instance(), &window);
        dialog.exec();
    });
    QObject::connect(trayPopupPanel, &TrayPopup::quickSwitchRequested, &window,
                     [&](int slot) { window.switchToDeviceBySlot(slot); });
    QObject::connect(trayPopupPanel, &TrayPopup::aboutRequested, &window, [&] {
        if (auto *dlg = app.aboutDialog()) {
            dlg->exec();
            dlg->deleteLater();
        }
    });
    QObject::connect(trayPopupPanel, &TrayPopup::quitRequested, &app, [] { std::exit(0); });
    QObject::connect(&CoreClient::instance(), &CoreClient::showMainWindowRequested, &window, [&] {
        window.show();
        window.raise();
        window.activateWindow();
    });
    QObject::connect(&CoreClient::instance(), &CoreClient::quitApplicationRequested,
                     &app, [] { QCoreApplication::quit(); });

    QObject::connect(&tray, &QSystemTrayIcon::activated, &window,
                     [&](QSystemTrayIcon::ActivationReason reason) {
                         if (reason == QSystemTrayIcon::Trigger) {
                             if (trayPopupPanel->isVisible())
                                 trayPopupPanel->close();
                             else
                                 trayPopupPanel->popup();
                         }
                     });

    return app.exec();
}
