// HAL 层真机测试：真实 evdev 捕获 → 真实协议 → 真实 uinput 注入 → 读回验证。
//
// 这条链路里没有假的东西：
//   · 用 uinput 造一个「物理键鼠」（名字与引擎自己的虚拟设备**故意不同**，
//     否则捕获引擎会按名字把它当成自己的设备跳过），从它发出真实的
//     REL_X/REL_Y 与 KEY_A；
//   · 捕获引擎从 /dev/input/event* 读到真实事件（生产用的那条代码路径）；
//   → 协议层重映射 → 真实 TCP（回环）→ 对端解析 → 注入引擎写进 /dev/uinput；
//   · 最后把「FlowGrid Virtual Input」那个设备节点的流读回来，确认移动与
//     按键真的出现在 evdev 里（不是「调用没报错」就算过）。
//
// 鼠标移动是真的会动你的光标——X server 认下这个虚拟设备后，光标会跟着走。
// 这是有意的：注入类的东西只能这样验。测试设备**故意不声明 BTN_LEFT**，
// 所以 X 不会把它当指针，桌面上唯一会动的光标就是注入那一份，
// 「光标动了多少」能直接归因到注入通路上。
//
// 权限：
//   · /dev/uinput 可写（写权限由 99-flowgrid-input.rules 的 uaccess 给到）——
//     这是注入侧的前提，缺了整条链路都跑不了；
//   · uinput 造出来的虚拟设备节点是 root:input 0660，**本用户得在 input
//     组里**才读得回来（读回验证与合成设备的捕获都靠它）。
//
// 权限不全时**降级但不假装**：捕获侧或读回侧不可用时，退到「只跑注入侧」，
// 照样把协议 → TCP → 注入这条路走完（光标会真的动），并如实说明哪几段
// 没验——「没跑」和「跑过了」必须分得清。整条链路都跑不了时才报跳过
// （退出码 77，automake 的 skip 约定）。
#include "core/input/input_forwarder.h"
#include "core/input/peer_session.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if FLOWGRID_HAL_X11
#include <X11/Xlib.h>
#endif

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (condition) {
        return;
    }
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

// 引擎自己的虚拟设备名（见 input_engine.cpp）。
constexpr char kEngineDevice[] = "FlowGrid Virtual Input";
// 测试用的「物理键鼠」名字：与上面**故意不同**，否则会被捕获引擎按名字跳过。
constexpr char kTestDevice[] = "FlowGrid HAL Test Device";

// 这一轮注入的量：X 光标应当正好走这些像素。
constexpr int kMoveX = 120;
constexpr int kMoveY = -60;
// HID Usage ID of 'A'（§6.1）。
constexpr std::uint16_t kHidA = 0x04;

// 造一个 uinput 设备后，等 udev 建出 evdev 节点的上限。
constexpr int kNodeWaitAttempts = 30;
constexpr int kNodeWaitMs = 100;

// 能不能拿到 HAL 需要的权限。
bool haveHalPermissions(std::string *why)
{
    const int uinputFd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinputFd < 0) {
        *why = std::string("/dev/uinput 打不开：") + std::strerror(errno)
             + "（装 99-flowgrid-input.rules 并重新登录后才有写权限）";
        return false;
    }
    ::close(uinputFd);

    DIR *dir = ::opendir("/dev/input");
    if (dir == nullptr) {
        *why = "/dev/input 打不开";
        return false;
    }
    bool any = false;
    while (const dirent *entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.rfind("event", 0) != 0) {
            continue;
        }
        const int fd = ::open(("/dev/input/" + name).c_str(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            ::close(fd);
            any = true;
            break;
        }
    }
    ::closedir(dir);
    if (!any) {
        *why = "/dev/input 下没有可读的 event 设备";
        return false;
    }
    return true;
}

