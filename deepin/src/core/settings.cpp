#include "settings.h"

#include <DConfig>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcSettings, "flowgrid.settings")

namespace fg {
namespace {

// DConfig 的键名与 meta 文件（resources/flowgrid-deepin.json）里的 key 一一
// 对应。改这里必须同步改那个 json，否则写入会被 DConfig 拒掉并静默失效。
constexpr auto kKeyPreferredLink = "connection/preferred_link";
constexpr auto kKeyAutoRouting   = "connection/auto_routing";
constexpr auto kKeyAutoStart     = "connection/auto_start";
constexpr auto kKeyKeymap        = "input/keymap_enabled";
constexpr auto kKeySmoothing     = "input/mouse_smoothing";
constexpr auto kKeyEdgeThreshold = "input/edge_threshold";
constexpr auto kKeySwitchHotkey  = "input/switch_hotkey";
constexpr auto kKeyClipSync      = "sync/clipboard_enabled";
constexpr auto kKeyClipHistory   = "sync/clipboard_history";
constexpr auto kKeyFileTransfer  = "sync/file_transfer_enabled";
constexpr auto kKeyE2E           = "security/e2e_encryption";
constexpr auto kKeyConfirm       = "security/require_confirmation";

// DConfig::create(appId, name, subpath) 的三段语义，别混：
//   appId   —— meta 文件所在的目录名，即 /usr/share/<appId>/<name>.json；
//   name    —— meta 文件名（不含 .json），要和 JSON 里的 fileName 一致；
//   subpath —— JSON 内部再往下的一层路径，通常留空，本工程的键已自带
//              "connection/xxx" 这样的完整相对路径。
// 三者对不上的表现不是报错，而是 create() 返回空指针、设置全部走默认值。
constexpr auto kAppId   = "flowgrid";
constexpr auto kName    = "flowgrid-deepin";
constexpr auto kSubPath = "";

} // namespace

Settings &Settings::instance()
{
    static Settings s;
    return s;
}

Settings::Settings()
{
    config_ = Dtk::Core::DConfig::create(QString::fromLatin1(kAppId),
                                          QString::fromLatin1(kName),
                                          QString::fromLatin1(kSubPath));
    // isValid() 与 nullptr 要分开判：后端建起来了但 meta 文件缺失时，
    // create() 仍会返回非空对象，此时 setValue 会被静默丢弃。
    if (config_ != nullptr && !config_->isValid()) {
        reason_ = QStringLiteral("DConfig meta 未安装（缺少 /usr/share/%1/%2.json）")
                      .arg(QLatin1String(kAppId), QLatin1String(kName));
        qCWarning(lcSettings) << reason_;
        delete config_;
        config_ = nullptr;
    }
    if (config_ == nullptr && reason_.isEmpty()) {
        reason_ = QStringLiteral("DConfig 后端不可用");
        qCWarning(lcSettings) << reason_;
    }

    if (config_ != nullptr) {
        // DConfig 的 valueChanged 只带 key，没有 value——变更内容要自己再读一次。
        connect(config_, &Dtk::Core::DConfig::valueChanged, this,
                [this](const QString &key) { Q_EMIT changed(key); });
    }
}

QVariant Settings::read(const QString &key, const QVariant &fallback) const
{
    if (config_ != nullptr) {
        return config_->value(key, fallback);
    }
    return fallback_.value(key, fallback);
}

void Settings::write(const QString &key, const QVariant &value)
{
    if (config_ != nullptr) {
        config_->setValue(key, value);
        return;
    }
    if (fallback_.value(key) == value) {
        return;
    }
    fallback_.insert(key, value);
    Q_EMIT changed(key);
}

QString Settings::preferredLink() const
{
    return read(QLatin1String(kKeyPreferredLink), QStringLiteral("IntelligentLink")).toString();
}

void Settings::setPreferredLink(const QString &value)
{
    write(QLatin1String(kKeyPreferredLink), value);
}

bool Settings::autoRouting() const
{
    return read(QLatin1String(kKeyAutoRouting), true).toBool();
}

void Settings::setAutoRouting(bool on)
{
    write(QLatin1String(kKeyAutoRouting), on);
}

bool Settings::autoStart() const
{
    return read(QLatin1String(kKeyAutoStart), true).toBool();
}

void Settings::setAutoStart(bool on)
{
    write(QLatin1String(kKeyAutoStart), on);
}

bool Settings::keymapEnabled() const
{
    return read(QLatin1String(kKeyKeymap), true).toBool();
}

void Settings::setKeymapEnabled(bool on)
{
    write(QLatin1String(kKeyKeymap), on);
}

bool Settings::mouseSmoothing() const
{
    return read(QLatin1String(kKeySmoothing), false).toBool();
}

void Settings::setMouseSmoothing(bool on)
{
    write(QLatin1String(kKeySmoothing), on);
}

int Settings::edgeThreshold() const
{
    return read(QLatin1String(kKeyEdgeThreshold), 8).toInt();
}

void Settings::setEdgeThreshold(int px)
{
    write(QLatin1String(kKeyEdgeThreshold), px);
}

int Settings::switchHotkey() const
{
    return read(QLatin1String(kKeySwitchHotkey), 1).toInt();
}

void Settings::setSwitchHotkey(int key)
{
    write(QLatin1String(kKeySwitchHotkey), key);
}

bool Settings::clipboardSync() const
{
    return read(QLatin1String(kKeyClipSync), true).toBool();
}

void Settings::setClipboardSync(bool on)
{
    write(QLatin1String(kKeyClipSync), on);
}

int Settings::clipboardHistory() const
{
    // 设计稿：同步历史保留最近 50 条
    return read(QLatin1String(kKeyClipHistory), 50).toInt();
}

void Settings::setClipboardHistory(int count)
{
    write(QLatin1String(kKeyClipHistory), count);
}

bool Settings::fileTransfer() const
{
    return read(QLatin1String(kKeyFileTransfer), true).toBool();
}

void Settings::setFileTransfer(bool on)
{
    write(QLatin1String(kKeyFileTransfer), on);
}

bool Settings::e2eEncryption() const
{
    // 端到端加密没有关闭项：v1.1 的 DTLS 握手强制启用（PROTOCOL.md §3），
    // 这里读配置只是为了让设置屏能显示「已启用」，不接受写入。
    return read(QLatin1String(kKeyE2E), true).toBool();
}

bool Settings::requireConfirmation() const
{
    return read(QLatin1String(kKeyConfirm), true).toBool();
}

void Settings::setRequireConfirmation(bool on)
{
    write(QLatin1String(kKeyConfirm), on);
}

void Settings::resetToDefaults()
{
    static const char *const kAllKeys[] = {
        kKeyPreferredLink, kKeyAutoRouting,   kKeyAutoStart,
        kKeyKeymap,       kKeySmoothing,     kKeyEdgeThreshold,
        kKeySwitchHotkey, kKeyClipSync,      kKeyClipHistory,
        kKeyFileTransfer, kKeyE2E,            kKeyConfirm,
    };
    for (const char *k : kAllKeys) {
        const QString key = QLatin1String(k);
        if (config_ != nullptr) {
            config_->reset(key);   // 回到 meta 文件里的 default
        } else if (fallback_.remove(key) > 0) {
            Q_EMIT changed(key);
        }
    }
}

} // namespace fg
