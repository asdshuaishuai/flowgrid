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
    bool fileTransfer() const;
    void setFileTransfer(bool on);

    // ── 安全 ──
    bool e2eEncryption() const;
    bool requireConfirmation() const;

    // 页脚「恢复默认」：交给 DConfig 的 reset，回到 meta 文件里的默认值，
    // 而不是在这里把 12 个默认值再抄一遍——抄一份就会漂移一份。
    void resetToDefaults();

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

} // namespace fg

#endif // FLOWGRID_SETTINGS_H
