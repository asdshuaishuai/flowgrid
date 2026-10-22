#include "settings.h"

#include <DConfig>

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcSettings, "flowgrid.settings")

namespace fg {
namespace {

// 本机配对码的位数与落盘位置。
//
// 放在 ~/.config/flowgrid/ 而不是 DConfig，理由见 settings.h 里那段注释：
// DConfig 的 meta 装在系统目录，没跟着新版本重装就没有这个键，写入会被
// 静默丢弃，码就会每次开面板都换一个。
constexpr int kPairCodeDigits = 4;

QString pairCodePath()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/flowgrid");
    return dir + QStringLiteral("/paircode");
}

// 只收 4 位 ASCII 数字：文件是人能编辑的，别的内容一律当损坏处理。
bool isPairCode(const QString &s)
{
    if (s.size() != kPairCodeDigits) {
        return false;
    }
    for (const QChar c : s) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) {
            return false;
        }
    }
    return true;
}

// 新生成一组随机数字。
//
// 用随机数而不是从身份公钥派生：公钥是跟着 LAN 通告广播出去的，谁拿到
// 都能推算出「你的配对码」，那「必须有人站在机器旁边」这条就没了。随机
// 落盘才保得住「这组数字只有本机知道」。
QString newPairCode()
{
    QRandomGenerator *rng = QRandomGenerator::global();
    QString code;
    code.reserve(kPairCodeDigits);
    for (int i = 0; i < kPairCodeDigits; ++i) {
        code += QLatin1Char('0' + static_cast<char>(rng->bounded(10)));
    }
    return code;
}

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
constexpr auto kKeyClipImages    = "sync/clipboard_images";
constexpr auto kKeyClipIgnorePwd = "sync/clipboard_ignore_passwords";
constexpr auto kKeyClipAutoWrite = "sync/clipboard_auto_write";
constexpr auto kKeyFileTransfer  = "sync/file_transfer_enabled";
constexpr auto kKeyE2E           = "security/e2e_encryption";
constexpr auto kKeyConfirm       = "security/require_confirmation";

// DConfig::create(appId, name, subpath) 的三段语义，别混：
//   appId   —— meta 所在目录名，完整位置是
//              /usr/share/dsg/configs/<appId>/<name>.json。
//              这个路径是 DTK 自己给的，不是约定俗成：DConfigMeta::
//              applicationMetaDirs("", appId) 返回的正是
//              /usr/share/dsg/configs/<appId>。dconfig 守护进程只扫
//              /usr/share/dsg 与 /var/lib/linglong/entries/share/dsg
//              （dde-dconfig-daemon.service 的 DSG_DATA_DIRS），装到
//              /usr/share/flowgrid/ 或 /usr/local/share/ 下它都看不见。
//   name    —— meta 文件名（不含 .json）。
//   subpath —— JSON 内部再往下的一层路径，通常留空，本工程的键已自带
//              "connection/xxx" 这样的完整相对路径。
// 三者对不上的表现不是报错，而是 create() 返回不可用对象、设置全部走默认值。
//
// appId / name 不在这里写死：CMakeLists 是唯一来源，编译期发下来，
// 否则改了这里却忘了改安装目录，就会得到一次「设置静默失效」的回归。
//
// 另外 meta 文件本身必须是 DSG 格式（顶层 magic/version/contents 映射）。
// 旧 dconfig 那种 fileName/keys 数组的写法，DDE 25 的 dconfig 直接拒收
// （"The meta magic does not match"），表现同样是设置静默失效。
constexpr auto kAppId   = FLOWGRID_DCONFIG_APPID;
constexpr auto kName    = FLOWGRID_DCONFIG_NAME;
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
        reason_ = QStringLiteral("DConfig meta 未安装或格式不对（期望 /usr/share/dsg/configs/%1/%2.json，DSG 格式）")
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

bool Settings::clipboardImages() const
{
    return read(QLatin1String(kKeyClipImages), true).toBool();
}

void Settings::setClipboardImages(bool on)
{
    write(QLatin1String(kKeyClipImages), on);
}

