#include "oscontrol.h"

#include <libflowgrid_protocol.h>

#include <utility>

namespace fg::input {

namespace {

// 取一帧的等待上限（毫秒）。
//
// 为什么要有上限：捕获线程得能及时发现「该退出了」。20ms 的轮询间隔在
// 键鼠场景下完全察觉不到（人眼分辨不出 20ms），而退出延迟被它框住。
constexpr int kPollTimeoutMs = 20;

// 读协议层最近一次失败的说明。
QString readError(std::uintptr_t handle)
{
    char buffer[512] = {0};
    fg_oscontrol_last_error(handle, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

} // namespace

OsControl::OsControl()
{
    handle_ = fg_oscontrol_new(proto::platform::kLinux);
    if (handle_ == 0) {
        lastError_ = QStringLiteral("协议层没有可用的 Linux OS 控制实现");
    }
}

OsControl::~OsControl()
{
    stopCapture();
    if (handle_ != 0) {
        fg_oscontrol_free(handle_);
        handle_ = 0;
    }
}

bool OsControl::setPeer(std::uint8_t peerPlatform, const proto::KeyMap *keymap,
                        const QString &context)
{
    if (handle_ == 0) {
        return false;
    }
    void *table = (keymap != nullptr && keymap->isValid()) ? keymap->handle() : nullptr;
    QByteArray ctx = context.toUtf8();
    // 传可写的指针：cgo 生成的头里没有 const 限定（Go 侧只读，不改它）。
    if (fg_oscontrol_set_peer(handle_, peerPlatform, table, ctx.data()) != 0) {
        std::lock_guard<std::mutex> lock(stateMu_);
        lastError_ = readError(handle_);
        return false;
    }
    return true;
}

bool OsControl::startCapture(FrameFn onFrame, QString *error)
{
    if (handle_ == 0) {
        if (error != nullptr) {
            *error = lastError_;
        }
        return false;
    }
    if (captureRunning_.load(std::memory_order_acquire)) {
        return true;
    }
    onFrame_ = std::move(onFrame);
    if (fg_oscontrol_start_capture(handle_) != 0) {
        std::lock_guard<std::mutex> lock(stateMu_);
        lastError_ = readError(handle_);
        if (error != nullptr) {
            *error = lastError_;
        }
        onFrame_ = nullptr;
        return false;
    }
    captureRunning_.store(true, std::memory_order_release);
    captureThread_ = std::thread([this] { captureLoop(); });
    return true;
}

void OsControl::stopCapture()
{
    // 先让线程知道该退了，再 join：线程此刻可能正阻塞在取帧上，最多等一个
    // 轮询间隔。
    captureRunning_.store(false, std::memory_order_release);
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    if (handle_ == 0) {
        return;
    }
    // 松开还按着的键：协议层把 keyUp 帧塞回队列，这里取出来送出去。
    //
    // 顺序要紧：先停捕获（不再有新帧），再松键（keyUp 进队列），最后排空
    // 队列。反过来的话 keyUp 会被后面新来的帧挤在后面，而那时捕获已经
    // 停了、没人再取。
    fg_oscontrol_release_captured(handle_);
    drainFrames();
}

void OsControl::captureLoop()
{
    while (captureRunning_.load(std::memory_order_acquire)) {
        std::uint8_t frameType = 0;
        std::uint8_t *payload = nullptr;
        std::size_t length = 0;
        const int rc = fg_oscontrol_next_frame(handle_, kPollTimeoutMs, &frameType, &payload,
                                               &length);
        if (rc < 0) {
            break;
        }
        if (rc == 0) {
            continue;   // 这一轮没有新帧
        }
        QByteArray data(reinterpret_cast<const char *>(payload), static_cast<int>(length));
        fg_free_buffer(payload);
        if (onFrame_) {
            onFrame_(frameType, data);
        }
    }
}

bool OsControl::drainFrames()
{
    bool any = false;
    for (;;) {
        std::uint8_t frameType = 0;
        std::uint8_t *payload = nullptr;
        std::size_t length = 0;
        const int rc = fg_oscontrol_next_frame(handle_, 0, &frameType, &payload, &length);
        if (rc <= 0) {
            break;
        }
        QByteArray data(reinterpret_cast<const char *>(payload), static_cast<int>(length));
        fg_free_buffer(payload);
        if (onFrame_) {
            onFrame_(frameType, data);
        }
        any = true;
    }
    return any;
}

bool OsControl::startInjection(QString *error)
{
    if (handle_ == 0) {
        if (error != nullptr) {
            *error = lastError_;
        }
        return false;
    }
    if (fg_oscontrol_start_injection(handle_) != 0) {
        std::lock_guard<std::mutex> lock(stateMu_);
        lastError_ = readError(handle_);
        if (error != nullptr) {
            *error = lastError_;
        }
        return false;
    }
    return true;
}

void OsControl::stopInjection()
{
    if (handle_ == 0) {
        return;
    }
    // 先松开本机还按着的键，再停注入：uinput 设备一销毁，之前注入的
    // keyDown 就永远没有对应的 keyUp 了，目标机上那个键会一直卡着。
    fg_oscontrol_stop_injection(handle_);
}

bool OsControl::injectFrame(std::uint8_t frameType, const QByteArray &payload)
{
    if (handle_ == 0) {
        return false;
    }
    // 去 const 只是为了对上 cgo 生成的签名（cgo 的头里没有 const 限定）：
    // Go 侧只读——C.GoBytes 会把这段字节拷走，它不会写。
    auto *bytes = const_cast<std::uint8_t *>(
        reinterpret_cast<const std::uint8_t *>(payload.constData()));
    return fg_oscontrol_inject_frame(handle_, frameType, bytes,
                                     static_cast<std::size_t>(payload.size())) == 0;
}

QString OsControl::lastError() const
{
    std::lock_guard<std::mutex> lock(stateMu_);
    return lastError_;
}

} // namespace fg::input