// 通过 sysfs 找名字为 wanted 的 evdev 设备节点路径。
//
// 为什么绕 sysfs 而不是直接 open + EVIOCGNAME：节点本身可能因为权限不足
// 打不开（uinput 造出来的虚拟设备就是 root:input 0660），那时 EVIOCGNAME
// 根本问不到名字，就分不清「设备还没出现」和「权限不够」——而这两种情况
// 的修法完全不一样，报错必须说清楚是哪一种。
std::string findEventPathByName(const char *wanted)
{
    DIR *dir = ::opendir("/sys/class/input");
    if (dir == nullptr) {
        return {};
    }
    std::string found;
    while (const dirent *entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.rfind("event", 0) != 0) {
            continue;
        }
        const std::string namePath = "/sys/class/input/" + name + "/device/name";
        FILE *file = std::fopen(namePath.c_str(), "r");
        if (file == nullptr) {
            continue;
        }
        char buffer[256] = {};
        const char *line = std::fgets(buffer, sizeof(buffer), file);
        std::fclose(file);
        if (line == nullptr) {
            continue;
        }
        std::string deviceName(buffer);
        while (!deviceName.empty()
               && (deviceName.back() == '\n' || deviceName.back() == '\r')) {
            deviceName.pop_back();
        }
        if (deviceName == wanted) {
            found = "/dev/input/" + name;
            break;
        }
    }
    ::closedir(dir);
    return found;
}

// 打开指定名字的 evdev 节点。
enum class NodeState {
    NotYet,  // 节点还没被 udev 建出来（再等等）
    Denied,  // 节点在，但本用户读不了（权限问题，别再等了）
    Ready,   // 打开了
};

NodeState openDeviceByName(const char *wanted, int *outFd, std::string *why)
{
    const std::string path = findEventPathByName(wanted);
    if (path.empty()) {
        *why = std::string("设备「") + wanted + "」的 evdev 节点还没出现";
        return NodeState::NotYet;
    }
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        *why = std::string("打不开 ") + path + "：" + std::strerror(errno)
             + "（uinput 造出来的虚拟设备节点是 root:input 0660，"
               "本用户不在 input 组就读不回来）";
        return NodeState::Denied;
    }
    *outFd = fd;
    return NodeState::Ready;
}

// 造一个「物理键鼠」：名字与引擎的虚拟设备不同，捕获引擎会把它当真实设备。
//
// 刻意**不**声明 BTN_LEFT：X 只把带按钮的设备认成指针，不声明它就不会
// 抢走桌面的光标——这样「光标动了」只能来自注入通路，归因才干净。
int createTestDevice()
{
    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    if (::ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 || ::ioctl(fd, UI_SET_EVBIT, EV_REL) < 0
        || ::ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) {
        ::close(fd);
        return -1;
    }
    ::ioctl(fd, UI_SET_KEYBIT, KEY_A);
    ::ioctl(fd, UI_SET_RELBIT, REL_X);
    ::ioctl(fd, UI_SET_RELBIT, REL_Y);

    struct uinput_user_dev device = {};
    std::snprintf(device.name, sizeof(device.name), "%s", kTestDevice);
    device.id.bustype = BUS_USB;
    device.id.vendor = 0x1209; // pid.codes 的开源硬件测试 VID
    device.id.product = 0x0002;
    device.id.version = 1;
    if (::write(fd, &device, sizeof(device)) != static_cast<ssize_t>(sizeof(device))) {
        ::close(fd);
        return -1;
    }
    if (::ioctl(fd, UI_DEV_CREATE) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void emitKey(int fd, int code, int value)
{
    struct input_event event = {};
    event.type = EV_KEY;
    event.code = code;
    event.value = value;
    ::write(fd, &event, sizeof(event));
    event.type = EV_SYN;
    event.code = SYN_REPORT;
    event.value = 0;
    ::write(fd, &event, sizeof(event));
}

void emitMove(int fd, int dx, int dy)
{
    struct input_event events[3] = {};
    int count = 0;
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
    ::write(fd, events, static_cast<ssize_t>(sizeof(struct input_event)) * count);
}

#if FLOWGRID_HAL_X11
// 把 X 光标挪到屏幕正中，四周留出余量。
//
// 为什么必须挪：不挪的话光标此刻可能正贴在屏幕边上（实测钉在 x=1919，
// 也就是 1920 宽屏幕的最右一列），注入的 +120 会被屏幕边界夹住，光标一动
// 不动——测出来的「没往右走」其实是撞墙，不是注入没生效。这种失败看着
// 像产品坏了，其实在测一个跟产品无关的边界。
//
// dest_w 必须传 **root**，不能传 None：传 None 时这台机器上的 X 把
// dst_x/dst_y 当成**相对当前指针的偏移**——在 (960,540) 处调
// XWarpPointer(..., None, 0,0,0,0, 960,540) 会跑到 (1919,1079)（右下角，
// 被屏幕边夹住）。传 root 才是绝对坐标，实测落在 (960,540)。
//
// 返回值顺带把屏幕尺寸带出去（断言要拿它算余量）。
bool warpCursorToCenter(int *outScreenW, int *outScreenH)
{
    Display *display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return false;
    }
    Screen *screen = DefaultScreenOfDisplay(display);
    if (screen == nullptr) {
        XCloseDisplay(display);
        return false;
    }
    const int width = WidthOfScreen(screen);
    const int height = HeightOfScreen(screen);
    const Window root = DefaultRootWindow(display);
    XWarpPointer(display, root, root, 0, 0, 0, 0, width / 2, height / 2);
    XSync(display, False);
    XCloseDisplay(display);
    if (outScreenW != nullptr) {
        *outScreenW = width;
    }
    if (outScreenH != nullptr) {
        *outScreenH = height;
    }
    return width > 0 && height > 0;
}

// 读一次 X 光标位置。没有 DISPLAY、连不上 X 时返回 false。
bool queryCursor(int *outX, int *outY)
{
    Display *display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return false;
    }
    const Window root = DefaultRootWindow(display);
    Window rootReturn = 0;
    Window childReturn = 0;
    int rootX = 0;
    int rootY = 0;
    int winX = 0;
    int winY = 0;
    unsigned int mask = 0;
    const bool ok = XQueryPointer(display, root, &rootReturn, &childReturn, &rootX, &rootY, &winX,
                                  &winY, &mask)
                    != 0;
    if (ok) {
        *outX = rootX;
        *outY = rootY;
    }
    XCloseDisplay(display);
    return ok;
}
#endif

