#include "input_engine.h"

#include "input_keymap.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <dirent.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace fg::input {
namespace {

constexpr char kDeviceName[] = "FlowGrid Virtual Input";
constexpr int kPollTimeoutMs = 5;
constexpr auto kMoveFlushInterval = std::chrono::milliseconds(5);
constexpr std::size_t kReadEvents = 64;

bool bitSet(const std::uint8_t *bits, std::size_t bytes, int bit) {
    const auto index = static_cast<std::size_t>(bit);
    if (index / 8 >= bytes) return false;
    return (bits[index / 8] & (1u << (index % 8))) != 0;
}

std::string errnoText(const char *what) {
    return std::string(what) + ": " + std::strerror(errno);
}

std::uint16_t modifierEvdev(std::uint8_t bit) {
    switch (bit) {
    case kModLeftCtrl: return KEY_LEFTCTRL;
    case kModLeftShift: return KEY_LEFTSHIFT;
    case kModLeftAlt: return KEY_LEFTALT;
    case kModLeftMeta: return KEY_LEFTMETA;
    case kModRightCtrl: return KEY_RIGHTCTRL;
    case kModRightShift: return KEY_RIGHTSHIFT;
    case kModRightAlt: return KEY_RIGHTALT;
    case kModRightMeta: return KEY_RIGHTMETA;
    default: return 0;
    }
}

constexpr std::array<std::uint8_t, 8> kModifierBits = {
    kModLeftCtrl, kModLeftShift, kModLeftAlt, kModLeftMeta,
    kModRightCtrl, kModRightShift, kModRightAlt, kModRightMeta,
};

std::string deviceName(int fd) {
    char name[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) return {};
    return name;
}

} // namespace

Engine::Engine() = default;

Engine::~Engine() {
    stopCapture();
    stopInjection();
}

std::string Engine::lastError() const {
    std::lock_guard<std::mutex> lock(stateMu_);
    return lastError_;
}

void Engine::setError(const std::string &message) {
    std::lock_guard<std::mutex> lock(stateMu_);
    lastError_ = message;
}

// ── 注入 ──────────────────────────────────────────────────────────────

bool Engine::startInjection(std::string *error) {
    std::lock_guard<std::mutex> lock(injectMu_);
    if (uinputFd_.load() >= 0) return true;

    const int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        const std::string message =
            errnoText("打开 /dev/uinput 失败") +
            "（需要 uinput 权限：安装 99-flowgrid-input.rules 后重新登录，"
            "或 sudo modprobe uinput）";
        setError(message);
        if (error) *error = message;
        return false;
    }

    // 一个虚拟设备同时带键盘与相对鼠标能力；libinput 能正常识别，
    // 也省掉两个 fd 的同步问题。
    bool ok = true;
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(fd, UI_SET_EVBIT, EV_REL) < 0) {
        ok = false;
    }
    for (int key = 1; ok && key <= KEY_MAX; ++key) {
        if (ioctl(fd, UI_SET_KEYBIT, key) < 0) ok = false;
    }
    for (int rel : {REL_X, REL_Y, REL_WHEEL, REL_HWHEEL}) {
        if (ok && ioctl(fd, UI_SET_RELBIT, rel) < 0) ok = false;
    }

    if (!ok) {
        const std::string message = errnoText("配置 uinput 设备失败");
        setError(message);
        if (error) *error = message;
        close(fd);
        return false;
    }

    struct uinput_user_dev device = {};
    std::snprintf(device.name, sizeof(device.name), "%s", kDeviceName);
    device.id.bustype = BUS_USB;
    device.id.vendor = 0x1209;  // pid.codes（开源硬件测试 VID）
    device.id.product = 0x0001;
    device.id.version = 1;

    const auto *raw = reinterpret_cast<const std::uint8_t *>(&device);
    std::size_t written = 0;
    while (written < sizeof(device)) {
        const ssize_t n = write(fd, raw + written, sizeof(device) - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        written += static_cast<std::size_t>(n);
    }
    if (written != sizeof(device) || ioctl(fd, UI_DEV_CREATE) < 0) {
        const std::string message = errnoText("创建 uinput 设备失败");
        setError(message);
        if (error) *error = message;
        close(fd);
        return false;
    }

    uinputFd_.store(fd);
    injectedMods_ = 0;
    syntheticMods_ = 0;
    injectedKeys_.clear();
    injectedButtons_ = 0;
    return true;
}

