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
#include <QList>
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

// ── 错误码 §12.2（协议层）：与 protocol/errors.go 一致 ────────────────
// 只镜像协议层这一档：传输层与 HAL 的错误码归未来客户端的传输实现，会话层
// 不会拿它们做判断。镜像的理由同帧类型——UI 要按原因分档展示（对端拒绝、
// 校验不符、超时），而 cgo 只能取到字符串，取不出「是哪种失败」。
namespace errorcode {
inline constexpr int kHmacMismatch = 0x10;
inline constexpr int kUnknownFrameType = 0x11;
inline constexpr int kInvalidPayload = 0x12;
inline constexpr int kVersionMismatch = 0x13;
inline constexpr int kIdentifyRejected = 0x14;
inline constexpr int kTransferTimeout = 0x15;
inline constexpr int kTransferRejected = 0x16;
inline constexpr int kSequenceGap = 0x20;
} // namespace errorcode

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

// ── HID 载荷（§5.3）：键鼠软模拟的帧内容 ──────────────────────────────
// input HAL 只产出字段（HID/修饰位/位移），帧字节仍由这里经 Go 生成/解析。
GoBuffer keyDownPayload(std::uint16_t keyCode, std::uint8_t modifiers);
GoBuffer keyUpPayload(std::uint16_t keyCode);
GoBuffer mouseMovePayload(std::int16_t deltaX, std::int16_t deltaY);
GoBuffer mouseButtonPayload(std::uint8_t buttonId, bool down);
GoBuffer scrollPayload(std::int16_t deltaY, std::int16_t deltaX);

struct KeyDownPayload {
    std::uint16_t keyCode = 0;
    std::uint8_t modifiers = 0;
};
struct KeyUpPayload {
    std::uint16_t keyCode = 0;
};
struct MouseMovePayload {
    std::int16_t deltaX = 0;
    std::int16_t deltaY = 0;
};
struct MouseButtonPayload {
    std::uint8_t buttonId = 0;
    bool down = false;
};
struct ScrollPayload {
    std::int16_t deltaY = 0;
    std::int16_t deltaX = 0;
};

bool parseKeyDown(const QByteArray &payload, KeyDownPayload *out);
bool parseKeyUp(const QByteArray &payload, KeyUpPayload *out);
bool parseMouseMove(const QByteArray &payload, MouseMovePayload *out);
bool parseMouseButton(const QByteArray &payload, MouseButtonPayload *out);
bool parseScroll(const QByteArray &payload, ScrollPayload *out);

// ── 平台枚举（§3.1.2）：与 protocol/platform.go 同值 ──────────────────────
// 键鼠重映射要按「本机平台 → 对端平台」换算修饰键，两端的取值必须一致，
// 所以这里镜像协议层常量而不是各写各的字面量。
namespace platform {
inline constexpr std::uint8_t kMacOS = 0x01;
inline constexpr std::uint8_t kWindows = 0x02;
inline constexpr std::uint8_t kLinux = 0x03;
} // namespace platform

// ── 协议版本（§2.2）：与 protocol/frame.go 的 ProtocolVersion 同值 ────────
inline constexpr std::uint8_t kProtocolVersion = 1;

// ── 角色与能力位（§3.1.2 / §4.2）：与 protocol/platform.go 同值 ──────────
namespace role {
inline constexpr std::uint8_t kHost = 0x01;   // 键鼠发送端
inline constexpr std::uint8_t kTarget = 0x02; // 键鼠接收端
inline constexpr std::uint8_t kBoth = 0x03;   // 双向
} // namespace role

namespace caps {
inline constexpr std::uint8_t kIntelligentLink = 0x01;
inline constexpr std::uint8_t kDirectLink = 0x02;
inline constexpr std::uint8_t kWiFiDirect = 0x04;
inline constexpr std::uint8_t kDTLS13 = 0x08;
inline constexpr std::uint8_t kClipboard = 0x10;
inline constexpr std::uint8_t kFileTransfer = 0x20;
// deepin 客户端报的能力：智能连接 + 直连 + 剪贴板 + 文件传输。
inline constexpr std::uint8_t kDeepinDefault =
    kIntelligentLink | kDirectLink | kClipboard | kFileTransfer;
} // namespace caps