bool Settings::clipboardIgnorePasswords() const
{
    return read(QLatin1String(kKeyClipIgnorePwd), true).toBool();
}

void Settings::setClipboardIgnorePasswords(bool on)
{
    write(QLatin1String(kKeyClipIgnorePwd), on);
}

bool Settings::clipboardAutoWrite() const
{
    return read(QLatin1String(kKeyClipAutoWrite), false).toBool();
}

void Settings::setClipboardAutoWrite(bool on)
{
    write(QLatin1String(kKeyClipAutoWrite), on);
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
    // 键表以 DConfig meta 为唯一源头，不在此手抄一份。上一版正是手抄的：
    // 「恢复默认」漏掉了 clipboard_images / clipboard_ignore_passwords /
    // clipboard_auto_write 三个键，点完按钮它们不还原——列表与定义两处漂移。
    if (config_ != nullptr) {
        const QStringList keys = config_->keyList();
        for (const QString &key : keys) {
            config_->reset(key);   // 回到 meta 文件里的 default
        }
        return;
    }
    // 兜底 Hash：只有被写过的键才在表里，逐个移除即回到进程内默认。
    const QStringList keys = fallback_.keys();
    for (const QString &key : keys) {
        fallback_.remove(key);
        Q_EMIT changed(key);
    }
}

// ── 本机配对码 ────────────────────────────────────────────────────────

QString Settings::pairingCode()
{
    QFile f(pairCodePath());
    if (f.open(QIODevice::ReadOnly)) {
        const QString stored = QString::fromLatin1(f.readAll()).trimmed();
        f.close();
        if (isPairCode(stored)) {
            return stored;
        }
    }
    // 读不到 / 读坏了（首次运行、被人手改过）：现生成一组并落盘。
    return refreshPairingCode();
}

QString Settings::refreshPairingCode()
{
    const QString code = newPairCode();
    const QString path = pairCodePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    // 用 QSaveFile 而不是「自己写 .tmp 再 QFile::rename」。
    //
    // Qt 的 QFile::rename **不覆盖已存在的文件**（POSIX 的 rename() 覆盖，
    // Qt 在这上面做了保护：实测 rename 到已存在的目标返回 false，原文件原样
    // 不动）。原先那个写法于是只在「文件还不存在」时成功——第一次写入 OK，
    // 之后每次「刷新」都改名失败：界面上码变了，盘上还是旧的，用户对着
    // 屏幕念的码跟对端拿到的对不上。
    //
    // QSaveFile 一次解决两件事：写临时文件再原子替换（写到一半掉电不会留下
    // 半截的码，下次启动读它会当成损坏又换一个，用户正对着屏幕输的码就白输了），
    // 以及**能覆盖已存在的目标文件**。
    QSaveFile f(path);
    // 配对码是凭据：同机其它用户读到就能直接拿着它去跟本机配对。
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(lcSettings) << "配对码写入失败" << path << f.errorString();
        return code;   // 落盘失败也要给调用方一个本次会话内一致的码
    }
    f.write(code.toLatin1());
    if (!f.commit()) {
        qCWarning(lcSettings) << "配对码落盘失败" << path << f.errorString();
    }
    return code;
}

QStringList Settings::managedKeys()
{
    // 就是上面那组常量，一处定义两处用：读写走它，自检拿它跟 meta 对表。
    //
    // 本机配对码不在表里：它不走 DConfig，落在 ~/.config/flowgrid/paircode。
    return {
        QLatin1String(kKeyPreferredLink), QLatin1String(kKeyAutoRouting),
        QLatin1String(kKeyAutoStart),    QLatin1String(kKeyKeymap),
        QLatin1String(kKeySmoothing),     QLatin1String(kKeyEdgeThreshold),
        QLatin1String(kKeySwitchHotkey),  QLatin1String(kKeyClipSync),
        QLatin1String(kKeyClipHistory),   QLatin1String(kKeyClipImages),
        QLatin1String(kKeyClipIgnorePwd), QLatin1String(kKeyClipAutoWrite),
        QLatin1String(kKeyFileTransfer),  QLatin1String(kKeyE2E),
        QLatin1String(kKeyConfirm),
    };
}

} // namespace fg
