#include "keyrules.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

#include <utility>

namespace fg {

namespace {

// HID Usage ID（§6.1）：修饰键那一组。
constexpr std::uint16_t kHidLCtrl = 0xE0;
constexpr std::uint16_t kHidLShift = 0xE1;
constexpr std::uint16_t kHidLAlt = 0xE2;
constexpr std::uint16_t kHidLMeta = 0xE3;
constexpr std::uint16_t kHidRCtrl = 0xE4;
constexpr std::uint16_t kHidRShift = 0xE5;
constexpr std::uint16_t kHidRAlt = 0xE6;
constexpr std::uint16_t kHidRMeta = 0xE7;

// 规则文件按优先级排好：用户级覆盖系统级，系统级覆盖发行版级，应用自带的最弱。
//
// 为什么要用户级优先：用户改映射不应该需要 root 改系统文件；系统级是
// 管理员给的全局默认，用户级是他自己的偏好。
//
// 为什么还有 /usr/local/share/flowgrid/：那是 install() 落地的位置——
// /usr 在 deepin 25 的 ostree 机器上是只读 overlay，写不进去；/usr/local 可写。
QStringList candidatePaths()
{
    QStringList paths;
    // 用 GenericConfigLocation + "/flowgrid"，不用 AppConfigLocation：
    // 后者会带上应用名（~/.config/flowgrid/FlowGrid/），而本机配对码已经落在
    // ~/.config/flowgrid/（core/settings.cpp）——同一份用户配置分两个目录，
    // 找文件的人会扑空。
    const QString userDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/flowgrid");
    paths << userDir + QStringLiteral("/keymap.yaml");
    paths << QStringLiteral("/usr/local/share/flowgrid/keymap.yaml");
    paths << QStringLiteral("/usr/share/flowgrid/keymap.yaml");
    return paths;
}

// 应用自带的规则（打进可执行文件的资源）。
//
// 为什么单独走「从字节载入」而不是当成一个路径：协议层用 Go 的 os.ReadFile
// 读文件，读不了 Qt 的资源（:/xxx.yaml）。规则解析只有一份（在协议层），
// 所以这里把字节递进去，而不是自己再解析一遍 YAML。
constexpr char kBundledResource[] = ":/keymap.yaml";


} // namespace

KeyRules &KeyRules::instance()
{
    static KeyRules rules;
    return rules;
}

void KeyRules::ensureLoaded() const
{
    if (!tried_) {
        load();
    }
}

void KeyRules::reload()
{
    load();
    Q_EMIT tableChanged();
}

bool KeyRules::load() const
{
    tried_ = true;
    lastError_.clear();
    sourcePath_.clear();
    table_.reset();

    QStringList tried;
    for (const QString &path : candidatePaths()) {
        tried << path;
        if (!QFile::exists(path)) {
            continue;
        }
        auto table = std::make_unique<proto::KeyMap>(path);
        if (!table->isValid()) {
            // 找到了但坏了：这是**失败**，不是「没有规则」。按默认跑等于用户配的
            // 映射悄悄失效，而界面看不出任何异样。
            lastError_ = table->lastError();
            table_.reset();
            return false;
        }
        sourcePath_ = path;
        table_ = std::move(table);
        return true;
    }

    // 磁盘上没有：试试应用自带的（qrc）。
    QFile bundled(kBundledResource);
    if (bundled.open(QIODevice::ReadOnly)) {
        auto table = std::make_unique<proto::KeyMap>(proto::KeyMap::fromData(bundled.readAll()));
        if (!table->isValid()) {
            lastError_ = table->lastError();
            table_.reset();
            return false;
        }
        sourcePath_ = QObject::tr("应用自带（%1）").arg(kBundledResource);
        table_ = std::move(table);
        return true;
    }

    // 一个都没找到：按平台默认换算即可，不算错。
    lastError_ = QObject::tr("没有找到键位规则文件（%1），按平台默认换算")
                     .arg(tried.join(QStringLiteral("、")));
    return false;
}

QList<KeyRuleRow> KeyRules::rows() const
{
    QList<KeyRuleRow> out;
    ensureLoaded();
    if (table_ == nullptr || !table_->isValid()) {
        return out;
    }
    for (int i = 0; i < table_->ruleCount(); ++i) {
        proto::KeyMap::Rule rule;
        if (!table_->ruleAt(i, &rule)) {
            break;
        }
        // 应用级规则不在这儿显示：它们由 contextGroups() 单独列出，
        // 两边各画一遍就会出现两处计数对不上。
        if (rule.context != QStringLiteral("global")) {
            continue;
        }
        KeyRuleRow row;
        row.fromLabel = keyGlyph(rule.fromPlatform, rule.fromKey);
        row.toLabel = keyGlyph(rule.toPlatform, rule.toKey);
        row.caption = QStringLiteral("%1 → %2").arg(platformName(rule.fromPlatform),
                                                     platformName(rule.toPlatform));
        out.append(row);
    }
    return out;
}

QList<KeyContextGroup> KeyRules::contextGroups() const
{
    QList<KeyContextGroup> out;
    ensureLoaded();
    if (table_ == nullptr || !table_->isValid()) {
        return out;
    }
    // 按 context 归并：一个上下文下的多条规则是同一个覆盖。
    //
    // 顺手把每条规则的「源 → 目标」攒起来：一个覆盖有两条规则时，说明里
    // 只写最后一条，就会出现「2 条覆盖：Alt → ⌃」这种对不上的话。
    QHash<QString, QStringList> pairsByContext;
    for (int i = 0; i < table_->ruleCount(); ++i) {
        proto::KeyMap::Rule rule;
        if (!table_->ruleAt(i, &rule)) {
            break;
        }
        if (rule.context.isEmpty() || rule.context == QStringLiteral("global")) {
            continue;
        }
        int index = -1;
        for (int j = 0; j < out.size(); ++j) {
            if (out.at(j).context == rule.context) {
                index = j;
                break;
            }
        }
        if (index < 0) {
            KeyContextGroup group;
            group.context = rule.context;
            group.label = contextLabel(rule.context);
            group.iconName = rule.context == QStringLiteral("game")
                                 ? QStringLiteral("play")
                                 : QStringLiteral("code");
            out.append(group);
            index = out.size() - 1;
        }
        KeyContextGroup &group = out[index];
        ++group.ruleCount;
        pairsByContext[rule.context].append(QStringLiteral("%1 → %2")
                                                .arg(keyGlyph(rule.fromPlatform, rule.fromKey),
                                                     keyGlyph(rule.toPlatform, rule.toKey)));
    }
    for (KeyContextGroup &group : out) {
        const QStringList pairs = pairsByContext.value(group.context);
        group.hint = pairs.size() > 1
                         ? QObject::tr("%1 条覆盖：%2").arg(pairs.size()).arg(pairs.join(QStringLiteral("、")))
                         : QObject::tr("1 条覆盖：%1").arg(pairs.value(0));
    }
    return out;
}

QList<KeyConflictRow> KeyRules::conflicts() const
{
    QList<KeyConflictRow> out;
    ensureLoaded();
    if (table_ == nullptr || !table_->isValid()) {
        return out;
    }
    const QList<proto::KeyMap::Conflict> list = table_->conflicts();
    for (const proto::KeyMap::Conflict &c : list) {
        KeyConflictRow row;
        row.fromLabel = keyGlyph(c.fromPlatform, c.fromKey);
        row.toLabel = keyGlyph(c.toPlatform, c.toKey);
        row.toPlatform = platformName(c.toPlatform);
        out.append(row);
    }
    return out;
}

// 上下文 → 显示名。
//
// bundle ID 没有对应的显示名表：编一个「com.microsoft.VSCode → Visual Studio
// Code」的映射表就是猜，猜错了界面会骗人。原样显示 ID 更诚实。
QString KeyRules::contextLabel(const QString &context)
{
    if (context == QStringLiteral("game")) {
        return QObject::tr("游戏模式");
    }
    if (context.startsWith(QStringLiteral("app:"))) {
        return context.mid(4);
    }
    return context;
}

QString KeyRules::platformName(std::uint8_t platform)
{
    switch (platform) {
    case proto::platform::kLinux:
        return QStringLiteral("Linux");
    case proto::platform::kMacOS:
        return QStringLiteral("macOS");
    case proto::platform::kWindows:
        return QStringLiteral("Windows");
    default:
        return QStringLiteral("平台 %1").arg(platform);
    }
}

QString KeyRules::keyGlyph(std::uint8_t platform, std::uint16_t hidKey)
{
    const bool mac = (platform == proto::platform::kMacOS);
    switch (hidKey) {
    case kHidLCtrl:
        return mac ? QStringLiteral("⌃") : QStringLiteral("Ctrl");
    case kHidRCtrl:
        return mac ? QStringLiteral("⌃") : QStringLiteral("Ctrl");
    case kHidLShift:
        return mac ? QStringLiteral("⇧") : QStringLiteral("Shift");
    case kHidRShift:
        return mac ? QStringLiteral("⇧") : QStringLiteral("Shift");
    case kHidLAlt:
        return mac ? QStringLiteral("⌥") : QStringLiteral("Alt");
    case kHidRAlt:
        return mac ? QStringLiteral("⌥") : QStringLiteral("Alt");
    case kHidLMeta:
        return mac ? QStringLiteral("⌘") : QStringLiteral("Super");
    case kHidRMeta:
        return mac ? QStringLiteral("⌘") : QStringLiteral("Super");
    default:
        // 认不出来的键位就报码值，不猜：猜错了界面会骗人。
        return QStringLiteral("0x%1").arg(hidKey, 4, 16, QLatin1Char('0'));
    }
}

} // namespace fg