// ── 键位规则表（§11）：句柄 + 查询，规则本体在协议层的 YAML 解析里 ──────
//
// 客户端只持有句柄：规则文件的读盘、解析、匹配全在协议层
// （fg_keymap_load_file / fg_keymap_lookup）。句柄从 1 起，0 是失败哨兵，
// 所以 isValid() 等价于 handle_ != nullptr。
class KeyMap {
public:
    // path 指向 YAML 规则文件；文件不存在或解析失败时对象不可用，
    // lastError() 给出原因——规则坏了不能静默按默认值跑（那等于用户
    // 配的映射悄悄失效）。
    explicit KeyMap(const QString &path);

    // 从**内存里的字节**载入。规则不一定在磁盘上（打进可执行文件的资源、
    // 网络下载的……），但解析只有一份，在协议层。
    static KeyMap fromData(const QByteArray &yaml);
    ~KeyMap();

    KeyMap(const KeyMap &) = delete;
    KeyMap &operator=(const KeyMap &) = delete;
    KeyMap(KeyMap &&o) noexcept;
    KeyMap &operator=(KeyMap &&o) noexcept;

    bool isValid() const { return handle_ != nullptr; }
    // 裸句柄：只有需要直接调 cgo 的地方才用（remapCapturedKey）。
    void *handle() const { return handle_; }
    QString version() const { return version_; }
    int ruleCount() const { return ruleCount_; }
    QString lastError() const { return lastError_; }

    // context 传空 = 全局规则；应用上下文用 "app:<bundleID>"，游戏用 "game"。
    bool lookup(std::uint8_t fromPlatform, std::uint8_t toPlatform,
                std::uint16_t keyCode, std::uint8_t modifiers,
                const QString &context, std::uint16_t *outKeyCode,
                std::uint8_t *outModifiers) const;

    // 一条规则（界面要显示**真实的规则表**，不是只回答「查某个键会变成什么」）。
    struct Rule {
        std::uint8_t fromPlatform = 0;
        std::uint8_t toPlatform = 0;
        std::uint16_t fromKey = 0;     // HID Usage ID
        std::uint16_t toKey = 0;       // HID Usage ID
        std::uint8_t modifiers = 0;
        QString context;
    };
    // 取第 index 条。下标越界返回 false。
    bool ruleAt(int index, Rule *out) const;

    // 一条冲突：规则把某个键映到了**目标平台的系统修饰键**上（macOS 的 ⌘/⌥、
    // Linux 的 Super、Windows 的 Win）。目标机上「修饰键 + 其它键」的组合会被
    // 系统截走——Linux 的 Ctrl+Tab 到 macOS 变成 Cmd+Tab = 切应用。
    //
    // 判定在协议层做（fg_keymap_conflict_count / fg_keymap_conflict_at），
    // 客户端只负责把结果画出来。
    struct Conflict {
        std::uint8_t fromPlatform = 0;
        std::uint16_t fromKey = 0;
        std::uint8_t toPlatform = 0;
        std::uint16_t toKey = 0;
    };
    QList<Conflict> conflicts() const;

private:
    // 无参构造：给 fromData 用（它拿不到路径，只有字节）。
    KeyMap() = default;
    void release();

    void *handle_ = nullptr;
    QString version_;
    int ruleCount_ = 0;
    QString lastError_;
};

