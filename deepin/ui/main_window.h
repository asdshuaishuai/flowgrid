// Main window. Layout/interactions follow design/mac/main.html (dynamic
// device cards with live latency pills, icon actions, collapsible sections,
// three-way mode selector, 3x3 device layout grid with drag&drop and
// Ctrl+1..9 switching, toasts). Visuals are native DTK widgets + palette.
#ifndef FLOWGRID_MAIN_WINDOW_H
#define FLOWGRID_MAIN_WINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QVariantList>

#include <DSuggestButton>
#include <DWarningButton>
#include <DBackgroundGroup>
#include "chip.h"
#include <DIconButton>
#include <dheaderline.h>

#include "core_client.h"

class QScrollArea;
class QVBoxLayout;
class QGridLayout;
class QKeyEvent;

namespace Dtk { namespace Widget { class DIconButton; class DSwitchButton; } }
using Dtk::Widget::DIconButton;
using Dtk::Widget::DSwitchButton;
using Dtk::Widget::DBackgroundGroup;

class MainWindow : public QMainWindow {
    Q_OBJECT

    // collapsible sections (design: chevron + collapsible body)
    struct Section {
        QWidget *root = nullptr;
        QWidget *body = nullptr;
        QLabel *chevron = nullptr;
        bool collapsed = false;
    };

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // called by the layout-grid drop handler (deepin/native UI side)
    void moveDeviceToSlot(const QString &id, int gridX, int gridY);

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

public slots:
    // tray quick-switch (Ctrl+1..9 mirror)
    void switchToDeviceBySlot(int slot);

private:
    QWidget *buildToolbar();
    QWidget *buildDeviceSection();
    QWidget *buildCollapsibleSection(Section *section, const QString &title,
                                     const QString &badgeText, QWidget *body);
    QWidget *buildKeymapSection();
    QWidget *buildModeSection();
    QWidget *buildLayoutSection();
    QWidget *buildSettingsSection();
    QWidget *buildDeviceCard(const QVariantMap &device);
    QPushButton *modeOptionCard(const QString &name, const QString &desc, const QString &badge);
    QWidget *switchCell(int gridX, int gridY);
    QWidget *makeTile(const QString &text, int size);
    void rebuildDeviceList();
    void rebuildRules();
    void rebuildLayoutGrid();
    void refreshConnectionStatus();
    void showToast(const QString &message);
    void showTransientError(const QString &message);
    static QString platformAbbrev(const QString &platform);
    static QString keyName(const QString &hexCode);
    static QString contextLabel(const QString &context);

    // toolbar
    QLabel *m_statusDot = nullptr;
    QLabel *m_statusLabel = nullptr;

    // device list
    QWidget *m_deviceListHost = nullptr;
    QVBoxLayout *m_deviceListLayout = nullptr;
    DBackgroundGroup *m_deviceGroup = nullptr;

    Chip *m_keymapBadge = nullptr;
    QVBoxLayout *m_keymapRulesLayout = nullptr;
    Section m_keymapSection;
    Section m_modeSection;
    Section m_layoutSection;

    // mode selector
    QList<QPushButton *> m_modeButtons;
    QStringList m_modeNames = { "Auto", "NearLink", "DirectLink" };
    int m_modeIndex = 0;

    // 3x3 device layout grid (design: 设备布局)
    QGridLayout *m_layoutGrid = nullptr;
    struct LayoutSlot {
        int gridX = -1;
        int gridY = -1;
        QString deviceId; // empty = free slot
    };
    QList<LayoutSlot> m_layout;
    QString m_currentDeviceId;

    QVBoxLayout *m_mainLayout = nullptr;
    CoreClient &m_core;
};
#endif
