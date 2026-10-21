#include "protocolapi.h"

extern "C" {
#include "libflowgrid_protocol.h"
}

#include <QString>

#include <utility>

namespace fg::proto {

// ── GoBuffer ──────────────────────────────────────────────────────────
GoBuffer &GoBuffer::operator=(GoBuffer &&o) noexcept
{
    if (this != &o) {
        if (data_ != nullptr) {
            fg_free_buffer(data_);
        }
        data_ = o.data_;
        size_ = o.size_;
        o.data_ = nullptr;
        o.size_ = 0;
    }
    return *this;
}

GoBuffer::~GoBuffer()
{
    if (data_ != nullptr) {
        // 所有权只在这里释放一次；GoBuffer 不可拷贝，移动后源指针被置空。
        fg_free_buffer(data_);
    }
}

QByteArray GoBuffer::toByteArray() const
{
    if (data_ == nullptr || size_ == 0) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(data_), static_cast<qsizetype>(size_));
}

// ── 帧编解码 ──────────────────────────────────────────────────────────
GoBuffer encodeFrame(std::uint8_t frameType, std::uint16_t sequence,
                     std::uint64_t timestamp, const QByteArray &payload)
{
    std::size_t outLen = 0;
    auto *raw = fg_frame_encode(
        frameType, sequence, timestamp,
        reinterpret_cast<std::uint8_t *>(const_cast<char *>(payload.constData())),
        static_cast<std::uint8_t>(payload.size()), &outLen);
    return GoBuffer(raw, outLen);
}

GoBuffer encodeFrameWithHmac(std::uint8_t frameType, std::uint16_t sequence,
                             std::uint64_t timestamp, const QByteArray &payload,
                             const QByteArray &hmacKey)
{
    std::size_t outLen = 0;
    auto *raw = fg_frame_encode_with_hmac(
        frameType, sequence, timestamp,
        reinterpret_cast<std::uint8_t *>(const_cast<char *>(payload.constData())),
        static_cast<std::uint8_t>(payload.size()),
        reinterpret_cast<std::uint8_t *>(const_cast<char *>(hmacKey.constData())),
        static_cast<std::size_t>(hmacKey.size()), &outLen);
    return GoBuffer(raw, outLen);
}

bool decodeFrame(const QByteArray &wire, Frame *out)
{
    if (out == nullptr || wire.isEmpty()) {
        return false;
    }
    std::uint8_t type = 0;
    std::uint16_t seq = 0;
    std::uint64_t ts = 0;
    std::uint8_t payloadLen = 0;
    std::uint8_t *payload = nullptr;
    const int rc = fg_frame_decode(
        reinterpret_cast<std::uint8_t *>(const_cast<char *>(wire.constData())),
        static_cast<std::size_t>(wire.size()), &type, &seq, &ts, &payloadLen, &payload);
    if (rc != 0) {
        return false;
    }
    // 载荷缓冲由 Go 侧 malloc，必须在本次调用内释放；GoBuffer 接管这个职责。
    GoBuffer owned(payload, payloadLen);
    out->type = type;
    out->sequence = seq;
    out->timestamp = ts;
    out->payload = owned.toByteArray();
    return true;
}

bool decodeFrameWithHmac(const QByteArray &wire, const QByteArray &hmacKey, Frame *out)
{
    if (out == nullptr || wire.isEmpty()) {
        return false;
    }
    std::uint8_t type = 0;
    std::uint16_t seq = 0;
    std::uint64_t ts = 0;
    std::uint8_t payloadLen = 0;
    std::uint8_t *payload = nullptr;
    const int rc = fg_frame_decode_with_hmac(
        reinterpret_cast<std::uint8_t *>(const_cast<char *>(wire.constData())),
        static_cast<std::size_t>(wire.size()),
        reinterpret_cast<std::uint8_t *>(const_cast<char *>(hmacKey.constData())),
        static_cast<std::size_t>(hmacKey.size()), &type, &seq, &ts, &payloadLen, &payload);
    if (rc != 0) {
        return false;
    }
    GoBuffer owned(payload, payloadLen);
    out->type = type;
    out->sequence = seq;
    out->timestamp = ts;
    out->payload = owned.toByteArray();
    return true;
}

// ── 载荷构造器 ────────────────────────────────────────────────────────
GoBuffer latencyPingPayload(std::uint64_t timestampUs)
{
    std::size_t outLen = 0;
    return GoBuffer(fg_payload_latency_ping(timestampUs, &outLen), outLen);
}