void Engine::stopInjection() {
    // 关闭 fd 必须在 injectMu_ 内完成：所有写入都在同一把锁下，
    // 否则可能出现「write 拿到 fd → 另一线程 close → fd 被复用」。
    std::lock_guard<std::mutex> lock(injectMu_);
    const int fd = uinputFd_.exchange(-1);
    injectedMods_ = 0;
    syntheticMods_ = 0;
    injectedKeys_.clear();
    injectedButtons_ = 0;
    if (fd >= 0) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
    }
}

bool Engine::writeAll(const struct input_event *events, std::size_t count) {
    const int fd = uinputFd_.load();
    if (fd < 0) {
        setError("uinput 尚未初始化");
        return false;
    }
    const auto *raw = reinterpret_cast<const std::uint8_t *>(events);
    const std::size_t bytes = count * sizeof(struct input_event);
    std::size_t written = 0;
    while (written < bytes) {
        const ssize_t n = write(fd, raw + written, bytes - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            setError(errnoText("写入 uinput 失败"));
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}

bool Engine::emitKey(std::uint16_t evdevCode, bool down) {
    struct input_event events[2] = {};
    events[0].type = EV_KEY;
    events[0].code = evdevCode;
    events[0].value = down ? 1 : 0;
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    return writeAll(events, 2);
}

bool Engine::releaseSyntheticLocked() {
    for (const std::uint8_t bit : kModifierBits) {
        if ((syntheticMods_ & bit) == 0) continue;
        if (!emitKey(modifierEvdev(bit), false)) return false;
        injectedMods_ &= ~bit;
        syntheticMods_ &= ~bit;
    }
    return true;
}

bool Engine::injectKey(std::uint16_t hid, bool down, std::uint8_t modifiers) {
    if (!injectionReady()) {
        setError("uinput 尚未初始化");
        return false;
    }
    const std::uint16_t code = hidToEvdev(hid);
    if (code == 0) return true; // §6.1 未覆盖的键：静默忽略

    std::lock_guard<std::mutex> lock(injectMu_);

    // 修饰键自身的按下/松开直接反映到状态里，不再做「补齐」。
    if (const std::uint8_t bit = modifierBitForEvdev(code)) {
        if (down) {
            if ((injectedMods_ & bit) == 0) {
                if (!emitKey(code, true)) return false;
                injectedMods_ |= bit;
            }
            // 真实按键收到后，所有权从「合成」交还物理键。
            syntheticMods_ &= ~bit;
        } else {
            if (injectedMods_ & bit) {
                if (!emitKey(code, false)) return false;
                injectedMods_ &= ~bit;
            }
            syntheticMods_ &= ~bit;
        }
        return true;
    }

    const std::uint8_t wanted = modifiers & (kModCtrl | kModShift | kModAlt | kModMeta);

    if (down) {
        // 位掩码里有、但目标机还没按下的修饰键，由注入侧补按。
        for (const std::uint8_t bit : kModifierBits) {
            if ((wanted & bit) && (injectedMods_ & bit) == 0) {
                if (!emitKey(modifierEvdev(bit), true)) return false;
                injectedMods_ |= bit;
                syntheticMods_ |= bit;
            }
        }
        // 不再需要的合成位立刻松开；真实按下的位保持。
        for (const std::uint8_t bit : kModifierBits) {
            if ((syntheticMods_ & bit) && (wanted & bit) == 0) {
                if (!emitKey(modifierEvdev(bit), false)) return false;
                injectedMods_ &= ~bit;
                syntheticMods_ &= ~bit;
            }
        }
        if (!emitKey(code, true)) return false;
        injectedKeys_.insert(hid);
        return true;
    }
    if (!emitKey(code, false)) return false;
    injectedKeys_.erase(hid);
    return releaseSyntheticLocked();
}

bool Engine::injectMouseMove(std::int16_t dx, std::int16_t dy) {
    if (dx == 0 && dy == 0) return true;
    if (!injectionReady()) {
        setError("uinput 尚未初始化");
        return false;
    }
    struct input_event events[3] = {};
    std::size_t count = 0;
    if (dx != 0) {
        events[count].type = EV_REL;
        events[count].code = REL_X;
        events[count].value = dx;
        ++count;
    }
    if (dy != 0) {
        events[count].type = EV_REL;
        events[count].code = REL_Y;
        events[count].value = dy;
        ++count;
    }
    events[count].type = EV_SYN;
    events[count].code = SYN_REPORT;
    ++count;
    std::lock_guard<std::mutex> lock(injectMu_);
    return writeAll(events, count);
}

bool Engine::injectMouseButton(std::uint8_t button, bool down) {
    const std::uint16_t code = mouseButtonToEvdev(button);
    if (code == 0) return true;
    if (!injectionReady()) {
        setError("uinput 尚未初始化");
        return false;
    }
    struct input_event events[2] = {};
    events[0].type = EV_KEY;
    events[0].code = code;
    events[0].value = down ? 1 : 0;
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    std::lock_guard<std::mutex> lock(injectMu_);
    if (!writeAll(events, 2)) return false;
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << (button - 1));
    if (down) {
        injectedButtons_ |= bit;
    } else {
        injectedButtons_ &= ~bit;
    }
    return true;
}

bool Engine::injectScroll(std::int16_t deltaY, std::int16_t deltaX) {
    if (deltaY == 0 && deltaX == 0) return true;
    if (!injectionReady()) {
        setError("uinput 尚未初始化");
        return false;
    }
    struct input_event events[3] = {};
    std::size_t count = 0;
    if (deltaY != 0) {
        events[count].type = EV_REL;
        events[count].code = REL_WHEEL;
        events[count].value = deltaY;
        ++count;
    }
    if (deltaX != 0) {
        events[count].type = EV_REL;
        events[count].code = REL_HWHEEL;
        events[count].value = horizontalWheelSign(deltaX);
        ++count;
    }
    events[count].type = EV_SYN;
    events[count].code = SYN_REPORT;
    ++count;
    std::lock_guard<std::mutex> lock(injectMu_);
    return writeAll(events, count);
}

void Engine::releaseAll() {
    std::lock_guard<std::mutex> lock(injectMu_);
    if (uinputFd_.load() >= 0) {
        // 先松鼠标键与普通键，再松修饰键：与真实输入设备的收尾顺序一致，
        // 避免应用在修饰键已抬起后仍收到「主键松开」的组合键语义。
        for (std::uint8_t button = 1; button <= 5; ++button) {
            if (injectedButtons_ & (1u << (button - 1))) {
                (void)emitKey(mouseButtonToEvdev(button), false);
            }
        }
        for (const std::uint16_t hid : injectedKeys_) {
            (void)emitKey(hidToEvdev(hid), false);
        }
        for (const std::uint8_t bit : kModifierBits) {
            if (injectedMods_ & bit) {
                (void)emitKey(modifierEvdev(bit), false);
            }
        }
    }
    injectedButtons_ = 0;
    injectedKeys_.clear();
    injectedMods_ = 0;
    syntheticMods_ = 0;
}

// ── 捕获 ──────────────────────────────────────────────────────────────

bool Engine::startCapture(Callback callback, bool grabDevices, std::string *error) {
    stopCapture();
    if (!callback) {
        const std::string message = "捕获回调为空";
        setError(message);
        if (error) *error = message;
        return false;
    }

    DIR *dir = opendir("/dev/input");
    if (!dir) {
        const std::string message = errnoText("打开 /dev/input 失败");
        setError(message);
        if (error) *error = message;
        return false;
    }

    devices_.clear();
    std::string grabWarning;
    while (const dirent *entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) continue;
        const std::string path = std::string("/dev/input/") + entry->d_name;
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue; // 权限不足：跳过，最后按「一个都没有」报错

        // 绝不能抓自己的虚拟设备，否则注入的事件会被捕获线程再抓一遍。
        if (deviceName(fd) == kDeviceName) {
            close(fd);
            continue;
        }

        std::array<std::uint8_t, (EV_MAX / 8) + 1> evBits = {};
        std::array<std::uint8_t, (KEY_MAX / 8) + 1> keyBits = {};
        std::array<std::uint8_t, (REL_MAX / 8) + 1> relBits = {};
        if (ioctl(fd, EVIOCGBIT(0, evBits.size()), evBits.data()) < 0) {
            close(fd);
            continue;
        }
        ioctl(fd, EVIOCGBIT(EV_KEY, keyBits.size()), keyBits.data());
        ioctl(fd, EVIOCGBIT(EV_REL, relBits.size()), relBits.data());

        const bool hasEvKey = bitSet(evBits.data(), evBits.size(), EV_KEY);
        const bool hasEvRel = bitSet(evBits.data(), evBits.size(), EV_REL);
        const bool keyboard = hasEvKey &&
            (bitSet(keyBits.data(), keyBits.size(), KEY_A) ||
             bitSet(keyBits.data(), keyBits.size(), KEY_Z) ||
             bitSet(keyBits.data(), keyBits.size(), KEY_SPACE));
        const bool mouse = hasEvKey && bitSet(keyBits.data(), keyBits.size(), BTN_LEFT) &&
            (hasEvRel ||
             (bitSet(relBits.data(), relBits.size(), REL_X) &&
              bitSet(relBits.data(), relBits.size(), REL_Y)));
        if (!keyboard && !mouse) {
            close(fd);
            continue;
        }

        CaptureDevice device;
        device.fd = fd;
        device.keyboard = keyboard;
        device.mouse = mouse;
        if (grabDevices) {
            const int one = 1;
            if (ioctl(fd, EVIOCGRAB, &one) == 0) {
                device.grabbed = true;
            } else if (grabWarning.empty()) {
                grabWarning = errnoText(("EVIOCGRAB 失败（" + path + "）").c_str());
            }
        }
        devices_.push_back(device);
    }
    closedir(dir);

    if (devices_.empty()) {
        const std::string message =
            "没有可读的 /dev/input/event* 键盘/鼠标（检查 99-flowgrid-input.rules "
            "与 input 组权限，重新登录后再试）";
        setError(message);
        if (error) *error = message;
        return false;
    }
    if (!grabWarning.empty()) setError(grabWarning);

    callback_ = std::move(callback);
    captureRunning_.store(true, std::memory_order_release);
    try {
        captureThread_ = std::thread(&Engine::captureLoop, this);
    } catch (const std::exception &e) {
        captureRunning_.store(false, std::memory_order_release);
        closeDevices();
        const std::string message = std::string("启动捕获线程失败: ") + e.what();
        setError(message);
        if (error) *error = message;
        return false;
    }
    return true;
}

