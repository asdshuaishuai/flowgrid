// CoreClient: QObject wrapper around the Rust C ABI (fg_core_*).
// Pulls events at frame rate, refreshes cached data, emits Qt signals.
#ifndef FLOWGRID_CORE_CLIENT_H
#define FLOWGRID_CORE_CLIENT_H

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class CoreClient : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.flowgrid.DDE")

public:
    static CoreClient &instance();

    // Starts the Rust core; safe to call once at app startup.
    void start();

    QVariantList devices() const { return m_devices; }
    QVariantList keymapRules() const { return m_rules; }
    QVariantMap settings() const { return m_settings; }
    QString latency() const { return m_latency; }
    QString connectionState() const { return m_connectionState; }
    bool scanning() const { return m_scanning; }

    void scan();
    void connectDevice(const QString &id);
    void disconnectDevice(const QString &id);
    void removeDevice(const QString &id);
    void setSetting(const QString &key, bool value);

    // Host-side capture; returns false when it could not start (permissions).
    bool setCapturing(bool enabled);
    bool capturing() const;
    bool hostDark() const;

signals:
    // exposed on org.flowgrid.DDE for single-instance & dock integration
    void showMainWindowRequested();
    void quitApplicationRequested();

    void devicesChanged();
    void hostThemeChanged(bool dark);
    void keymapChanged();
    void settingsChanged();
    void latencyChanged();
    void capturingChanged(bool enabled);
    void scanStateChanged(bool scanning);
    void errorOccurred(const QString &message);

public slots:
    // org.flowgrid.DDE methods (second launch / dock plugin call these)
    void requestShowMainWindow();
    void quitApplication();

    // Follows the host Appearance1/GlobalTheme (deepin 25 exposes dark mode
    // there; DGuiApplicationHelper::themeType() misses it on this stack).
    void applyHostTheme();

private:
    explicit CoreClient(QObject *parent = nullptr);
    void pollEvents();
    void refreshDevices();
    void refreshRules();
    void refreshSettings();

    QTimer m_timer;
    QVariantList m_devices;
    QVariantList m_rules;
    QVariantMap m_settings;
    QString m_latency;
    QString m_connectionState;
    bool m_hostDark = false;
    bool m_scanning = false;
};

#endif // FLOWGRID_CORE_CLIENT_H