GoBuffer latencyPongPayload(std::uint64_t timestampUs)
{
    std::size_t outLen = 0;
    return GoBuffer(fg_payload_latency_pong(timestampUs, &outLen), outLen);
}

// ── HID 载荷（§5.3）────────────────────────────────────────────────────
GoBuffer keyDownPayload(std::uint16_t keyCode, std::uint8_t modifiers)
{
    std::size_t outLen = 0;
    return GoBuffer(fg_payload_keydown(keyCode, modifiers, &outLen), outLen);
}

GoBuffer keyUpPayload(std::uint16_t keyCode)
{
    std::size_t outLen = 0;
    return GoBuffer(fg_payload_keyup(keyCode, &outLen), outLen);
}

GoBuffer mouseMovePayload(std::int16_t deltaX, std::int16_t deltaY)
{
    std::size_t outLen = 0;
    return GoBuffer(fg_payload_mousemove(deltaX, deltaY, &outLen), outLen);
}

GoBuffer mouseButtonPayload(std::uint8_t buttonId, bool down)
{
    std::size_t outLen = 0;
    return GoBuffer(fg_payload_mousebtn(buttonId, down ? 1 : 0, &outLen), outLen);
}

GoBuffer scrollPayload(std::int16_t deltaY, std::int16_t deltaX)
{
    std::size_t outLen = 0;
    return GoBuffer(fg_payload_scroll(deltaY, deltaX, &outLen), outLen);
}

namespace {

std::uint8_t *payloadBytes(const QByteArray &payload)
{
    return reinterpret_cast<std::uint8_t *>(const_cast<char *>(payload.constData()));
}

} // namespace

bool parseKeyDown(const QByteArray &payload, KeyDownPayload *out)
{
    if (out == nullptr || payload.isEmpty()) {
        return false;
    }
    return fg_payload_unmarshal_keydown(
               payloadBytes(payload), static_cast<std::size_t>(payload.size()),
               &out->keyCode, &out->modifiers)
        == 0;
}

bool parseKeyUp(const QByteArray &payload, KeyUpPayload *out)
{
    if (out == nullptr || payload.isEmpty()) {
        return false;
    }
    return fg_payload_unmarshal_keyup(
               payloadBytes(payload), static_cast<std::size_t>(payload.size()),
               &out->keyCode)
        == 0;
}

bool parseMouseMove(const QByteArray &payload, MouseMovePayload *out)
{
    if (out == nullptr || payload.isEmpty()) {
        return false;
    }
    return fg_payload_unmarshal_mousemove(
               payloadBytes(payload), static_cast<std::size_t>(payload.size()),
               &out->deltaX, &out->deltaY)
        == 0;
}

bool parseMouseButton(const QByteArray &payload, MouseButtonPayload *out)
{
    if (out == nullptr || payload.isEmpty()) {
        return false;
    }
    std::uint8_t state = 0;
    if (fg_payload_unmarshal_mousebtn(
            payloadBytes(payload), static_cast<std::size_t>(payload.size()),
            &out->buttonId, &state)
        != 0) {
        return false;
    }
    out->down = state == 1;
    return true;
}

bool parseScroll(const QByteArray &payload, ScrollPayload *out)
{
    if (out == nullptr || payload.isEmpty()) {
        return false;
    }
    return fg_payload_unmarshal_scroll(
               payloadBytes(payload), static_cast<std::size_t>(payload.size()),
               &out->deltaY, &out->deltaX)
        == 0;
}

// ── LatencyMonitor ────────────────────────────────────────────────────
LatencyMonitor::LatencyMonitor(int maxSamples) : maxSamples_(maxSamples)
{
    reset();
}

LatencyMonitor::LatencyMonitor(LatencyMonitor &&o) noexcept
    : id_(std::exchange(o.id_, 0)), maxSamples_(o.maxSamples_)
{
}

LatencyMonitor &LatencyMonitor::operator=(LatencyMonitor &&o) noexcept
{
    if (this != &o) {
        release();
        id_ = std::exchange(o.id_, 0);
        maxSamples_ = o.maxSamples_;
    }
    return *this;
}

LatencyMonitor::~LatencyMonitor()
{
    // 只释放，绝不 reset：reset 会再建一个新句柄，析构里建就是每析构一个漏
    // 一个 Go 侧 monitor（DeviceManager::setSource 换设备时逐个 delete 即中招）。
    release();
}

