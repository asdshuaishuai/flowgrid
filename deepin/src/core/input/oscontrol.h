// OS 控制层（客户端侧的薄壳）：捕获 / 注入都归协议层的 oscontrol 流水线。
//
// 分工（AGENTS.md §29「能力在协议层，客户端只做 UI + HAL」）：
//   · 「捕获事件 → 跨平台重映射 → 组帧」整条在协议层
//     （protocol/oscontrol + protocol/oscontrol/evdev）。本类拿到的已经是
//     **成品帧**，不是 OS 事件——它不写键位换算、不拼载荷、不管修饰键合成；
//   · 「入站帧 → 解码 → 注入」也在协议层（DecodeInput → evdev 注入）。
//     本类只把帧递进去；
//   · 本类只做三件事：起停捕获线程、把产出的帧交给上层、把协议层的错误
//     翻译成人话。
//
// 换句话说：C++ 这一侧**没有任何键鼠语义**，只有线程与搬运。
//
// 线程：捕获线程取帧 → 回调 onFrame → 上层排队回主线程再发。
// QTcpSocket 不是线程安全的，捕获线程直接 write 会出事，所以那一跳必须走
// 事件循环队列。
#ifndef FLOWGRID_OSCONTROL_H
#define FLOWGRID_OSCONTROL_H

#include "../protocolapi.h"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace fg::input {

class OsControl {
public:
    // 一条待发的线上帧：帧类型 + 载荷。
    //
    // 帧类型就是协议层的帧类型常量（§5.3）：客户端不另设一套「操作类型」。
    using FrameFn = std::function<void(std::uint8_t frameType, const QByteArray &payload)>;

    OsControl();
    ~OsControl();

    OsControl(const OsControl &) = delete;
    OsControl &operator=(const OsControl &) = delete;

    bool isValid() const { return handle_ != 0; }

    // 对端平台与键位规则：决定重映射的方向（§6.3 Ctrl⇄Meta）与用户规则。
    //
    // 必须在 startCapture() **之前**设：重映射在协议层按对端平台算，捕获
    // 一开就是按那时的对端平台换算的。
    //
    // keymap 传 nullptr = 只用平台默认换算；context 传空 = 全局规则。
    bool setPeer(std::uint8_t peerPlatform, const proto::KeyMap *keymap,
                 const QString &context);

    // 开始捕获。onFrame 在**捕获线程**上被调用；上层要送上网线就自己排队
    // 回主线程。error 非空时写入失败原因。
    bool startCapture(FrameFn onFrame, QString *error = nullptr);

    // 停止捕获。
    //
    // 做两件事，顺序不能反：
    //   1. 停捕获线程（不再有新的帧）；
    //   2. 让协议层把「还按着的键」松开——它会产出 keyUp 帧，这些帧照样
    //      经 onFrame 送出去。
    //
    // 第 2 步不能省：对端在**另一台机器**上。我们不再发松开帧的话，那边的
    // 键会一直停在按下态——典型症状是「远程机器上有个键卡住了」。
    void stopCapture();

    // 注入侧：打开 /dev/uinput 造出虚拟键鼠。需要 uinput 权限。
    bool startInjection(QString *error = nullptr);
    void stopInjection();

    // 喂一帧入站输入帧：协议层解码 → 注入本机。
    //
    // 非输入帧（剪贴板、文件、心跳）会失败——那是正常情况，不是错误。
    bool injectFrame(std::uint8_t frameType, const QByteArray &payload);

    // 最近一次失败的说明（协议层给的原文）。
    QString lastError() const;

private:
    void captureLoop();
    // 把队列里剩下的帧取干净（停接管时用来把松键的 keyUp 送出去）。
    bool drainFrames();

    std::uintptr_t handle_ = 0;
    FrameFn onFrame_;
    std::thread captureThread_;
    std::atomic<bool> captureRunning_{false};
    mutable std::mutex stateMu_;
    QString lastError_;
};

} // namespace fg::input

#endif // FLOWGRID_OSCONTROL_H
