// Rich tray popup, follows design/mac/tray.html: header with live status
// pill, current-device card, quick-switch buttons (Ctrl+1..3), device rows,
// quick-action toggles bound to settings, and plain menu items. Frameless
// popup painted with palette colors (dde light/dark aware).
#ifndef FLOWGRID_TRAY_POPUP_H
#define FLOWGRID_TRAY_POPUP_H

#include <DBlurEffectWidget>

#include <QWidget>

#include "core_client.h"

class QLabel;
class QGridLayout;
class QVBoxLayout;

class TrayPopup : public DTK_WIDGET_NAMESPACE::DBlurEffectWidget {
    Q_OBJECT

public:
    explicit TrayPopup(QWidget *parent = nullptr);

    void popup();

signals:
    void showMainRequested();
    void latencyRequested();
    void addDeviceRequested();
    void aboutRequested();
    void quickSwitchRequested(int slot);
    void quitRequested();

private:
    QLabel *sectionTitle(const QString &text);
    QWidget *toggleRow(const QString &label, const QString &key, bool usesCapturing);
    void refresh();

    CoreClient &m_core;
    QLabel *m_latencyText = nullptr;
    QLabel *m_statusDot = nullptr;
    QWidget *m_currentDeviceCard = nullptr;
    QWidget *m_currentDeviceIcon = nullptr;
    QLabel *m_currentDeviceName = nullptr;
    QWidget *m_quickSwitchHost = nullptr;
    QGridLayout *m_quickSwitchLayout = nullptr;
    QVBoxLayout *m_deviceRowsLayout = nullptr;
    QLabel *m_currentTitle = nullptr;
    QLabel *m_quickTitle = nullptr;
    QLabel *m_devicesTitle = nullptr;
};

#endif // FLOWGRID_TRAY_POPUP_H