// 接管真实鼠标（EVIOCGRAB）：接管期间真实鼠标不再驱动光标，光标只会被
// 注入的事件推动——这样「光标动了多少」能干净地归因到注入通路上，也不会
// 被用户自己挪鼠标搅浑。
//
// 必须**排除引擎自己的虚拟设备**：把它也 grab 了，X 就收不到我们注入的
// 事件，光标反而不动了（这正是捕获引擎按名字跳过它的同一个理由）。
class PointerGrab {
public:
    ~PointerGrab() { release(); }

    bool grab()
    {
        DIR *dir = ::opendir("/dev/input");
        if (dir == nullptr) {
            return false;
        }
        while (const dirent *entry = ::readdir(dir)) {
            const std::string name = entry->d_name;
            if (name.rfind("event", 0) != 0) {
                continue;
            }
            const int fd = ::open(("/dev/input/" + name).c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                continue;
            }
            if (!isPointerDevice(fd)) {
                ::close(fd);
                continue;
            }
            const int one = 1;
            if (::ioctl(fd, EVIOCGRAB, &one) == 0) {
                fds_.push_back(fd);
            } else {
                ::close(fd);
            }
        }
        ::closedir(dir);
        return !fds_.empty();
    }

    void release()
    {
        for (const int fd : fds_) {
            const int zero = 0;
            ::ioctl(fd, EVIOCGRAB, &zero);
            ::close(fd);
        }
        fds_.clear();
    }

    int count() const { return static_cast<int>(fds_.size()); }

private:
    // 「是鼠标」：有 BTN_LEFT 且有 REL_X/REL_Y；而且不是我们自己的虚拟设备。
    static bool isPointerDevice(int fd)
    {
        char deviceName[256] = {};
        if (::ioctl(fd, EVIOCGNAME(sizeof(deviceName) - 1), deviceName) >= 0
            && std::strcmp(deviceName, kEngineDevice) == 0) {
            return false;
        }
        unsigned char keyBits[(KEY_MAX / 8) + 1] = {};
        unsigned char relBits[(REL_MAX / 8) + 1] = {};
        if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0) {
            return false;
        }
        if (::ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relBits)), relBits) < 0) {
            return false;
        }
        const auto bit = [](const unsigned char *bits, int index) {
            return (bits[index / 8] & (1u << (index % 8))) != 0;
        };
        return bit(keyBits, BTN_LEFT) && bit(relBits, REL_X) && bit(relBits, REL_Y);
    }

    std::vector<int> fds_;
};

