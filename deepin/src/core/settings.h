// 应用配置：DConfig（deepin 原生配置后端）+ YAML 键位映射。
//
// 选 DConfig 而不是 QSettings，是因为它在 dde 设置中心里有对应条目、支持
// per-user 隔离与变更通知，符合 DTK 规范；QSettings 只能落在散落的 ini 里。
// 键位映射表另走 YAML（协议层 LoadKeyMapFile 的格式），两者不混。
#ifndef FLOWGRID_SETTINGS_H
#define FLOWGRID_SETTINGS_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

// DConfig 声明在 Dtk::Core 里（见 dtk6/DCore/dconfig.h）。前向声明必须连
// 命名空间一起复刻，否则会造出一个全局 ::DConfig，与真身对不上——真身定义
// 在 DCORE_BEGIN_NAMESPACE（Dtk::Core）之内。
#include <dtkcore_global.h>
DCORE_BEGIN_NAMESPACE
class DConfig;
DCORE_END_NAMESPACE

namespace fg {

class Settings : public QObject {
    Q_OBJECT
public:
    static Settings &instance();

    // DConfig 在无 DDE 会话、或 meta 文件没装到 /usr/share/<appId>/ 时不可用。
    // 这时退化成进程内 Hash：设置屏的开关仍然可点可生效，只是不跨进程保留。
    bool isBackedByDConfig() const { return config_ != nullptr; }
    QString unavailableReason() const { return reason_; }

    // ── 连接 ──
    QString preferredLink() const;                  // "IntelligentLink" / "DirectLink" / "IntelligentLinkOnly"
    void setPreferredLink(const QString &value);
    bool autoRouting() const;
    void setAutoRouting(bool on);
    bool autoStart() const;
    void setAutoStart(bool on);

    // ── 输入 ──
    bool keymapEnabled() const;
    void setKeymapEnabled(bool on);
    bool mouseSmoothing() const;
    void setMouseSmoothing(bool on);
    int edgeThreshold() const;
    void setEdgeThreshold(int px);
    int switchHotkey() const;
    void setSwitchHotkey(int key);

    // ── 跨端同步 ──
    bool clipboardSync() const;
    void setClipboardSync(bool on);
    int clipboardHistory() const;
    void setClipboardHistory(int count);
    // 关闭后入站的 image/* 条目直接丢弃，不进历史（客户端侧过滤）。
    bool clipboardImages() const;
    void setClipboardImages(bool on);
    // 协议帧不带来源应用标识（PROTOCOL.md §5.3 只有 MIME + 载荷），所以这一项
    // 目前只落盘不生效；剪贴板屏那一行如实标注了「需协议支持」。
    bool clipboardIgnorePasswords() const;
    void setClipboardIgnorePasswords(bool on);
    // 打开后入站条目自动写入本机剪贴板。
    bool clipboardAutoWrite() const;
    void setClipboardAutoWrite(bool on);
    bool fileTransfer() const;
    void setFileTransfer(bool on);

    // ── 安全 ──
    // 端到端加密没有关闭项：DTLS 握手强制启用（PROTOCOL.md §3），只有 getter。
    bool e2eEncryption() const;
    // 新设备需确认是用户偏好，设计稿 settings.html 给的就是一个可点开关，
    // DConfig meta 里也是普通可写键（默认 true）。
    bool requireConfirmation() const;
    void setRequireConfirmation(bool on);

    // 页脚「恢复默认」：交给 DConfig 的 reset，回到 meta 文件里的默认值，
    // 而不是在这里把 12 个默认值再抄一遍——抄一份就会漂移一份。
    void resetToDefaults();

    // 应用实际管理的全部 DConfig 键（就是 settings.cpp 里那组常量）。
    // 用途只有一个：界面自检拿它跟 meta 文件的 contents 对表——
    // 应用读一个 meta 里没有的键，DConfig 会静默丢写入 / 回默认值，
    // 表现是「设置屏的开关看着能点，关掉进程就没了」。这层漂移没有别的
    // 地方能拦住，所以把它做成可自检的一条。
    static QStringList managedKeys();

Q_SIGNALS:
    void changed(const QString &key);

private:
    Settings();

    // 读写都走这两个：藏住「DConfig 还是兜底 Hash」的分叉，避免每个 getter
    // 都写一遍三目表达式（那正是上一版 20+ 处重复代码的来源）。
    QVariant read(const QString &key, const QVariant &fallback) const;
    void write(const QString &key, const QVariant &value);

    Dtk::Core::DConfig *config_ = nullptr;
    QHash<QString, QVariant> fallback_;
    QString reason_;
};

// input/switch_hotkey 取值语义（1=Ctrl / 2=Alt）对应的修饰键名。设置屏的键帽、
// 托盘里的三处快捷键提示都从这里派生——原先只有托盘的两条导航行跟着设置走，
// 另三处写死「Ctrl」，用户把热键改成 Alt 后界面就指着一组并不存在的快捷键。
inline QString switchModifierName(int hotkey)
{
    return hotkey == 2 ? QStringLiteral("Alt") : QStringLiteral("Ctrl");
}

} // namespace fg

#endif // FLOWGRID_SETTINGS_H