void LatencyMonitor::release()
{
    if (id_ != 0) {
        fg_latency_monitor_free(id_);
        id_ = 0;
    }
}

void LatencyMonitor::reset()
{
    // reset = 「丢弃旧句柄、换一个新的」（句柄失效时用），与 release 不同。
    release();
    id_ = fg_latency_monitor_new(maxSamples_);
}

void LatencyMonitor::recordPing(std::uint64_t sendTimeUs)
{
    if (id_ != 0) {
        fg_latency_record_ping(id_, sendTimeUs);
    }
}

bool LatencyMonitor::recordPong(std::uint64_t pongTimestampUs, std::uint64_t nowUs,
                                double *outMs, double *outJitter)
{
    if (id_ == 0) {
        return false;
    }
    double ms = 0.0;
    double jitter = 0.0;
    if (fg_latency_record_pong(id_, pongTimestampUs, nowUs, &ms, &jitter) != 0) {
        return false;
    }
    if (outMs != nullptr) {
        *outMs = ms;
    }
    if (outJitter != nullptr) {
        *outJitter = jitter;
    }
    return true;
}

bool LatencyMonitor::latest(double *outMs, double *outJitter, int *outQuality) const
{
    if (id_ == 0) {
        return false;
    }
    double ms = 0.0;
    double jitter = 0.0;
    int quality = 0;
    if (fg_latency_latest(id_, &ms, &jitter, &quality) != 0) {
        return false;
    }
    if (outMs != nullptr) {
        *outMs = ms;
    }
    if (outJitter != nullptr) {
        *outJitter = jitter;
    }
    if (outQuality != nullptr) {
        *outQuality = quality;
    }
    return true;
}

int LatencyMonitor::consecutiveBad() const
{
    return id_ != 0 ? fg_latency_consecutive_bad(id_) : 0;
}

bool LatencyMonitor::stats(double *outMin, double *outMax, double *outAvg) const
{
    if (id_ == 0) {
        return false;
    }
    double mn = 0.0;
    double mx = 0.0;
    double avg = 0.0;
    if (fg_latency_stats(id_, &mn, &mx, &avg) != 0) {
        return false;
    }
    if (outMin != nullptr) {
        *outMin = mn;
    }
    if (outMax != nullptr) {
        *outMax = mx;
    }
    if (outAvg != nullptr) {
        *outAvg = avg;
    }
    return true;
}

// ── ClipboardSession ──────────────────────────────────────────────────
ClipboardSession::ClipboardSession() : id_(fg_clipboard_session_new()) {}

ClipboardSession::ClipboardSession(ClipboardSession &&o) noexcept : id_(std::exchange(o.id_, 0)) {}

ClipboardSession &ClipboardSession::operator=(ClipboardSession &&o) noexcept
{
    if (this != &o) {
        fg_clipboard_session_free(id_);
        id_ = std::exchange(o.id_, 0);
    }
    return *this;
}

ClipboardSession::~ClipboardSession()
{
    fg_clipboard_session_free(id_);
}

int ClipboardSession::send(const QString &mime, const QByteArray &data, int passes)
{
    const QByteArray mimeUtf8 = mime.toUtf8();
    return fg_clipboard_send(id_, const_cast<char *>(mimeUtf8.constData()),
                             reinterpret_cast<std::uint8_t *>(const_cast<char *>(data.constData())),
                             static_cast<size_t>(data.size()), passes);
}

int ClipboardSession::handleFrame(const QByteArray &payload)
{
    return fg_clipboard_handle_frame(id_,
                                     reinterpret_cast<std::uint8_t *>(const_cast<char *>(payload.constData())),
                                     static_cast<size_t>(payload.size()));
}

bool ClipboardSession::takePending(QString *mime, QByteArray *data)
{
    std::uint8_t *mimeBuf = nullptr;
    std::size_t mimeLen = 0;
    std::uint8_t *dataBuf = nullptr;
    std::size_t dataLen = 0;
    if (fg_clipboard_take_pending(id_, &mimeBuf, &mimeLen, &dataBuf, &dataLen) != 1) {
        return false;
    }
    // 两个缓冲都由 C.malloc 分配，各自恰好释放一次。
    GoBuffer ownedMime(mimeBuf, mimeLen);
    GoBuffer ownedData(dataBuf, dataLen);
    if (mime != nullptr) {
        *mime = QString::fromUtf8(ownedMime.toByteArray());
    }
    if (data != nullptr) {
        *data = ownedData.toByteArray();
    }
    return true;
}