// ── 键鼠发送侧重映射（§6.3 / §11）──────────────────────────────────────
//
// 能力在协议层（protocol.RemapCapturedKey，经 cgo fg_input_remap_key）：
// 掩码先按规则感知换算、主键再走显式规则（未命中才用平台默认 Ctrl⇄Meta），
// 保证「按的是 Alt、掩码却说 Ctrl」不会发生。客户端只调这一个入口，
// 禁止自己再写一份跨平台修饰键换算（AGENTS.md §29）。
//
// keymap 传 nullptr = 只用平台默认（无用户规则）；context 传空 = 全局规则。
bool remapCapturedKey(std::uint16_t hidKey, std::uint8_t modifiers,
                       std::uint8_t fromPlatform, std::uint8_t toPlatform,
                       const KeyMap *keymap, const QString &context,
                       std::uint16_t *outKey, std::uint8_t *outModifiers);

// ── 输入操作编码（「事件 → 线上帧」归协议层）───────────────────────────
//
// 客户端只做「OS 原生事件 ↔ 协议模型」的翻译：帧类型选哪个、载荷怎么拼、
// 键位怎么重映射，全部经协议层（fg_input_encode → protocol.EncodeInput）。
// 各端各写一份 switch 正是行为分叉的根源——协议层的黄金向量
// （protocol/testdata/input_vectors.json）钉的就是这段语义。
//
// kind 传**帧类型**常量（key_down=0x01 …）：协议层里操作类型与帧类型一一
// 对应且同值，客户端因此不必再镜像一套「操作类型」常量。
//
// outKey / outModifiers 回填**重映射之后**的键位与掩码：客户端要记住它们，
// 停接管时才能把还按着的键松开（对端在另一台机器上，keyUp 不发出去那边会卡住）。
GoBuffer encodeInput(std::uint8_t kind, std::uint16_t key, std::uint8_t modifiers,
                      std::int16_t deltaX, std::int16_t deltaY, std::uint8_t button,
                      bool pressed, std::uint8_t fromPlatform, std::uint8_t toPlatform,
                      const KeyMap *keymap, const QString &context,
                      std::uint8_t *outFrameType, std::uint16_t *outKey,
                      std::uint8_t *outModifiers);

// ── IDENTIFY 握手载荷（§4.2）────────────────────────────────────────────
// version 传 kProtocolVersion；role 取 role:: 里的值。
GoBuffer identifyPayload(std::uint8_t platform, std::uint8_t role, std::uint8_t caps,
                         std::uint32_t seqInit, const QString &name,
                         std::uint8_t version = kProtocolVersion);
GoBuffer identifyResponsePayload(std::uint8_t status, std::uint8_t platform,
                                 std::uint8_t caps, const QString &name);

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
    // 只释放句柄，不重建。析构与移动赋值要的是这个——把它们接到 reset()
    // 上会在对象将死时又造一个新句柄，等于泄漏。
    void release();

    unsigned long long id_ = 0;
    int maxSamples_ = 32;
};

// ── 会话层：ClipboardSession / FileSender / FileReceiver ──────────────
//
// 设计上刻意不传回调函数指针：Go 侧的回调无法直接跨 C ABI，改成「Go 缓存
// 结果、C 侧轮询」。事件队列有界（剪贴板 200 条、文件事件各 4 条），终态事件
// 仍是「读一次即清」；进度轮询（FileSender::poll / FileReceiver::poll）只读，
// UI 可以每帧调用。

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

    // 待取队列积压条数 / 因队列满被挤掉的累计条数。drops>0 说明轮询太慢。
    int pendingCount() const;
    int droppedCount() const;

    // 只读历史：最近 50 条，index=0 为最旧；同一索引可反复读，也不影响
    // takePending。seq 单调递增，receivedAtMs 是重组完成时的 Unix 毫秒时间。
    int historyCount() const;
    bool historyAt(int index, QString *mime, QByteArray *data,
                   std::uint64_t *seq = nullptr, std::int64_t *receivedAtMs = nullptr) const;
    void clearHistory();

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
    // 与 FileSender::State 同序，UI 可共用一套进度渲染。
    enum class State { Idle = 0, Streaming = 1, Done = 2, Failed = 3 };

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

    // 入站进度，非破坏性：可每帧调用；终态仍由 takeComplete 消费。
    bool poll(State *outState, int *outReceivedBytes, int *outTotalBytes, int *outErrCode);

    // offer / 完成事件因队列上限被挤掉的累计条数。任意非 0 表示客户端
    // drain 太慢，完成事件丢失会连带丢掉文件数据。
    bool droppedCounts(int *outOffers, int *outCompletes) const;

