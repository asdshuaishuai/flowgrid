// 键鼠转发通路：控制端捕获 → 协议层重映射 → 组帧 → 发送；接收端入站帧 → 解析 → 注入。
//
// 分工（AGENTS.md §29「能力在协议层，客户端只做 UI + HAL」）：
//   · 「识别一帧输入 + 跨平台重映射」是协议层能力（protocol.RemapCapturedKey /
//     DecodeInputFrame），本文件只调 protocolapi 里的入口，一行换算逻辑都不写；
//   · evdev 捕获 / uinput 注入是 OS HAL，在 input_engine 里；
//   · 帧的字节拼装由传输层做（encodeFrame 走 cgo），本文件只产出
//     「帧类型 + 载荷」，和 mac 的 send?(type, payload)、
//     Windows 的 SendToPeer(type, payload) 同一分工。
//
// 对应 mac 的 InputCaptureTap.handle() + 接收侧注入、对应 Windows 的
// hub.forward()：三端跑的是同一套协议层重映射，行为不会分叉。
#ifndef FLOWGRID_INPUT_FORWARDER_H
#define FLOWGRID_INPUT_FORWARDER_H

#include "input_engine.h"

#include "../protocolapi.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <set>

namespace fg::input {

// 一条待发出的线上帧：帧类型 + 载荷。
struct OutboundFrame {
    std::uint8_t frameType = 0;
    QByteArray payload;
};

class Forwarder {
public:
    using SendFn = std::function<void(const OutboundFrame &)>;

    // injector：接收端注入目标（可为 nullptr——只做控制端转发时不需要）。
    // send：把线上帧交给传输层；为空时 forwardCaptured 只算不发。
    Forwarder(Injector *injector, SendFn send);

    // 换发送通道（会话建立后才拿到通道时用）。
    void setSendFn(SendFn send) { send_ = std::move(send); }
    bool hasSendChannel() const { return static_cast<bool>(send_); }

    // 对端平台。跨平台时修饰键要按目标平台换算（§6.3 Ctrl⇄Meta）：
    // Linux→macOS 传 Ctrl，对端要收到 Cmd。
    void setPeerPlatform(std::uint8_t platform) { peerPlatform_ = platform; }
    std::uint8_t peerPlatform() const { return peerPlatform_; }

    // 键位规则表：可空 = 只走平台默认（mac 现状如此）。
    // 指针由调用方持有，本类不接管生命周期——规则热重载时调用方换指针即可。
    void setKeyMap(const proto::KeyMap *keymap) { keymap_ = keymap; }

    // 规则上下文：空 = 全局规则；应用上下文用 "app:<bundleID>"，游戏用 "game"。
    void setContext(const QString &context) { context_ = context; }

    // 控制端：一条捕获事件 → 重映射 → 载荷 → 发出。
    // 返回 false 表示这条事件没发出去（重映射失败或没有发送通道）。
    bool forwardCaptured(const CapturedEvent &event);

    // 接收端：一条入站帧 → 解析 → 注入。
    // 返回 false 表示这条帧不是输入帧或注入失败。
    bool handleInbound(std::uint8_t frameType, const QByteArray &payload);

    // 停接管前把还按着的键与鼠标按钮松开。
    //
    // 为什么必须做：对端在**另一台机器**上。我们不再转发 keyUp 的话，那边的
    // 键会一直停在按下态——本机这边松手了，对面还按着，典型症状是「远程
    // 机器上有个键卡住了」。
    void releasePressed();

    // 最近一次失败原因（诊断用；界面层只展示，不据此改行为）。
    QString lastError() const { return lastError_; }

private:
    Injector *injector_;
    SendFn send_;
    std::uint8_t peerPlatform_ = proto::platform::kLinux;
    const proto::KeyMap *keymap_ = nullptr;
    QString context_;
    QString lastError_;

    // 还按着的键/按钮（存的是**重映射后**的键码，松开时发出去的必须与
    // 按下时完全一致，否则对端会多出一个按下的键）。
    std::set<std::uint16_t> pressedKeys_;
    std::set<std::uint8_t> pressedButtons_;
};

} // namespace fg::input

#endif // FLOWGRID_INPUT_FORWARDER_H
