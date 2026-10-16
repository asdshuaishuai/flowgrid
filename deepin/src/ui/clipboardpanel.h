// 剪贴板浮板：对应 design/unified/screens/clipboard.html。
// 分段/重组/超时全部由 Go 的 ClipboardSession 负责（PROTOCOL.md §5.3），
// 本屏只做展示与「写入本地 / 推送」两个动作。
#ifndef FLOWGRID_CLIPBOARDPANEL_H
#define FLOWGRID_CLIPBOARDPANEL_H

#include "core/protocolapi.h"
#include "ui/panel.h"

class QVBoxLayout;
class Badge;

namespace fg {

struct ClipEntry {
    QString mime = QStringLiteral("text/plain");
    QString text;        // 预览用，超长截断
    QByteArray raw;      // 原始载荷，推送时用
    QString source;      // 「MacBook Pro」
    int agoSecs = 0;
    bool oversize = false;   // 超 63,240 B 上限，不入库（设计稿的「超限」）
};

class ClipboardPanel : public Panel {
    Q_OBJECT
public:
    explicit ClipboardPanel(QWidget *parent = nullptr);

    void reloadHistory();

public Q_SLOTS:
    void clearHistory();

    // 会话在传输层落地前是空壳，但接线方式已定：入站走 handleFrame，
    // 出站走 send。构造函数已建立会话句柄。
    proto::ClipboardSession &session() { return session_; }

Q_SIGNALS:
    void localWriteRequested(const ClipEntry &entry);
    void pushRequested(const ClipEntry &entry);
    // 页脚「清空历史」。历史目前只在进程内，Go 侧不持有副本，
    // 所以清空是本地动作；但要不要真的丢，等接上持久化后要重新确认。
    void clearHistoryRequested();

private:
    QWidget *buildCurrent();
    QWidget *buildHistory();
    QWidget *buildSettings();
    QWidget *buildFooter();
    void rebuildHistory();

    proto::ClipboardSession session_;
    QVBoxLayout *currentLayout_ = nullptr;
    QVBoxLayout *historyLayout_ = nullptr;
    QList<ClipEntry> history_;
};

} // namespace fg

#endif // FLOWGRID_CLIPBOARDPANEL_H