// ── FileSender ────────────────────────────────────────────────────────
FileSender::FileSender() : id_(fg_file_sender_new()) {}

FileSender::FileSender(FileSender &&o) noexcept : id_(std::exchange(o.id_, 0)) {}

FileSender &FileSender::operator=(FileSender &&o) noexcept
{
    if (this != &o) {
        fg_file_sender_free(id_);
        id_ = std::exchange(o.id_, 0);
    }
    return *this;
}

FileSender::~FileSender()
{
    fg_file_sender_free(id_);
}

int FileSender::offer(const QString &fileName, const QByteArray &data, quint16 *outTransferId)
{
    const QByteArray nameUtf8 = fileName.toUtf8();
    std::uint16_t id = 0;
    const int rc = fg_file_sender_offer(id_, const_cast<char *>(nameUtf8.constData()),
                                        reinterpret_cast<std::uint8_t *>(const_cast<char *>(data.constData())),
                                        static_cast<size_t>(data.size()), &id);
    if (rc == 0 && outTransferId != nullptr) {
        *outTransferId = id;
    }
    return rc;
}

int FileSender::pump(int maxChunks, int *outPumped)
{
    int pumped = 0;
    const int rc = fg_file_sender_pump(id_, maxChunks, &pumped);
    if (outPumped != nullptr) {
        *outPumped = pumped;
    }
    return rc;
}

int FileSender::handleFrame(std::uint8_t frameType, const QByteArray &payload)
{
    return fg_file_sender_handle_frame(
        id_, frameType,
        reinterpret_cast<std::uint8_t *>(const_cast<char *>(payload.constData())),
        static_cast<size_t>(payload.size()));
}

bool FileSender::poll(State *outState, int *outSentBytes, int *outTotalBytes, int *outErrCode)
{
    int state = 0;
    int sent = 0;
    int total = 0;
    int code = 0;
    if (fg_file_sender_poll(id_, &state, &sent, &total, &code) != 0) {
        return false;
    }
    if (outState != nullptr) {
        *outState = static_cast<State>(state);
    }
    if (outSentBytes != nullptr) {
        *outSentBytes = sent;
    }
    if (outTotalBytes != nullptr) {
        *outTotalBytes = total;
    }
    if (outErrCode != nullptr) {
        *outErrCode = code;
    }
    return true;
}

// ── FileReceiver ──────────────────────────────────────────────────────
FileReceiver::FileReceiver() : id_(fg_file_receiver_new()) {}

FileReceiver::FileReceiver(FileReceiver &&o) noexcept : id_(std::exchange(o.id_, 0)) {}

FileReceiver &FileReceiver::operator=(FileReceiver &&o) noexcept
{
    if (this != &o) {
        fg_file_receiver_free(id_);
        id_ = std::exchange(o.id_, 0);
    }
    return *this;
}

FileReceiver::~FileReceiver()
{
    fg_file_receiver_free(id_);
}

int FileReceiver::handleFrame(std::uint8_t frameType, const QByteArray &payload, bool *outHandled)
{
    int handled = 0;
    const int rc = fg_file_receiver_handle_frame(
        id_, frameType,
        reinterpret_cast<std::uint8_t *>(const_cast<char *>(payload.constData())),
        static_cast<size_t>(payload.size()), &handled);
    if (outHandled != nullptr) {
        *outHandled = handled != 0;
    }
    return rc;
}

bool FileReceiver::takeOffer(FileOffer *out)
{
    std::uint16_t id = 0;
    std::uint8_t *nameBuf = nullptr;
    std::size_t nameLen = 0;
    std::uint64_t size = 0;
    if (fg_file_receiver_take_offer(id_, &id, &nameBuf, &nameLen, &size) != 1) {
        return false;
    }
    GoBuffer ownedName(nameBuf, nameLen);
    if (out != nullptr) {
        out->transferId = id;
        out->name = QString::fromUtf8(ownedName.toByteArray());
        out->size = size;
    }
    return true;
}

int FileReceiver::accept(quint16 transferId)
{
    return fg_file_receiver_accept(id_, transferId);
}