void Engine::stopCapture() {
    captureRunning_.store(false, std::memory_order_release);
    if (captureThread_.joinable()) captureThread_.join();
    closeDevices();
    callback_ = nullptr;
}

void Engine::closeDevices() {
    for (auto &device : devices_) {
        if (device.fd < 0) continue;
        if (device.grabbed) {
            const int zero = 0;
            ioctl(device.fd, EVIOCGRAB, &zero);
        }
        close(device.fd);
        device.fd = -1;
    }
    devices_.clear();
}

void Engine::captureLoop() {
    std::vector<struct pollfd> pollFds;
    pollFds.reserve(devices_.size());
    for (const auto &device : devices_) {
        pollFds.push_back(pollfd{device.fd, POLLIN, 0});
    }

    std::uint8_t modifiers = 0;
    std::int32_t pendingX = 0;
    std::int32_t pendingY = 0;
    auto lastFlush = std::chrono::steady_clock::now();

    auto emit = [this](const CapturedEvent &event) {
        try {
            if (callback_) callback_(event);
        } catch (...) {
            // 回调异常不能把捕获线程带走；调用方自己负责记录。
        }
    };
    auto flushMove = [&]() {
        if (pendingX == 0 && pendingY == 0) return;
        CapturedEvent event;
        event.kind = EventKind::MouseMove;
        event.deltaX = static_cast<std::int16_t>(
            std::max(-32768, std::min(32767, pendingX)));
        event.deltaY = static_cast<std::int16_t>(
            std::max(-32768, std::min(32767, pendingY)));
        pendingX = 0;
        pendingY = 0;
        lastFlush = std::chrono::steady_clock::now();
        emit(event);
    };

    std::array<struct input_event, kReadEvents> buffer = {};

    while (captureRunning_.load(std::memory_order_acquire)) {
        const int ready = poll(pollFds.data(), pollFds.size(), kPollTimeoutMs);
        if (ready < 0) {
            if (errno == EINTR) continue;
            setError(errnoText("poll(/dev/input) 失败"));
            break;
        }

        for (std::size_t i = 0; i < pollFds.size() && ready > 0; ++i) {
            if (pollFds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                // 设备拔掉：停掉这个 fd，别让 poll 每 5ms 立刻返回造成忙等。
                pollFds[i].fd = -1;
                continue;
            }
            if ((pollFds[i].revents & POLLIN) == 0) continue;
            while (true) {
                const ssize_t bytes = read(pollFds[i].fd, buffer.data(),
                                            buffer.size() * sizeof(struct input_event));
                if (bytes < 0) {
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    // 设备被拔掉（ENODEV）时只停这一个 fd，不拖垮其它设备。
                    pollFds[i].fd = -1;
                    break;
                }
                if (bytes == 0) break;
                const std::size_t count = static_cast<std::size_t>(bytes) /
                    sizeof(struct input_event);

                for (std::size_t n = 0; n < count; ++n) {
                    const struct input_event &raw = buffer[n];
                    if (raw.type == EV_KEY) {
                        if (raw.value != 0 && raw.value != 1) continue; // 自动重复交给目标机
                        const bool down = raw.value == 1;

                        const std::uint8_t button = evdevToMouseButton(raw.code);
                        if (button != 0) {
                            flushMove();
                            CapturedEvent event;
                            event.kind = EventKind::MouseButton;
                            event.button = button;
                            event.pressed = down; // 不带这一位，对端分不清按下/松开
                            event.modifiers = modifiers;
                            emit(event);
                            continue;
                        }

                        const std::uint16_t hid = evdevToHid(raw.code);
                        if (hid == 0) continue;
                        if (const std::uint8_t bit = modifierBitForEvdev(raw.code)) {
                            if (down) {
                                modifiers |= bit;
                            } else {
                                modifiers &= ~bit;
                            }
                        }
                        flushMove();
                        CapturedEvent event;
                        event.kind = down ? EventKind::KeyDown : EventKind::KeyUp;
                        event.keyCode = hid;
                        event.modifiers = modifiers;
                        emit(event);
                    } else if (raw.type == EV_REL) {
                        if (raw.code == REL_X) {
                            pendingX += raw.value;
                        } else if (raw.code == REL_Y) {
                            pendingY += raw.value;
                        } else if (raw.code == REL_WHEEL || raw.code == REL_HWHEEL) {
                            flushMove();
                            CapturedEvent event;
                            event.kind = EventKind::Scroll;
                            const auto delta = static_cast<std::int16_t>(
                                std::max(-32768, std::min(32767, raw.value)));
                            if (raw.code == REL_WHEEL) {
                                event.deltaY = delta;
                            } else {
                                event.deltaX = static_cast<std::int16_t>(
                                    std::max(-32768, std::min(32767,
                                        horizontalWheelSign(raw.value))));
                            }
                            event.modifiers = modifiers;
                            emit(event);
                        }
                    }
                }
            }
        }

        if ((pendingX != 0 || pendingY != 0) &&
            std::chrono::steady_clock::now() - lastFlush >= kMoveFlushInterval) {
            flushMove();
        }
    }

    flushMove();
    closeDevices();
    captureRunning_.store(false, std::memory_order_release);
}

} // namespace fg::input
