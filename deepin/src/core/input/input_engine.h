// Linux 键鼠软模拟引擎：uinput 注入 + evdev 捕获。
//
// 与协议层的关系：
//   · 捕获线程只产出 CapturedEvent（HID Usage + 修饰位），不碰帧编码；
//   · 注入方法只接受已经解析好的 HID 字段，不碰帧解码。
// 帧的编解码仍由调用方经 protocol/cgo 完成（AGENTS.md 的硬约束）。
//
// 线程模型：捕获在自己的线程里跑，回调也在该线程触发；注入可在任意线程
// 调用（内部串行化）。调用方要把回调结果送回 Qt 主线程，请自行
// QMetaObject::invokeMethod 排队。
#ifndef FLOWGRID_INPUT_ENGINE_H
#define FLOWGRID_INPUT_ENGINE_H

#include <linux/input.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fg::input {

enum class EventKind : std::uint8_t {
    KeyDown,
    KeyUp,
    MouseMove,
    MouseButton,
    Scroll,
};

// 与协议 §5.3 的 HID 字段一一对应；不含帧头/序号/时间戳。
struct CapturedEvent {
    EventKind kind = EventKind::KeyDown;
    std::uint16_t keyCode = 0; // HID Usage ID
    std::uint8_t modifiers = 0; // 协议 §6.2 位掩码（仅 KeyDown 有意义）
    std::int16_t deltaX = 0;
    std::int16_t deltaY = 0;
    std::uint8_t button = 0; // 协议 §6.4 按钮号
};

class Engine {
public:
    using Callback = std::function<void(const CapturedEvent &)>;

    Engine();
    ~Engine();

    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    // ── 注入（目标端）────────────────────────────────────────────────
    // 打开 /dev/uinput 并创建虚拟键盘/鼠标。需要 uinput 权限：
    // /etc/udev/rules.d/99-flowgrid-input.rules + 重新登录（见 deepin/README）。
    bool startInjection(std::string *error = nullptr);
    void stopInjection();
    bool injectionReady() const { return uinputFd_.load(std::memory_order_acquire) >= 0; }

    // modifiers 来自 KEY_DOWN 载荷；非修饰键按下时用它补齐修饰键，
    // 松开主键时释放「合成」的修饰键（与 Windows 注入侧同一套状态机）。
    bool injectKey(std::uint16_t hid, bool down, std::uint8_t modifiers);
    bool injectMouseMove(std::int16_t dx, std::int16_t dy);
    bool injectMouseButton(std::uint8_t button, bool down);
    // 单位是「格/行」（协议 P11 的结论），不是像素；evdev REL_WHEEL 原样写。
    bool injectScroll(std::int16_t deltaY, std::int16_t deltaX);
    // 松开本引擎注入且仍按着的键/鼠标键。会话断开或切换目标时必须调用：
    // uinput 设备不会因为“对端走了”自动收到 KEY_UP，不释放会一直卡住。
    void releaseAll();

    // ── 捕获（控制端）────────────────────────────────────────────────
    // grabDevices=true 时对能识别的键盘/鼠标做 EVIOCGRAB，事件不再落到本机，
    // 这是「控制端」的语义。抓不到独占权时会退化为「旁路监听」（本机也响应），
    // 但不会因此启动失败——权限不足时至少能跑，错误在 lastError() 里。
    bool startCapture(Callback callback, bool grabDevices, std::string *error = nullptr);
    void stopCapture();
    bool isCapturing() const { return captureRunning_.load(std::memory_order_acquire); }

    // 返回副本：捕获线程会写这条错误，UI 线程读时不能拿内部字符串的引用。
    std::string lastError() const;

private:
    struct CaptureDevice {
        int fd = -1;
        bool grabbed = false;
        bool keyboard = false;
        bool mouse = false;
    };

    bool setupUinput(std::string *error);
    bool writeAll(const struct input_event *events, std::size_t count);
    bool emitKey(std::uint16_t evdevCode, bool down);
    bool releaseSyntheticLocked();
    void captureLoop();
    void closeDevices();
    void setError(const std::string &message);

    std::atomic<int> uinputFd_{-1};
    mutable std::mutex injectMu_;
    std::uint8_t injectedMods_ = 0; // 当前按下的修饰位（真实 + 合成）
    std::uint8_t syntheticMods_ = 0; // 由 KEY_DOWN 掩码补按的位
    std::set<std::uint16_t> injectedKeys_; // 非修饰键的 HID，断线时补 keyUp
    std::uint8_t injectedButtons_ = 0; // bit0..bit4 = 按钮 1..5

    std::thread captureThread_;
    std::atomic<bool> captureRunning_{false};
    Callback callback_;
    std::vector<CaptureDevice> devices_;
    mutable std::mutex stateMu_;
    std::string lastError_;
};

} // namespace fg::input

#endif // FLOWGRID_INPUT_ENGINE_H
