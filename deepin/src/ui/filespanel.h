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

class QVBoxLayout;

namespace fg {

// 设计稿的边界值：1 B – 16 MiB，超限条目标「超限」而不发送
constexpr qint64 kMaxFileSize = 16LL * 1024 * 1024;
constexpr qint64 kMinFileSize = 1;

struct XferEntry {
    QString name;
    QString peer;
    bool inbound = false;
    // 传输中
    int sentBytes = 0;
    int totalBytes = 0;
    // 终态
    bool done = false;
    bool failed = false;
    bool rejected = false;    // 对端主动拒绝（与「失败」区分，徽章不同）
    bool verified = false;    // SHA-256 已校验
    qint64 finishedAtMs = 0;  // QDateTime::currentMSecsSinceEpoch
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
    int offerFile(const QString &path);

Q_SIGNALS:
    void fileChosen();

private:
    QWidget *buildTransferList();
    QWidget *buildDropZone();
    QWidget *buildRecords();
    QWidget *buildFooter();
    void rebuildTransfers();
    void rebuildRecords();

    proto::FileSender sender_;
    proto::FileReceiver receiver_;

    QVBoxLayout *transferLayout_ = nullptr;
    QVBoxLayout *recordsLayout_ = nullptr;
    QList<XferEntry> entries_;
};

} // namespace fg

#endif // FLOWGRID_FILESPANEL_H
