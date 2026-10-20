// 文件传输浮板：对应 design/unified/screens/files.html。
//
// 发送/接收状态机全部在 Go 的 FileSender / FileReceiver（PROTOCOL.md §7.4），
// 本屏只轮询进度并展示；UI 不碰帧结构，也不自己算校验和。
//
// 协议一次只允许一路传输（FileSender 只有一个活动事务），所以拖放区一次只
// 取第一个文件，多选时在日志里点明而不是静默丢弃。
#ifndef FLOWGRID_FILESPANEL_H
#define FLOWGRID_FILESPANEL_H

#include "core/protocolapi.h"
#include "ui/panel.h"

#include <QList>
#include <QStringList>

class QLabel;
class QVBoxLayout;

namespace fg {
class StatusDot;

// 设计稿的边界值：1 B – 16 MiB，超限条目标「超限」而不发送
constexpr qint64 kMaxFileSize = 16LL * 1024 * 1024;
constexpr qint64 kMinFileSize = 1;

struct XferEntry {
    QString name;
    QString peer;
    bool inbound = false;
    // 传输中。刻意用 qint64 而不是 int：超限判定要在读完全部字节之后做，
    // >2 GiB 的文件若按 int 存会静默截断，记录里的大小就成了错的数
    // （设计稿传输记录里那条 4.2 GB 的 ISO 就是这一档）。
    qint64 sentBytes = 0;
    qint64 totalBytes = 0;
    qint64 startedAtMs = 0;   // 算平均速率与剩余时间用；0 表示刚创建还没起跑
    // 收到过 FILE_ABORT / 提议被拒，与 failed 分开：徽章与文案不同。
    bool oversize = false;
    // 终态
    bool done = false;
    bool failed = false;
    bool rejected = false;    // 对端主动拒绝（与「失败」区分，徽章不同）
    bool verified = false;    // SHA-256 已校验
    // 失败原因（协议 errCode §12.2，0 = 没有错误码）。徽章与说明按它分档：
    // 只有 0x12 才是校验/载荷问题，把超时、对端拒绝都说成「SHA-256 不符，
    // 未交付」，等于告诉用户文件坏了。
    int errCode = 0;
    qint64 finishedAtMs = 0;  // QDateTime::currentMSecsSinceEpoch
};

// 连接屏「文件传输」折叠区要的只读摘要。整份 XferEntry 暴露出去等于让第二个
// 面板也能去改传输状态，所以只给一份拷贝出来的快照 + 一个进度百分比。
struct XferSummary {
    QString name;
    QString peer;
    bool inbound = false;
    int pct = 0;
    qint64 totalBytes = 0;
    int agoSecs = 0;
    enum class State { Active, Done, Rejected, Failed, Oversize };
    State state = State::Active;
};

// 拖放区。mouseReleaseEvent / dropEvent 都是 QWidget 的 protected 虚函数，
// 没法用 QObject::connect 取地址，因此单独派生一个带信号的小控件。
class DropZone : public QWidget {
    Q_OBJECT
public:
    // Qt6 已移除 QWidget::setContentLayout（Qt5 的 protected 成员），
    // 直接 new QVBoxLayout(parent) 即可接管布局。
    explicit DropZone(QWidget *parent = nullptr);

    // 静止态样式（描边 line-2、底 surface-2）。拖拽结束后需要复位，但不能靠
    // leaveEvent(nullptr) —— 基类实现会解引用事件指针。
    void applyRestingStyle();

Q_SIGNALS:
    void clicked();
    void filesDropped(const QStringList &paths);

protected:
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
};

class FilesPanel : public Panel {
    Q_OBJECT
public:
    explicit FilesPanel(QWidget *parent = nullptr);

    // 从协议会话拉一次状态。传输层落地前列表为空，界面结构照常呈现。
    void pollSessions();

    // 送出一个本地文件；走 Go 的 FileSender，UI 不碰帧结构。
    //
    // 没有已连接的对端时**不建记录**：早先这里不看对端，直接建一条「进行中」
    // 再提示「已开始发送」——文件哪儿也没去，记录永远卡在「进行中」，托盘徽标
    // 也就永远显示「1 项进行中」。现在如实提示「没有已连接的对端」。
    int offerFile(const QString &path);

    // 托盘徽标用的进行中路数（只读）。
    int activeCount() const;

    // 只读快照，供连接屏那一区展示（传输本身仍只归本面板所有）。
    QList<XferSummary> transferSummary() const;

Q_SIGNALS:
    void fileChosen();
    // 列表有任何变化就发，连接屏据此重建那一区。
    void transfersChanged();

private:
    QWidget *buildTransferList();
    QWidget *buildDropZone();
    QWidget *buildRecords();
    QWidget *buildSettings();
    QWidget *buildFooter();
    // 标题栏胶囊写着「N 项进行中」，条目状态一变就要跟着改。
    void refreshPill();
    void rebuildTransfers();
    void rebuildRecords();

    proto::FileSender sender_;
    proto::FileReceiver receiver_;

    // 外层装分组标题，内层装条目行：重建只清内层，标题留在原位
    // （设计稿的 .list__group-title 是卡片里的固定头一行）。
    QVBoxLayout *transferLayout_ = nullptr;
    QVBoxLayout *transferRows_ = nullptr;
    QVBoxLayout *recordsLayout_ = nullptr;
    QVBoxLayout *recordRows_ = nullptr;
    QList<XferEntry> entries_;
    QWidget *pill_ = nullptr;
    QLabel *pillLabel_ = nullptr;
    StatusDot *pillDot_ = nullptr;
};

} // namespace fg

#endif // FLOWGRID_FILESPANEL_H