private:
    unsigned long long id_ = 0;
};

// ── 智能连接（LAN 私有链，§3.1） ──────────────────────────────────────

// 持久身份密钥对（ECDSA P-256，签名用；与 §4.3 临时 ECDH 密钥分离）。
class Identity {
public:
    Identity();                                    // 生成新密钥对
    explicit Identity(const QByteArray &privKey);  // 从导出的私钥恢复
    ~Identity();

    Identity(const Identity &) = delete;
    Identity &operator=(const Identity &) = delete;
    Identity(Identity &&o) noexcept;
    Identity &operator=(Identity &&o) noexcept;

    bool isValid() const { return id_ != 0; }

    QByteArray exportPrivateKey() const;  // SEC1 EC 私钥（客户端安全存储）
    QByteArray publicKey() const;         // 65 字节 SEC1 公钥

private:
    friend class Announcer;
    friend class TrustChain;
    void release();
    unsigned long long id_ = 0;
};

// 签名通告器（纯逻辑：Announce 产生字节，客户端经 UDP 多播发送）。
class Announcer {
public:
    Announcer(const Identity &identity, std::uint8_t platform, std::uint8_t caps,
              std::uint16_t port, const QString &name);
    ~Announcer();

    Announcer(const Announcer &) = delete;
    Announcer &operator=(const Announcer &) = delete;
    Announcer(Announcer &&o) noexcept;
    Announcer &operator=(Announcer &&o) noexcept;

    bool isValid() const { return id_ != 0; }

    QByteArray announce() const;  // 签名通告字节

private:
    void release();
    unsigned long long id_ = 0;
};

// 识别到的对端。
struct LanIDPeer {
    QString name;
    std::uint8_t platform = 0;
    std::uint8_t caps = 0;
    QByteArray address;    // 原始 IP 字节（4 = IPv4）
    std::uint16_t port = 0;
    QByteArray publicKey;  // 65 字节
    QString fingerprint;   // 32 hex
};

// 监听器（纯逻辑：Feed 喂字节，peer/peersCount 取结果）。
class Listener {
public:
    Listener();
    ~Listener();

    Listener(const Listener &) = delete;
    Listener &operator=(const Listener &) = delete;
    Listener(Listener &&o) noexcept;
    Listener &operator=(Listener &&o) noexcept;

    bool isValid() const { return id_ != 0; }

    void feed(const QByteArray &data, const QByteArray &fromIP);
    int peersCount() const;
    bool peer(int index, LanIDPeer *out) const;
    int prune(int maxAgeMs);

private:
    void release();
    unsigned long long id_ = 0;
};

// 私有链信任（局域网私有区块链）。
class TrustChain {
public:
    explicit TrustChain(const Identity &identity);
    ~TrustChain();

    TrustChain(const TrustChain &) = delete;
    TrustChain &operator=(const TrustChain &) = delete;
    TrustChain(TrustChain &&o) noexcept;
    TrustChain &operator=(TrustChain &&o) noexcept;

    bool isValid() const { return id_ != 0; }

    int append(const QByteArray &peerID, std::uint8_t decision);  // 0=trust, 1=reject
    bool isTrusted(const QByteArray &peerID) const;
    bool verify() const;
    int count() const;

private:
    void release();
    unsigned long long id_ = 0;
};

// ── 错误码文本：诊断导出用 ────────────────────────────────────────────
QString errorString(int code);

} // namespace fg::proto

#endif // FLOWGRID_PROTOCOL_API_H
