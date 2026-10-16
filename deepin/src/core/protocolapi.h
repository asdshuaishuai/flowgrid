// 协议绑定层：FlowGrid 客户端与 Go 协议库之间唯一的接口。
//
// AGENTS.md 的硬约束是「客户端只能经 protocol/cgo 消费协议，禁止自行实现
// 帧编解码」，所以这里把 C ABI 全部包成 RAII，UI 层拿不到裸指针：
//   · cgo 返回的 uint8_t* 由 GoBuffer 持有，析构时恰好调用一次
//     fg_free_buffer（漏释放是泄漏，多释放是堆损坏）。
//   · cgo 的句柄表从 1 起，0 是失败哨兵，所以 LatencyMonitor 的 isValid()
//     等价于 id != 0，句柄失效一律走 reset 而不复用。
#ifndef FLOWGRID_PROTOCOL_API_H
#define FLOWGRID_PROTOCOL_API_H

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstdint>

namespace fg::proto {

// ── 帧类型：与 protocol/frame.go 一致 ─────────────────────────────────
namespace frametype {
inline constexpr std::uint8_t kKeyDown = 0x01;
inline constexpr std::uint8_t kKeyUp = 0x02;
inline constexpr std::uint8_t kMouseMove = 0x03;
inline constexpr std::uint8_t kMouseButton = 0x04;
inline constexpr std::uint8_t kScroll = 0x05;
inline constexpr std::uint8_t kClipboard = 0x07;
inline constexpr std::uint8_t kHeartbeat = 0x08;
inline constexpr std::uint8_t kLatencyPing = 0x09;
inline constexpr std::uint8_t kLatencyPong = 0x0A;
inline constexpr std::uint8_t kIdentify = 0x10;
inline constexpr std::uint8_t kIdentifyRes = 0x11;
} // namespace frametype

// cgo 返回的缓冲区所有权持有者：析构时恰好释放一次。
class GoBuffer {
public:
    GoBuffer() = default;
    explicit GoBuffer(std::uint8_t *data, std::size_t size) : data_(data), size_(size) {}

    // cgo 允许返回 nullptr（分配失败或零长载荷），两种情况都视为空但不算错。
    GoBuffer(const GoBuffer &) = delete;
    GoBuffer &operator=(const GoBuffer &) = delete;
    GoBuffer(GoBuffer &&o) noexcept : data_(o.data_), size_(o.size_) { o.data_ = nullptr; o.size_ = 0; }
    GoBuffer &operator=(GoBuffer &&o) noexcept;

    ~GoBuffer();

    bool isNull() const { return data_ == nullptr; }
    const std::uint8_t *data() const { return data_; }
    std::size_t size() const { return size_; }
    QByteArray toByteArray() const;

private:
    std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0;
};

// 解码出来的一帧。
struct Frame {
    std::uint8_t type = 0;
    std::uint16_t sequence = 0;
    std::uint64_t timestamp = 0;
    QByteArray payload;
};

// ── 帧编解码：全部转发 Go，客户端不碰字节序 ───────────────────────────
GoBuffer encodeFrame(std::uint8_t frameType, std::uint16_t sequence,
                     std::uint64_t timestamp, const QByteArray &payload);
GoBuffer encodeFrameWithHmac(std::uint8_t frameType, std::uint16_t sequence,
                             std::uint64_t timestamp, const QByteArray &payload,
                             const QByteArray &hmacKey);
bool decodeFrame(const QByteArray &wire, Frame *out);
bool decodeFrameWithHmac(const QByteArray &wire, const QByteArray &hmacKey, Frame *out);

// ── 载荷构造器 ────────────────────────────────────────────────────────
GoBuffer latencyPingPayload(std::uint64_t timestampUs);
GoBuffer latencyPongPayload(std::uint64_t timestampUs);

// ── 延迟监控：Go 侧维护采样环形缓冲，这里只做句柄与读数封装 ───────────
class LatencyMonitor {
public:
    // maxSamples 建议 32（设计稿「近 60 秒 / 每 2 秒采样」= 30 个点，留余量）。
    explicit LatencyMonitor(int maxSamples = 32);
    ~LatencyMonitor();

    LatencyMonitor(const LatencyMonitor &) = delete;
    LatencyMonitor &operator=(const LatencyMonitor &) = delete;
    LatencyMonitor(LatencyMonitor &&o) noexcept;
    LatencyMonitor &operator=(LatencyMonitor &&o) noexcept;

    bool isValid() const { return id_ != 0; }

