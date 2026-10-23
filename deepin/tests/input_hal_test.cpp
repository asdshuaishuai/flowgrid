// HAL 层真机测试：真实 evdev 捕获 → 真实协议 → 真实 uinput 注入 → 读回验证。
//
// 这条链路里没有假的东西：
//   · 用 uinput 造一个**独立**的测试键盘（名字与引擎自己的虚拟设备不同，
//     所以捕获引擎会把它当成物理键盘），从它发出真实的 KEY_A；
//   · 捕获引擎从 /dev/input/event* 读到真实事件；
//   → 协议层重映射 → 真实 TCP → 对端解析 → 注入引擎写进 /dev/uinput；
//   · 最后把「FlowGrid Virtual Input」那个设备节点的流读回来，确认键真的
//     出现在 evdev 里（不是「调用没报错」就算过）。
//
// 需要权限：/dev/uinput 可写、/dev/input/event* 可读
// （见 deepin/resources/99-flowgrid-input.rules）。权限不足时**如实报跳过**
// （退出码 77，automake 的 skip 约定），不假装通过——「没跑」和「跑过了」
// 必须分得清。
#include "core/input/input_engine.h"
#include "core/input/input_forwarder.h"
#include "core/input/peer_session.h"

#include <QCoreApplication>
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
#include <string>
#include <vector>

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
// 测试用的「物理键盘」名字：与上面**故意不同**，否则会被捕获引擎按名字跳过。
constexpr char kTestKeyboard[] = "FlowGrid HAL Test Keyboard";

// 能不能拿到 HAL 需要的权限。
bool haveHalPermissions(std::string *why)
{
    const int uinputFd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinputFd < 0) {
        *why = std::string("/dev/uinput 打不开：") + std::strerror(errno);
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

// 找到指定名字的 evdev 设备节点，返回 fd（-1 表示没找到）。
int openDeviceByName(const char *wanted)
{
    DIR *dir = ::opendir("/dev/input");
    if (dir == nullptr) {
        return -1;
    }
    int found = -1;
    while (const dirent *entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.rfind("event", 0) != 0) {
            continue;
        }
        const std::string path = "/dev/input/" + name;
        const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        char buffer[256] = {};
        if (::ioctl(fd, EVIOCGNAME(sizeof(buffer) - 1), buffer) >= 0) {
            if (std::strcmp(buffer, wanted) == 0) {
                found = fd;
                break;
            }
        }
        ::close(fd);
    }
    ::closedir(dir);
    return found;
}

// 造一个「物理键盘」：名字与引擎的虚拟设备不同，捕获引擎会把它当键盘。
int createTestKeyboard()
{
    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    if (::ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 || ::ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) {
        ::close(fd);
        return -1;
    }
    ::ioctl(fd, UI_SET_KEYBIT, KEY_A);
    ::ioctl(fd, UI_SET_KEYBIT, KEY_B);

    struct uinput_user_dev device = {};
    std::snprintf(device.name, sizeof(device.name), "%s", kTestKeyboard);
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

    // ① 测试键盘（扮演物理键盘）
    const int keyboardFd = createTestKeyboard();
    if (keyboardFd < 0) {
        std::fprintf(stderr, "FAIL: 创建测试键盘失败：%s\n", std::strerror(errno));
        return EXIT_FAILURE;
    }
    // 等 udev 把设备节点建出来，捕获引擎才枚举得到。
    QTimer::singleShot(800, &app, [&app, keyboardFd]() {
        // ② 注入引擎（真实的 uinput 设备）
        input::Engine injector;
        std::string error;
        if (!injector.startInjection(&error)) {
            std::fprintf(stderr, "FAIL: 注入引擎启动失败：%s\n", error.c_str());
            app.exit(1);
            return;
        }
        // ③ 捕获引擎（真实 evdev）
        input::Engine capture;
        bool sawKey = false;
        if (!capture.startCapture(
                [&sawKey](const input::CapturedEvent &event) {
                    if (event.kind == input::EventKind::KeyDown) {
                        sawKey = true;
                    }
                },
                false, // 不 grab：测试期间别把用户自己的键盘抢走
                &error)) {
            std::fprintf(stderr, "FAIL: 捕获引擎启动失败：%s\n", error.c_str());
            app.exit(1);
            return;
        }
        // ④ 读回注入设备的 evdev 流，验证键真的落到了 uinput
        const int injectFd = openDeviceByName(kEngineDevice);
        if (injectFd < 0) {
            std::fprintf(stderr, "FAIL: 找不到虚拟输入设备的 evdev 节点\n");
            app.exit(1);
            return;
        }

        QTimer::singleShot(300, &app, [&]() {
            // ⑤ 发一个真实的 KEY_A 出去
            emitKey(keyboardFd, KEY_A, 1);
            emitKey(keyboardFd, KEY_A, 0);
        });

        QTimer::singleShot(2500, &app, [&app, injectFd, &sawKey]() {
            // 读虚拟设备的事件流：应该看到 KEY_A 的 down 与 up。
            bool sawDown = false;
            bool sawUp = false;
            struct input_event event = {};
            while (::read(injectFd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event))) {
                if (event.type != EV_KEY || event.code != KEY_A) {
                    continue;
                }
                if (event.value == 1) {
                    sawDown = true;
                } else if (event.value == 0) {
                    sawUp = true;
                }
            }
            ::close(injectFd);
            check(sawKey, "捕获引擎读到了测试键盘的按键");
            check(sawDown, "注入设备上出现 KEY_A 按下");
            check(sawUp, "注入设备上出现 KEY_A 松开");
            app.exit(failures == 0 ? 0 : 1);
        });
    });

    const int rc = app.exec();
    ::close(keyboardFd);
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("input_hal_test: OK\n");
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