int FileReceiver::reject(quint16 transferId, std::uint8_t status)
{
    return fg_file_receiver_reject(id_, transferId, status);
}

bool FileReceiver::takeComplete(QString *outName, QByteArray *outData, int *outErrCode)
{
    std::uint8_t *nameBuf = nullptr;
    std::size_t nameLen = 0;
    std::uint8_t *dataBuf = nullptr;
    std::size_t dataLen = 0;
    int code = 0;
    if (fg_file_receiver_take_complete(id_, &nameBuf, &nameLen, &dataBuf, &dataLen, &code) != 1) {
        return false;
    }
    GoBuffer ownedName(nameBuf, nameLen);
    GoBuffer ownedData(dataBuf, dataLen);
    if (outName != nullptr) {
        *outName = QString::fromUtf8(ownedName.toByteArray());
    }
    if (outData != nullptr) {
        *outData = ownedData.toByteArray();
    }
    if (outErrCode != nullptr) {
        *outErrCode = code;
    }
    return true;
}

QString errorString(int code)
{
    // fg_error_string 返回的是 C.CString（malloc 出来的），必须自己释放——
    // 早先直接 fromUtf8 后就丢指针，每弹一次错误提示漏一小段。fg_free_buffer
    // 就是 C.free 的封装。
    char *s = fg_error_string(code);
    if (s == nullptr) {
        return QStringLiteral("未知错误 (%1)").arg(code);
    }
    const QString text = QString::fromUtf8(s);
    fg_free_buffer(s);
    return text;
}

// ── Identity ──────────────────────────────────────────────────────────
Identity::Identity()
{
    id_ = fg_lanid_identity_generate();
}

Identity::Identity(const QByteArray &privateKey)
{
    if (!privateKey.isEmpty()) {
        id_ = fg_lanid_identity_import(
            const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(privateKey.constData())),
            static_cast<size_t>(privateKey.size()));
    }
}

Identity::~Identity()
{
    release();
}

Identity::Identity(Identity &&o) noexcept : id_(std::exchange(o.id_, 0))
{
}

Identity &Identity::operator=(Identity &&o) noexcept
{
    if (this != &o) {
        release();
        id_ = std::exchange(o.id_, 0);
    }
    return *this;
}

void Identity::release()
{
    if (id_ != 0) {
        fg_lanid_identity_free(id_);
        id_ = 0;
    }
}

QByteArray Identity::exportPrivateKey() const
{
    if (id_ == 0) {
        return {};
    }
    size_t len = 0;
    uint8_t *buf = fg_lanid_identity_export(id_, &len);
    if (buf == nullptr) {
        return {};
    }
    GoBuffer g(buf, len);
    return g.toByteArray();
}

QByteArray Identity::publicKey() const
{
    if (id_ == 0) {
        return {};
    }
    size_t len = 0;
    uint8_t *buf = fg_lanid_identity_public_key(id_, &len);
    if (buf == nullptr) {
        return {};
    }
    GoBuffer g(buf, len);
    return g.toByteArray();
}

// ── Announcer ─────────────────────────────────────────────────────────
Announcer::Announcer(const Identity &identity, uint8_t platform, uint8_t caps,
                     uint16_t port, const QString &name)
{
    if (!identity.isValid()) {
        return;
    }
    const QByteArray nameBytes = name.toUtf8();
    id_ = fg_lanid_announcer_new(
        identity.id_, platform, caps, port,
        const_cast<char *>(nameBytes.constData()));
}

Announcer::~Announcer()
{
    release();
}

Announcer::Announcer(Announcer &&o) noexcept : id_(std::exchange(o.id_, 0))
{
}

Announcer &Announcer::operator=(Announcer &&o) noexcept
{
    if (this != &o) {
        release();
        id_ = std::exchange(o.id_, 0);
    }
    return *this;
}

void Announcer::release()
{
    if (id_ != 0) {
        fg_lanid_announcer_free(id_);
        id_ = 0;
    }
}

QByteArray Announcer::announce() const
{
    if (id_ == 0) {
        return {};
    }
    size_t len = 0;
    uint8_t *buf = fg_lanid_announcer_announce(id_, &len);
    if (buf == nullptr) {
        return {};
    }
    GoBuffer g(buf, len);
    return g.toByteArray();
}

// ── Listener ──────────────────────────────────────────────────────────
Listener::Listener()
{
    id_ = fg_lanid_listener_new();
}

