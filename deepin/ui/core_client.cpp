#include "core_client.h"

#include "flowgrid_core_capi.h"

#include <dguiapplicationhelper.h>

#include <QGuiApplication>

#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QByteArray callString(char *fn()) {
    char *raw = fn();
    QByteArray result(raw ? raw : "");
    fg_core_free_string(raw);
    return result;
}

} // namespace

CoreClient &CoreClient::instance() {
    static CoreClient client;
    return client;
}

CoreClient::CoreClient(QObject *parent)
    : QObject(parent) {
    connect(&m_timer, &QTimer::timeout, this, &CoreClient::pollEvents);
}

void CoreClient::start() {
    fg_core_start(nullptr);
    refreshDevices();
    refreshRules();
    refreshSettings();
    m_latency = QString::fromUtf8(callString(fg_core_get_latency));
    m_connectionState = QString::fromUtf8(callString(fg_core_get_connection_state));
    // Pull pushed core events; 100 ms keeps the UI responsive without polling
    // business data (data refreshes only when an event arrives).
    m_timer.start(100);
}

void CoreClient::pollEvents() {
    const QJsonDocument doc = QJsonDocument::fromJson(callString(fg_core_poll_events));
    bool devices = false, rules = false;
    for (const QJsonValue &value : doc.array()) {
        const QString type = value.toObject().value("type").toString();
        if (type == QLatin1String("devices_changed")) {
            devices = true;
        } else if (type == QLatin1String("latency_updated")) {
            devices = true;
        } else if (type == QLatin1String("keymap_changed")) {
            rules = true;
        } else if (type == QLatin1String("scan_state_changed")) {
            const bool scanning = value.toObject().value("scanning").toBool();
            if (scanning != m_scanning) {
                m_scanning = scanning;
                emit scanStateChanged(scanning);
            }
        } else if (type == QLatin1String("error")) {
            emit errorOccurred(value.toObject().value("message").toString());
        }
    }
    if (devices) {
        refreshDevices();
        emit devicesChanged();
        const QString latency = QString::fromUtf8(callString(fg_core_get_latency));
        if (latency != m_latency) {
            m_latency = latency;
            emit latencyChanged();
        }
        const QString state = QString::fromUtf8(callString(fg_core_get_connection_state));
        if (state != m_connectionState) {
            m_connectionState = state;
            emit latencyChanged();
        }
    }
    if (rules) {
        refreshRules();
        emit keymapChanged();
    }
}

void CoreClient::refreshDevices() {
    m_devices = QJsonDocument::fromJson(callString(fg_core_get_devices)).array().toVariantList();
}

void CoreClient::refreshRules() {
    m_rules = QJsonDocument::fromJson(callString(fg_core_get_keymap_rules)).array().toVariantList();
}

void CoreClient::refreshSettings() {
    m_settings = QJsonDocument::fromJson(callString(fg_core_get_settings)).object().toVariantMap();
}

void CoreClient::scan() {
    fg_core_scan();
}

void CoreClient::connectDevice(const QString &id) {
    fg_core_connect(id.toUtf8().constData());
}

void CoreClient::disconnectDevice(const QString &id) {
    fg_core_disconnect(id.toUtf8().constData());
}

void CoreClient::removeDevice(const QString &id) {
    fg_core_remove_device(id.toUtf8().constData());
}

void CoreClient::setSetting(const QString &key, bool value) {
    if (fg_core_set_setting(key.toUtf8().constData(), value ? 1 : 0) == 0) {
        m_settings.insert(key, value);
        emit settingsChanged();
    }
}

bool CoreClient::setCapturing(bool enabled) {
    if (fg_core_set_capturing(enabled ? 1 : 0) != 0)
        return false;
    emit capturingChanged(enabled);
    return true;
}

bool CoreClient::capturing() const {
    return fg_core_get_capturing() != 0;
}

bool CoreClient::hostDark() const {
    return m_hostDark;
}

void CoreClient::requestShowMainWindow() {
    emit showMainWindowRequested();
}

void CoreClient::quitApplication() {
    emit quitApplicationRequested();
}

void CoreClient::applyHostTheme() {
    using namespace DTK_GUI_NAMESPACE;
    QDBusInterface appearance("org.deepin.dde.Appearance1",
                              "/org/deepin/dde/Appearance1",
                              "org.freedesktop.DBus.Properties");
    QDBusReply<QDBusVariant> reply = appearance.call(
        "Get", "org.deepin.dde.Appearance1", "GlobalTheme");
    const QString theme = reply.isValid()
        ? reply.value().variant().toString() : QString();
    const bool dark = theme.contains(".dark");
    const auto type = dark ? DGuiApplicationHelper::DarkType
                           : DGuiApplicationHelper::LightType;
    // Official DTK switch: propagates to every DTK widget's DPalette.
    DGuiApplicationHelper::instance()->setPaletteType(type);
    qInfo("host GlobalTheme=%s -> %s",
          qPrintable(theme.isEmpty() ? QString("?") : theme),
          dark ? "dark" : "light");
    emit hostThemeChanged(dark);
}