    void recordPing(std::uint64_t sendTimeUs);
    // 返回 false 表示 pong 被 Go 侧判为未来/过期/不合理，属于正常噪声，调用方忽略即可。
    bool recordPong(std::uint64_t pongTimestampUs, std::uint64_t nowUs,
                    double *outMs, double *outJitter);
    bool latest(double *outMs, double *outJitter, int *outQuality) const;
    int consecutiveBad() const;
    bool stats(double *outMin, double *outMax, double *outAvg) const;

    void reset();

private:
    unsigned long long id_ = 0;
    int maxSamples_ = 32;
};

// ── 会话层：ClipboardSession / FileSender / FileReceiver ──────────────
//
// 设计上刻意不传回调函数指针：Go 侧的回调无法直接跨 C ABI，改成「Go 缓存
// 结果、C 侧轮询」。三个类的终态都是「读一次即清」，所以 UI 只需在自己的
// 定时器里 poll，不会重复弹提示。

// 剪贴板同步会话（PROTOCOL.md §5.3）。
class ClipboardSession {
public:
    ClipboardSession();
    ~ClipboardSession();

    ClipboardSession(const ClipboardSession &) = delete;
    ClipboardSession &operator=(const ClipboardSession &) = delete;
    ClipboardSession(ClipboardSession &&o) noexcept;
    ClipboardSession &operator=(ClipboardSession &&o) noexcept;

    bool isValid() const { return id_ != 0; }

    // 分段发送。passes<1 视作 1；返回 0 表示成功。
    int send(const QString &mime, const QByteArray &data, int passes = 1);

    // 喂一条入站 CLIPBOARD 载荷。返回 0 成功；stale 帧不会报错（协议层吞掉）。
    int handleFrame(const QByteArray &payload);

    // 取一份重组完成的剪贴板；没有则返回 false。
    // mime / data 均为拷贝，不涉及 Go 缓冲所有权。
    bool takePending(QString *mime, QByteArray *data);

private:
    unsigned long long id_ = 0;
};

// 文件发送方（PROTOCOL.md §7.4）。一次只能有一路传输。
class FileSender {
public:
    // state 取值：Idle / Streaming / Done / Failed
    enum class State { Idle = 0, Streaming = 1, Done = 2, Failed = 3 };

    FileSender();
    ~FileSender();

    FileSender(const FileSender &) = delete;
    FileSender &operator=(const FileSender &) = delete;
    FileSender(FileSender &&o) noexcept;
    FileSender &operator=(FileSender &&o) noexcept;

    bool isValid() const { return id_ != 0; }

    // 提议一个文件。fileName 与 data 会被协议层做 SHA-256 并留在内存里。
    int offer(const QString &fileName, const QByteArray &data, quint16 *outTransferId);

    // 推最多 maxChunks 个分块；由客户端在传输可写时调用。
    int pump(int maxChunks, int *outPumped);

    // 喂一条入站 FILE_ACK / FILE_ABORT。
    int handleFrame(std::uint8_t frameType, const QByteArray &payload);

    // 轮询进度。Done / Failed 是一次性的：读到后内部复位成 Idle。
    bool poll(State *outState, int *outSentBytes, int *outTotalBytes, int *outErrCode);

private:
    unsigned long long id_ = 0;
};

// 文件接收方（PROTOCOL.md §7.4）。
struct FileOffer {
    quint16 transferId = 0;
    QString name;
    quint64 size = 0;
};

class FileReceiver {
public:
    FileReceiver();
    ~FileReceiver();

    FileReceiver(const FileReceiver &) = delete;
    FileReceiver &operator=(const FileReceiver &) = delete;
    FileReceiver(FileReceiver &&o) noexcept;
    FileReceiver &operator=(FileReceiver &&o) noexcept;

    bool isValid() const { return id_ != 0; }

    // 喂一条入站帧。outHandled=true 表示该帧属于文件传输协议，
    // 调用方可据此不再往剪贴板等其它分发器里送。
    int handleFrame(std::uint8_t frameType, const QByteArray &payload, bool *outHandled);

    // 取一个待用户确认的 offer；没有则返回 false。
    bool takeOffer(FileOffer *out);

    int accept(quint16 transferId);
    int reject(quint16 transferId, std::uint8_t status);

    // 取一次终态结果（完成或失败）。errCode 非 0 表示失败，data 为空。
    bool takeComplete(QString *outName, QByteArray *outData, int *outErrCode);

private:
    unsigned long long id_ = 0;
};

// ── 错误码文本：诊断导出用 ────────────────────────────────────────────
QString errorString(int code);

} // namespace fg::proto

#endif // FLOWGRID_PROTOCOL_API_H