using namespace fg;

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::string why;
    if (!haveHalPermissions(&why)) {
        std::printf("input_hal_test: 跳过——%s\n", why.c_str());
        std::printf("  需要装 udev 规则并重新登录："
                    "sudo install -m 0644 deepin/resources/99-flowgrid-input.rules "
                    "/etc/udev/rules.d/\n");
        return 77; // automake 的 skip 约定
    }

    // ① 测试用的「物理键鼠」
    const int physFd = createTestDevice();
    if (physFd < 0) {
        std::fprintf(stderr, "FAIL: 创建测试设备失败：%s\n", std::strerror(errno));
        return EXIT_FAILURE;
    }

    // ② 注入引擎（真实的 uinput 虚拟键鼠）
    input::Engine injector;
    std::string error;
    if (!injector.startInjection(&error)) {
        std::fprintf(stderr, "FAIL: 注入引擎启动失败：%s\n", error.c_str());
        ::close(physFd);
        return EXIT_FAILURE;
    }

    // 捕获到的事件（主线程里才有；捕获线程的回调会排队回来）。
    auto captured = std::make_shared<std::vector<input::CapturedEvent>>();
    // 注入设备的 evdev 节点，读回验证时用。
    int injectFd = -1;
    // 测试设备的 evdev 节点（存在性由捕获引擎的枚举间接验证，这里只用来探测）。
    int testFd = -1;
    int waitAttempts = 0;
    // 两段各自的「不可用原因」；非空表示这一段没验成。
    std::string captureBlockReason;
    std::string readbackBlockReason;

    input::Engine capture;
    input::Forwarder sender(nullptr, nullptr);
    input::Forwarder receiver(&injector, nullptr);
    input::PeerSession target;
    input::PeerSession controller;
    // 接管真实鼠标 + 记录注入前后的光标位置。
    auto grab = std::make_shared<PointerGrab>();
    struct CursorWatch {
        bool enabled = false;
        bool haveBefore = false;
        // 光标是否成功挪到了屏幕正中。没挪成就不能判方向：光标贴着屏幕
        // 边时，注入的位移会被边界夹住，「没往右走」是撞墙不是没注入。
        bool centered = false;
        int beforeX = 0;
        int beforeY = 0;
        int screenW = 0;
        int screenH = 0;
    };
    auto watch = std::make_shared<CursorWatch>();

    // ③ 等 udev 把两个设备节点建出来（uinput 造设备是异步的）。
    auto waitNodes = std::make_shared<std::function<void()>>();
    *waitNodes = [waitNodes, &app, &injectFd, &testFd, &waitAttempts, &captureBlockReason,
                  &readbackBlockReason, captured, &capture, &sender, &receiver, &target,
                  &controller, physFd, grab, watch]() {
        std::string reason;
        bool pending = false;
        if (injectFd < 0) {
            const NodeState state = openDeviceByName(kEngineDevice, &injectFd, &reason);
            if (state == NodeState::Denied) {
                readbackBlockReason = reason;
            } else if (state == NodeState::NotYet) {
                pending = true;
            }
        }
        if (testFd < 0) {
            const NodeState state = openDeviceByName(kTestDevice, &testFd, &reason);
            if (state == NodeState::Denied) {
                captureBlockReason = reason;
            } else if (state == NodeState::NotYet) {
                pending = true;
            }
        }
        if (pending) {
            if (++waitAttempts >= kNodeWaitAttempts) {
                // 节点一直不出来：udev 没给 uinput 设备建节点（uinput 模块？规则？）。
                if (injectFd < 0) {
                    readbackBlockReason = "udev 一直没为注入设备建出 evdev 节点";
                }
                if (testFd < 0) {
                    captureBlockReason = "udev 一直没为测试设备建出 evdev 节点";
                }
            } else {
                QTimer::singleShot(kNodeWaitMs, &app, *waitNodes);
                return;
            }
        }
        if (testFd >= 0) {
            ::close(testFd);
            testFd = -1;
        }

        const bool captureOk = captureBlockReason.empty();
        const bool readbackOk = readbackBlockReason.empty();
        if (!captureOk) {
            std::printf("[HAL] 捕获侧不可用：%s\n", captureBlockReason.c_str());
            std::printf("       → 改用构造的输入事件，只验「协议 → TCP → 注入」这段\n");
        }
        if (!readbackOk) {
            std::printf("[HAL] 读回验证不可用：%s\n", readbackBlockReason.c_str());
        }

        // ④ 捕获引擎（生产用的那条路径，不 grab：别把用户自己的键盘抢走）。
        if (captureOk) {
            std::string captureError;
            if (!capture.startCapture(
                    [captured, &app](const input::CapturedEvent &event) {
                        // 捕获回调在捕获线程上：排队回主线程再落进 captured。
                        // 直接 push 是数据竞争——主线程正在遍历同一个 vector。
                        QMetaObject::invokeMethod(
                            &app,
                            [captured, event]() { captured->push_back(event); },
                            Qt::QueuedConnection);
                    },
                    false, &captureError)) {
                std::fprintf(stderr, "FAIL: 捕获引擎启动失败：%s\n", captureError.c_str());
                app.exit(1);
                return;
            }
        }

        // ⑤ 会话两端（回环 TCP），把捕获 → 协议 → 对端 → 注入串起来
        target.setLocalIdentity(QStringLiteral("hal-target"), proto::platform::kLinux);
        controller.setLocalIdentity(QStringLiteral("hal-controller"), proto::platform::kLinux);
        target.setPairingCode(QStringLiteral("6673"));
        controller.setPairingCode(QStringLiteral("6673"));

        sender.setPeerPlatform(proto::platform::kLinux);
        sender.setSendFn([&controller](const input::OutboundFrame &frame) {
            controller.sendFrame(frame.frameType, frame.payload);
        });

        QObject::connect(&target, &input::PeerSession::frameReceived, &app,
                         [&receiver](std::uint8_t type, const QByteArray &payload) {
                             if (!receiver.handleInbound(type, payload)) {
                                 std::fprintf(stderr, "FAIL: 入站帧处理失败：%s\n",
                                              receiver.lastError().toUtf8().constData());
                                 ++failures;
                             }
                         });
        QObject::connect(&target, &input::PeerSession::inviteReceived, &target,
                         [&target](const QString &, const QString &) {
                             target.respondInvite(true);
                         });

        QObject::connect(&controller, &input::PeerSession::stateChanged, &controller,
                         [&app, captured, &sender, injectFd, physFd, captureOk, readbackOk,
                          grab, watch](
                             input::PeerSession::State state, const QString &detail) {
            if (state == input::PeerSession::State::Connected) {
                // 测光标位移之前先接管真实鼠标：真实鼠标不再驱动光标，
                // 光标的位移就只可能来自注入这一条通路。
                const int grabbed = grab->grab();
                if (grabbed > 0) {
                    std::printf("[HAL] 已接管 %d 个真实鼠标设备"
                                "（光标位移只剩注入这一条来源）\n", grabbed);
                }
#if FLOWGRID_HAL_X11
                watch->enabled = ::getenv("DISPLAY") != nullptr;
                if (watch->enabled) {
                    // 先把光标挪到屏幕正中，四周留出余量——不挪的话光标此刻
                    // 可能正贴着屏幕边，注入的位移会被边界夹住，「没往右走」
                    // 其实是撞墙。
                    watch->centered = warpCursorToCenter(&watch->screenW, &watch->screenH);
                    watch->haveBefore = queryCursor(&watch->beforeX, &watch->beforeY);
                    if (watch->haveBefore) {
                        std::printf("[HAL] 注入前 X 光标：(%d,%d)（屏幕 %dx%d，%s）\n",
                                    watch->beforeX, watch->beforeY, watch->screenW, watch->screenH,
                                    watch->centered ? "已挪到正中" : "挪动失败，方向断言将跳过");
                    }
                }
#endif

                // ⑥ 握手通了。捕获侧可用时，从「物理」设备发出真实的鼠标移动与按键。
                if (captureOk) {
                    QTimer::singleShot(150, &app, [physFd]() {
                        std::printf("[HAL] 发出物理事件：鼠标移动 dx=%+d dy=%+d，按键 A\n",
                                    kMoveX, kMoveY);
                        emitMove(physFd, kMoveX, kMoveY);
                        emitKey(physFd, KEY_A, 1);
                        emitKey(physFd, KEY_A, 0);
                    });
                }

                // ⑦ 挑出这次的三条事件，走协议发出去。
                const int delay = captureOk ? 900 : 100;
                QTimer::singleShot(delay, &app, [captured, &sender, &app, injectFd, captureOk,
                                                 readbackOk, grab, watch]() {
                    input::CapturedEvent moveEvent;
                    input::CapturedEvent downEvent;
                    input::CapturedEvent upEvent;
                    moveEvent.kind = input::EventKind::MouseMove;
                    moveEvent.deltaX = kMoveX;
                    moveEvent.deltaY = kMoveY;
                    downEvent.kind = input::EventKind::KeyDown;
                    downEvent.keyCode = kHidA;
                    upEvent.kind = input::EventKind::KeyUp;
                    upEvent.keyCode = kHidA;

                    if (captureOk) {
                        const input::CapturedEvent *foundMove = nullptr;
                        const input::CapturedEvent *foundDown = nullptr;
                        const input::CapturedEvent *foundUp = nullptr;
                        for (const input::CapturedEvent &event : *captured) {
                            if (foundMove == nullptr
                                && event.kind == input::EventKind::MouseMove
                                && event.deltaX == kMoveX && event.deltaY == kMoveY) {
                                foundMove = &event;
                            } else if (foundDown == nullptr
                                       && event.kind == input::EventKind::KeyDown
                                       && event.keyCode == kHidA) {
                                foundDown = &event;
                            } else if (foundUp == nullptr
                                       && event.kind == input::EventKind::KeyUp
                                       && event.keyCode == kHidA) {
                                foundUp = &event;
                            }
                        }
                        std::printf("[HAL] 捕获到 %zu 条事件\n", captured->size());
                        for (const input::CapturedEvent &event : *captured) {
                            switch (event.kind) {
                            case input::EventKind::MouseMove:
                                std::printf("       mousemove ΔX=%d ΔY=%d\n", event.deltaX, event.deltaY);
                                break;
                            case input::EventKind::KeyDown:
                                std::printf("       keydown   hid=0x%04x\n", event.keyCode);
                                break;
                            case input::EventKind::KeyUp:
                                std::printf("       keyup     hid=0x%04x\n", event.keyCode);
                                break;
                            default:
                                break;
                            }
                        }
                        check(foundMove != nullptr, "捕获到鼠标移动事件（ΔX=120 ΔY=-60）");
                        check(foundDown != nullptr, "捕获到按键 A 按下");
                        check(foundUp != nullptr, "捕获到按键 A 松开");
                        if (foundMove == nullptr || foundDown == nullptr || foundUp == nullptr) {
                            app.exit(1);
                            return;
                        }
                        moveEvent = *foundMove;
                        downEvent = *foundDown;
                        upEvent = *foundUp;
                    } else {
                        std::printf("[HAL] 构造输入事件：鼠标移动 dx=%+d dy=%+d，按键 A\n",
                                    kMoveX, kMoveY);
                    }

                    // 只转发这次的三条：桌面上真实鼠标的噪声事件不影响归因。
                    sender.forwardCaptured(moveEvent);
                    sender.forwardCaptured(downEvent);
                    sender.forwardCaptured(upEvent);

                    // ⑧ 读回虚拟设备的事件流，验证真的落到了 uinput。
                    QTimer::singleShot(700, &app, [injectFd, &app, readbackOk, grab, watch]() {
                        if (readbackOk) {
                            bool sawRelX = false;
                            bool sawRelY = false;
                            bool sawKeyDown = false;
                            bool sawKeyUp = false;
                            int relXSum = 0;
                            int relYSum = 0;
                            struct input_event event = {};
                            while (::read(injectFd, &event, sizeof(event))
                                   == static_cast<ssize_t>(sizeof(event))) {
                                if (event.type == EV_REL && event.code == REL_X) {
                                    relXSum += event.value;
                                    if (event.value == kMoveX) {
                                        sawRelX = true;
                                    }
                                } else if (event.type == EV_REL && event.code == REL_Y) {
                                    relYSum += event.value;
                                    if (event.value == kMoveY) {
                                        sawRelY = true;
                                    }
                                } else if (event.type == EV_KEY && event.code == KEY_A) {
                                    if (event.value == 1) {
                                        sawKeyDown = true;
                                    } else if (event.value == 0) {
                                        sawKeyUp = true;
                                    }
                                }
                            }
                            std::printf("[HAL] 注入设备读回：REL_X 合计=%d REL_Y 合计=%d\n",
                                        relXSum, relYSum);
                            check(sawRelX, "鼠标移动 dx 落到 uinput（+120）");
                            check(sawRelY, "鼠标移动 dy 落到 uinput（-60）");
                            check(sawKeyDown, "按键按下落到 uinput");
                            check(sawKeyUp, "按键松开落到 uinput");
                        } else {
                            // 读不回来就没法逐条核对事件流：如实说没验，
                            // 不拿「注入调用没报错」冒充验证通过。
                            std::printf("[HAL] 注入已写入 /dev/uinput（读回验证未做）\n");
                        }

                        // ⑨ 光标位移：注入类的东西光看返回值不算数，得看桌面上的
                        // 光标有没有真的跟着走。
#if FLOWGRID_HAL_X11
                        if (watch->enabled && watch->haveBefore) {
                            int afterX = 0;
                            int afterY = 0;
                            if (queryCursor(&afterX, &afterY)) {
                                const int dx = afterX - watch->beforeX;
                                const int dy = afterY - watch->beforeY;
                                std::printf("[HAL] X 光标：(%.0f,%.0f) → (%.0f,%.0f)  Δ=(%+d,%+d)\n",
                                            static_cast<double>(watch->beforeX),
                                            static_cast<double>(watch->beforeY),
                                            static_cast<double>(afterX), static_cast<double>(afterY),
                                            dx, dy);
                                // X 的指针加速会缩放位移（实测 120 → 103），
                                // 所以只判方向和「确实动了」，不判精确值。
                                //
                                // 光标没能挪到屏幕正中时**不判方向**：贴着屏幕边时
                                // 位移会被边界夹住，那时候的「没动」是撞墙，不是
                                // 注入没生效。如实说没验，别拿一个必然失败的
                                // 断言把真问题盖住。
                                if (watch->centered) {
                                    check(dx > 0, "X 光标往右移动了");
                                    check(dy < 0, "X 光标往上移动了");
                                } else {
                                    std::printf("[HAL] 光标挪不到屏幕正中："
                                                "位置边界会让方向断言失真 → 跳过方向核对\n");
                                }
                            } else {
                                std::printf("[HAL] 读不到 X 光标位置（X 连接断了？）\n");
                            }
                        }
#endif
                        grab->release();

                        if (!readbackOk) {
                            app.exit(77);
                            return;
                        }
                        app.exit(failures == 0 ? 0 : 1);
                    });
                });
                return;
            }
            if (state == input::PeerSession::State::Failed
                || state == input::PeerSession::State::Rejected) {
                std::fprintf(stderr, "FAIL: 会话未建立：%s\n", detail.toUtf8().constData());
                app.exit(1);
            }
        });

        const quint16 port = 34921;
        if (!target.listen(port)) {
            std::fprintf(stderr, "FAIL: 被控端监听失败\n");
            app.exit(1);
            return;
        }
        controller.connectTo(QStringLiteral("127.0.0.1"), port);
    };
    QTimer::singleShot(0, &app, *waitNodes);

    const int rc = app.exec();
    ::close(physFd);
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    if (rc == 77) {
        std::printf("input_hal_test: 部分通过（注入侧已跑通；读回验证因权限不可用未做）\n");
        std::printf("  补齐权限：sudo usermod -aG input $USER，然后重新登录\n");
        return 77; // automake 的 skip 约定
    }
    if (rc != 0) {
        std::fprintf(stderr, "input_hal_test: 未走完流程（退出码 %d）\n", rc);
        return EXIT_FAILURE;
    }
    std::printf("input_hal_test: OK\n");
    return EXIT_SUCCESS;
}
