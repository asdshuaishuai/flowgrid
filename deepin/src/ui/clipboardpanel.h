// 剪贴板浮板：对应 design/unified/screens/clipboard.html。
// 分段/重组/超时全部由 Go 的 ClipboardSession 负责（PROTOCOL.md §5.3），
// 本屏只做展示与「写入本地 / 推送」两个动作。
#ifndef FLOWGRID_CLIPBOARDPANEL_H
#define FLOWGRID_CLIPBOARDPANEL_H

#include "core/protocolapi.h"
#include "ui/panel.h"

class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace fg {

class Badge;
class SegmentedControl;
class StatusDot;

struct ClipEntry {
    QString mime = QStringLiteral("text/plain");
    QString kind;         // text / code / link / image：分段控件的过滤依据
    QString text;         // 单行预览
    QByteArray raw;       // 原始载荷，推送时用
    QString source;       // 「MacBook Pro」
    int agoSecs = 0;
    bool oversize = false;   // 超 63,240 B 上限，不入库（设计稿的「超限」）
};

class ClipboardPanel : public Panel {
    Q_OBJECT
public:
    explicit ClipboardPanel(QWidget *parent = nullptr);

    void reloadHistory();

    // 托盘徽标用的条目数（只读）。
    int historyCount() const { return history_.size(); }

    // 仅 --snapshot 用：无头环境没有对端设备，历史永远是空的，ClipRow 的双行
    // 排版、分段控件的过滤、搜索框的匹配这三条路径一条都跑不到。种一批与
    // clipboard.html 同款的数据进去，截图才能和设计稿逐项对照。正常运行
    // 永远不会调它。
    void seedForSnapshot();

public Q_SLOTS:
    void clearHistory();

    // 会话在传输层落地前是空壳，但接线方式已定：入站走 handleFrame，
    // 出站走 send。构造函数已建立会话句柄。
    proto::ClipboardSession &session() { return session_; }

Q_SIGNALS:
    // 历史列表任何变化（收到新条目、过滤、清空）都发，托盘徽标据此刷新。
    void transfersChanged();
    void localWriteRequested(const ClipEntry &entry);
    // 页脚「清空历史」。历史目前只在进程内，Go 侧不持有副本，
    // 所以清空是本地动作；但要不要真的丢，等接上持久化后要重新确认。
    void clearHistoryRequested();

private:
    QWidget *buildCurrent();
    QWidget *buildFilterRow();
    QWidget *buildHistory();
    QWidget *buildSettings();
    QWidget *badgeHost();
    QWidget *buildFooter();
    void rebuildHistory();
    // 历史条数上限取自 sync/clipboard_history（设置屏「保留最近 N 条」）。
    // 每收一条裁一次——界面上写着「保留最近 50 条」，就得真按 50 条裁，
    // 否则设置只是块好看的牌子。
    void trimHistory();
    // 预览正文按固定行数截断（设计稿是 max-height + 内部滚动，浮板里不能嵌套
    // 滚动区），所以文字要先按可用宽高省略再交给 QLabel。
    void setPreviewText(const QString &text);
    // 标题栏胶囊跟着 sync/clipboard_enabled 走：设置屏关掉同步后，这一屏
    // 顶上不能还写着「同步中」。
    void refreshSyncState();

    proto::ClipboardSession session_;
    QVBoxLayout *historyLayout_ = nullptr;
    QVBoxLayout *rowsLayout_ = nullptr;
    SegmentedControl *filter_ = nullptr;
    QLineEdit *search_ = nullptr;
    QLabel *preview_ = nullptr;
    QLabel *spec_ = nullptr;
    // 头部那颗 MIME 徽章跟着 current_ 走。设计稿上写死了 text/plain（示例数据），
    // 但这一屏是能拿到真实 MIME 的——收到一张图还挂着 text/plain 就是假话。
    Badge *mimeBadge_ = nullptr;
    QWidget *pill_ = nullptr;
    QLabel *pillLabel_ = nullptr;
    StatusDot *pillDot_ = nullptr;
    QList<ClipEntry> history_;
    ClipEntry current_;      // .preview 显示的那条，即历史首条
};

} // namespace fg

#endif // FLOWGRID_CLIPBOARDPANEL_H