Listener::~Listener()
{
    release();
}

Listener::Listener(Listener &&o) noexcept : id_(std::exchange(o.id_, 0))
{
}

Listener &Listener::operator=(Listener &&o) noexcept
{
    if (this != &o) {
        release();
        id_ = std::exchange(o.id_, 0);
    }
    return *this;
}

void Listener::release()
{
    if (id_ != 0) {
        fg_lanid_listener_free(id_);
        id_ = 0;
    }
}

void Listener::feed(const QByteArray &data, const QByteArray &fromIP)
{
    if (id_ == 0 || data.isEmpty()) {
        return;
    }
    fg_lanid_listener_feed(
        id_,
        const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(data.constData())),
        static_cast<size_t>(data.size()),
        const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(fromIP.constData())),
        static_cast<size_t>(fromIP.size()));
}

int Listener::peersCount() const
{
    if (id_ == 0) {
        return 0;
    }
    return fg_lanid_listener_peers_count(id_);
}

int Listener::prune(int maxAgeMs)
{
    if (id_ == 0) {
        return 0;
    }
    return fg_lanid_listener_prune(id_, static_cast<int64_t>(maxAgeMs));
}

bool Listener::peer(int index, LanIDPeer *out) const
{
    if (id_ == 0 || out == nullptr) {
        return false;
    }
    uint8_t buf[512];
    const int written = fg_lanid_listener_peer(id_, index, buf, sizeof(buf));
    if (written <= 0 || static_cast<size_t>(written) > sizeof(buf)) {
        return false;
    }

    const uint8_t *p = buf;
    const uint8_t *end = buf + written;

    // NameLen(1) + Name
    if (p >= end) return false;
    const int nameLen = *p++;
    if (p + nameLen > end) return false;
    out->name = QString::fromUtf8(reinterpret_cast<const char *>(p), nameLen);
    p += nameLen;

    // Platform(1) + Caps(1) + Port(2 BE)
    if (p + 4 > end) return false;
    out->platform = *p++;
    out->caps = *p++;
    out->port = static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
    p += 2;

    // AddrLen(1) + Addr
    if (p >= end) return false;
    const int addrLen = *p++;
    if (p + addrLen > end) return false;
    out->address = QByteArray(reinterpret_cast<const char *>(p), addrLen);
    p += addrLen;

    // PubKey(65)
    if (p + 65 > end) return false;
    out->publicKey = QByteArray(reinterpret_cast<const char *>(p), 65);
    p += 65;

    // Fingerprint(32 hex)
    if (p + 32 > end) return false;
    out->fingerprint = QString::fromUtf8(reinterpret_cast<const char *>(p), 32);

    return true;
}

// ── TrustChain ────────────────────────────────────────────────────────
TrustChain::TrustChain(const Identity &identity)
{
    if (identity.isValid()) {
        id_ = fg_lanid_trust_new(identity.id_);
    }
}

TrustChain::~TrustChain()
{
    release();
}

TrustChain::TrustChain(TrustChain &&o) noexcept : id_(std::exchange(o.id_, 0))
{
}

TrustChain &TrustChain::operator=(TrustChain &&o) noexcept
{
    if (this != &o) {
        release();
        id_ = std::exchange(o.id_, 0);
    }
    return *this;
}

void TrustChain::release()
{
    if (id_ != 0) {
        fg_lanid_trust_free(id_);
        id_ = 0;
    }
}

int TrustChain::append(const QByteArray &peerID, uint8_t decision)
{
    if (id_ == 0 || peerID.isEmpty()) {
        return -1;
    }
    return fg_lanid_trust_append(
        id_,
        const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(peerID.constData())),
        static_cast<size_t>(peerID.size()),
        decision);
}

bool TrustChain::isTrusted(const QByteArray &peerID) const
{
    if (id_ == 0 || peerID.isEmpty()) {
        return false;
    }
    const int rc = fg_lanid_trust_is_trusted(
        id_,
        const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(peerID.constData())),
        static_cast<size_t>(peerID.size()));
    return rc == 1;
}

bool TrustChain::verify() const
{
    if (id_ == 0) {
        return false;
    }
    return fg_lanid_trust_verify(id_) == 0;
}

int TrustChain::count() const
{
    if (id_ == 0) {
        return 0;
    }
    return fg_lanid_trust_count(id_);
}

} // namespace fg::proto
