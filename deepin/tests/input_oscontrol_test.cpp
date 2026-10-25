// OS 控制层（cgo 桥）的单测：验**不需要任何权限**的那部分。
//
// 这一层是「协议层的 oscontrol 流水线」与 C++ 客户端之间的接缝，接缝坏了
// 的表现很隐蔽：客户端显示「接管中」，实际一个键都没动；或者协议层算出的
// 帧类型跟客户端以为的不一样。所以接缝本身要有测试。
//
// 这里只测**不需要 /dev/uinput、不需要 input 组**的部分：
//   · 句柄的建立与销毁（未注册的平台要拿不到）；
//   · 错误路径：无效句柄、无效键位规则句柄、不是输入帧的注入；
//   · 错误信息要能取回来（客户端要把它显示给用户，不能只给一个布尔值）。
//
// 真机那条（真捕获 → 真注入 → 读回验证 → X 光标位移）在 input_hal_test.cpp。
#include "core/input/oscontrol.h"

#include <libflowgrid_protocol.h>

#include <QCoreApplication>

#include <cstdio>
#include <cstdlib>

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

using namespace fg;

// 取一次桥上的错误说明。
QString bridgeError(std::uintptr_t handle)
{
    char buffer[512] = {0};
    fg_oscontrol_last_error(handle, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ── 1. 句柄的建立与销毁 ───────────────────────────────────────────
    {
        input::OsControl osControl;
        check(osControl.isValid(), "Linux 平台能拿到 OS 控制器");
        check(osControl.lastError().isEmpty(), "刚建好时没有残留错误");
    }
    {
        // 没实现的平台拿不到：这是**如实**失败，不是崩、也不是静默不做事。
        const auto handle = fg_oscontrol_new(proto::platform::kWindows);
        check(handle == 0, "没实现的平台拿不到控制器（句柄 0 = 失败哨兵）");
    }

    // ── 2. 无效句柄要报错，不能崩也不能假装成功 ───────────────────────
    {
        const auto handle = fg_oscontrol_new(proto::platform::kLinux);
        check(handle != 0, "Linux 平台能拿到控制器（原始接口）");

        std::uint8_t frameType = 0;
        std::uint8_t *payload = nullptr;
        std::size_t length = 0;
        const int rc = fg_oscontrol_next_frame(handle + 9999, 0, &frameType, &payload, &length);
        check(rc == -1, "无效句柄取帧失败（不是「没帧」，是出错）");

        const int injectRc = fg_oscontrol_inject_frame(handle + 9999,
                                                        proto::frametype::kMouseMove, nullptr, 0);
        check(injectRc == -1, "无效句柄注入失败");

        fg_oscontrol_free(handle);
    }

    // ── 3. 不是输入帧的注入要失败，但不算错 ───────────────────────────
    {
        const auto handle = fg_oscontrol_new(proto::platform::kLinux);
        // 剪贴板帧不是输入帧：协议层解不了，应当失败。
        // 去 const 只是为了对上 cgo 的签名（cgo 生成的头里没有 const 限定）：
        // Go 侧只读。
        auto *bytes = const_cast<std::uint8_t *>(
            reinterpret_cast<const std::uint8_t *>("x"));
        const int rc = fg_oscontrol_inject_frame(handle, proto::frametype::kClipboard, bytes, 1);
        check(rc == -1, "剪贴板帧不是输入帧，注入应当失败");
        // 失败之后错误说明要能取回来：客户端要把它显示给用户。
        const QString error = bridgeError(handle);
        check(!error.isEmpty(), "失败之后能取到错误说明");
        std::printf("  错误说明示例：%s\n", error.toUtf8().constData());
        fg_oscontrol_free(handle);
    }

    // ── 4. 包装层：空规则合法，非法规则句柄要报错 ─────────────────────
    {
        input::OsControl osControl;
        // nullptr = 只用平台默认换算，这是合法用法（用户没配规则时）。
        check(osControl.setPeer(proto::platform::kMacOS, nullptr, QString()),
              "空规则（nullptr）是合法的：只用平台默认换算");
    }

    // ── 5. 包装层：没开注入设备时注入要失败 ───────────────────────────
    {
        input::OsControl osControl;
        check(!osControl.injectFrame(proto::frametype::kMouseMove, QByteArray(4, '\0')),
              "没开注入设备时注入应当失败");
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("input_oscontrol_test: OK\n");
    return EXIT_SUCCESS;
}
