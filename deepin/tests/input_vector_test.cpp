// 跨客户端输入行为黄金向量测试：deepin 侧对着协议层生成的那份向量文件比对。
//
// 为什么要有这个：mac / Windows / deepin 各自实现捕获与注入的 HAL，但
// 「某个输入事件在协议层应该变成什么字节」只有协议层知道。以前这条只靠
// 各端单测里的零散断言守着，两端各写各的期望值，行为迟早分叉，而且分叉了
// 也没人知道是哪端错了。
//
// 现在协议层把向量算好落到 protocol/testdata/input_vectors.json，各端读
// **同一份文件**。本测试做两件事：
//   1. 常量镜像对齐：deepin 代码里手抄的帧类型 / 平台 / 修饰位 / 角色 /
//      能力位，逐个跟向量文件里的真值比对——协议层改了而客户端忘了改，
//      这里立刻红（以前没有任何机制保证这件事）。
//   2. 载荷字节对齐：把向量里的输入事件喂给转发通路，逐个核对产出的帧类型
//      与载荷字节。
//
// 只跑 from_platform == "linux" 的用例（deepin 是 Linux 客户端）；
// 其余平台的用例留给 mac / Windows 客户端读同一份文件。
#include "core/input/input_forwarder.h"
#include "core/input/input_keymap.h"
#include "core/protocolapi.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cstdio>
#include <cstdlib>
#include <vector>

#ifndef FLOWGRID_VECTOR_FILE
#error "FLOWGRID_VECTOR_FILE 没定义（CMake 应把向量文件路径传进来）"
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

using namespace fg;
using namespace fg::input;

// 向量文件里的帧类型名 → deepin 的帧类型常量。
std::uint8_t frameTypeOf(const QString &name)
{
    if (name == QLatin1String("key_down")) return proto::frametype::kKeyDown;
    if (name == QLatin1String("key_up")) return proto::frametype::kKeyUp;
    if (name == QLatin1String("mouse_move")) return proto::frametype::kMouseMove;
    if (name == QLatin1String("mouse_button")) return proto::frametype::kMouseButton;
    if (name == QLatin1String("scroll")) return proto::frametype::kScroll;
    if (name == QLatin1String("clipboard")) return proto::frametype::kClipboard;
    if (name == QLatin1String("heartbeat")) return proto::frametype::kHeartbeat;
    if (name == QLatin1String("latency_ping")) return proto::frametype::kLatencyPing;
    if (name == QLatin1String("latency_pong")) return proto::frametype::kLatencyPong;
    if (name == QLatin1String("identify")) return proto::frametype::kIdentify;
    if (name == QLatin1String("identify_resp")) return proto::frametype::kIdentifyRes;
    return 0;
}

std::uint8_t platformOf(const QString &name)
{
    if (name == QLatin1String("macos")) return proto::platform::kMacOS;
    if (name == QLatin1String("windows")) return proto::platform::kWindows;
    if (name == QLatin1String("linux")) return proto::platform::kLinux;
    return 0;
}

// 读向量文件里的一个「常量分组」（帧类型 / 平台 / 修饰位……）。
QJsonObject constantsGroup(const QJsonObject &constants, const QString &group)
{
    return constants.value(group).toObject();
}

// 一条常量比对：镜像值必须等于协议层的真值。
void checkConstant(const QJsonObject &group, const QString &name, std::uint8_t mirrored,
                   const char *label)
{
    const QJsonValue value = group.value(name);
    if (!value.isDouble()) {
        std::fprintf(stderr, "FAIL: 向量文件里没有常量 %s.%s\n", label, name.toUtf8().constData());
        ++failures;
        return;
    }
    const auto expected = static_cast<std::uint8_t>(value.toDouble());
    if (mirrored != expected) {
        std::fprintf(stderr, "FAIL: 常量 %s.%s 镜像成 0x%02x，协议层是 0x%02x\n", label,
                     name.toUtf8().constData(), mirrored, expected);
        ++failures;
    }
}

// 向量里的一个输入事件 → deepin 的捕获事件。
bool buildEvent(const QJsonObject &object, CapturedEvent *out)
{
    const QString kind = object.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("key_down")) {
        out->kind = EventKind::KeyDown;
    } else if (kind == QLatin1String("key_up")) {
        out->kind = EventKind::KeyUp;
    } else if (kind == QLatin1String("mouse_move")) {
        out->kind = EventKind::MouseMove;
    } else if (kind == QLatin1String("mouse_button")) {
        out->kind = EventKind::MouseButton;
    } else if (kind == QLatin1String("scroll")) {
        out->kind = EventKind::Scroll;
    } else {
        return false;
    }
    out->keyCode = static_cast<std::uint16_t>(object.value(QStringLiteral("key")).toDouble());
    out->modifiers = static_cast<std::uint8_t>(object.value(QStringLiteral("modifiers")).toDouble());
    out->deltaX = static_cast<std::int16_t>(object.value(QStringLiteral("delta_x")).toDouble());
    out->deltaY = static_cast<std::int16_t>(object.value(QStringLiteral("delta_y")).toDouble());
    out->button = static_cast<std::uint8_t>(object.value(QStringLiteral("button")).toDouble());
    out->pressed = object.value(QStringLiteral("pressed")).toBool();
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QFile file(QStringLiteral(FLOWGRID_VECTOR_FILE));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "FAIL: 打不开向量文件 %s：%s\n", FLOWGRID_VECTOR_FILE,
                     file.errorString().toUtf8().constData());
        std::fprintf(stderr, "  生成：cd protocol && go run ./cmd/inputvectors -w testdata/input_vectors.json\n");
        return EXIT_FAILURE;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        std::fprintf(stderr, "FAIL: 向量文件不是合法 JSON 对象\n");
        return EXIT_FAILURE;
    }
    const QJsonObject root = document.object();
    const QJsonObject constants = root.value(QStringLiteral("constants")).toObject();
    if (constants.isEmpty()) {
        std::fprintf(stderr, "FAIL: 向量文件里没有 constants\n");
        return EXIT_FAILURE;
    }

    // ── 1. 常量镜像对齐 ────────────────────────────────────────────
    const QJsonObject frameTypes = constantsGroup(constants, QStringLiteral("frame_types"));
    checkConstant(frameTypes, QStringLiteral("key_down"), proto::frametype::kKeyDown, "frame_types");
    checkConstant(frameTypes, QStringLiteral("key_up"), proto::frametype::kKeyUp, "frame_types");
    checkConstant(frameTypes, QStringLiteral("mouse_move"), proto::frametype::kMouseMove, "frame_types");
    checkConstant(frameTypes, QStringLiteral("mouse_button"), proto::frametype::kMouseButton, "frame_types");
    checkConstant(frameTypes, QStringLiteral("scroll"), proto::frametype::kScroll, "frame_types");
    checkConstant(frameTypes, QStringLiteral("clipboard"), proto::frametype::kClipboard, "frame_types");
    checkConstant(frameTypes, QStringLiteral("heartbeat"), proto::frametype::kHeartbeat, "frame_types");
    checkConstant(frameTypes, QStringLiteral("latency_ping"), proto::frametype::kLatencyPing, "frame_types");
    checkConstant(frameTypes, QStringLiteral("latency_pong"), proto::frametype::kLatencyPong, "frame_types");
    checkConstant(frameTypes, QStringLiteral("identify"), proto::frametype::kIdentify, "frame_types");
    checkConstant(frameTypes, QStringLiteral("identify_resp"), proto::frametype::kIdentifyRes, "frame_types");

    const QJsonObject platforms = constantsGroup(constants, QStringLiteral("platforms"));
    checkConstant(platforms, QStringLiteral("macos"), proto::platform::kMacOS, "platforms");
    checkConstant(platforms, QStringLiteral("windows"), proto::platform::kWindows, "platforms");
    checkConstant(platforms, QStringLiteral("linux"), proto::platform::kLinux, "platforms");

    const QJsonObject modifiers = constantsGroup(constants, QStringLiteral("modifiers"));
    checkConstant(modifiers, QStringLiteral("left_ctrl"), kModLeftCtrl, "modifiers");
    checkConstant(modifiers, QStringLiteral("left_shift"), kModLeftShift, "modifiers");
    checkConstant(modifiers, QStringLiteral("left_alt"), kModLeftAlt, "modifiers");
    checkConstant(modifiers, QStringLiteral("left_meta"), kModLeftMeta, "modifiers");
    checkConstant(modifiers, QStringLiteral("right_ctrl"), kModRightCtrl, "modifiers");
    checkConstant(modifiers, QStringLiteral("right_shift"), kModRightShift, "modifiers");
    checkConstant(modifiers, QStringLiteral("right_alt"), kModRightAlt, "modifiers");
    checkConstant(modifiers, QStringLiteral("right_meta"), kModRightMeta, "modifiers");
    checkConstant(modifiers, QStringLiteral("ctrl"), kModCtrl, "modifiers");
    checkConstant(modifiers, QStringLiteral("shift"), kModShift, "modifiers");
    checkConstant(modifiers, QStringLiteral("alt"), kModAlt, "modifiers");
    checkConstant(modifiers, QStringLiteral("meta"), kModMeta, "modifiers");

    const QJsonObject roles = constantsGroup(constants, QStringLiteral("roles"));
    checkConstant(roles, QStringLiteral("host"), proto::role::kHost, "roles");
    checkConstant(roles, QStringLiteral("target"), proto::role::kTarget, "roles");
    checkConstant(roles, QStringLiteral("both"), proto::role::kBoth, "roles");

    const QJsonObject capabilities = constantsGroup(constants, QStringLiteral("capabilities"));
    checkConstant(capabilities, QStringLiteral("intelligent_link"), proto::caps::kIntelligentLink, "capabilities");
    checkConstant(capabilities, QStringLiteral("direct_link"), proto::caps::kDirectLink, "capabilities");
    checkConstant(capabilities, QStringLiteral("wifi_direct"), proto::caps::kWiFiDirect, "capabilities");
    checkConstant(capabilities, QStringLiteral("dtls13"), proto::caps::kDTLS13, "capabilities");
    checkConstant(capabilities, QStringLiteral("clipboard"), proto::caps::kClipboard, "capabilities");
    checkConstant(capabilities, QStringLiteral("file_transfer"), proto::caps::kFileTransfer, "capabilities");

    // 协议版本：deepin 侧镜像的 kProtocolVersion。
    const auto protocolVersion =
        static_cast<std::uint8_t>(constants.value(QStringLiteral("protocol_version")).toDouble());
    check(protocolVersion == proto::kProtocolVersion, "协议版本镜像与协议层一致");

    // 鼠标按钮号：协议 §6.4 的 1=左 2=右 3=中。
    const QJsonObject buttons = constantsGroup(constants, QStringLiteral("mouse_buttons"));
    checkConstant(buttons, QStringLiteral("left"), kMouseButtonLeft, "mouse_buttons");
    checkConstant(buttons, QStringLiteral("right"), kMouseButtonRight, "mouse_buttons");
    checkConstant(buttons, QStringLiteral("middle"), kMouseButtonMiddle, "mouse_buttons");
    checkConstant(buttons, QStringLiteral("back"), kMouseButtonBack, "mouse_buttons");
    checkConstant(buttons, QStringLiteral("forward"), kMouseButtonForward, "mouse_buttons");
    // 按钮号 → evdev 键码的映射也得对（0x01 是左键，不是别的）。
    check(mouseButtonToEvdev(kMouseButtonLeft) == BTN_LEFT, "左键映射到 BTN_LEFT");
    check(mouseButtonToEvdev(kMouseButtonRight) == BTN_RIGHT, "右键映射到 BTN_RIGHT");
    check(mouseButtonToEvdev(kMouseButtonMiddle) == BTN_MIDDLE, "中键映射到 BTN_MIDDLE");

    // ── 2. 载荷字节对齐 ────────────────────────────────────────────
    const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
    int ran = 0;
    int skipped = 0;
    for (const QJsonValue &caseValue : cases) {
        const QJsonObject caseObject = caseValue.toObject();
        const QString from = caseObject.value(QStringLiteral("from_platform")).toString();
        const QString to = caseObject.value(QStringLiteral("to_platform")).toString();
        const QString name = caseObject.value(QStringLiteral("name")).toString();
        if (from != QLatin1String("linux")) {
            // 这是别的平台的客户端的用例（mac / Windows）——同一份文件，它们读得到。
            ++skipped;
            continue;
        }
        const std::uint8_t peerPlatform = platformOf(to);
        if (peerPlatform == 0) {
            std::fprintf(stderr, "FAIL: 用例 %s 的对端平台 %s 不认识\n", name.toUtf8().constData(),
                         to.toUtf8().constData());
            ++failures;
            continue;
        }

        // 转发通路：把捕获事件转成线上帧。这里不接注入目标（只关心产出什么）。
        Forwarder sender(nullptr, nullptr);
        sender.setPeerPlatform(peerPlatform);
        std::vector<OutboundFrame> sent;
        sender.setSendFn([&sent](const OutboundFrame &frame) { sent.push_back(frame); });

        const QJsonArray events = caseObject.value(QStringLiteral("events")).toArray();
        const QJsonArray expects = caseObject.value(QStringLiteral("expect")).toArray();
        for (const QJsonValue &eventValue : events) {
            CapturedEvent event;
            if (!buildEvent(eventValue.toObject(), &event)) {
                std::fprintf(stderr, "FAIL: 用例 %s 有不认识的事件\n", name.toUtf8().constData());
                ++failures;
                break;
            }
            if (!sender.forwardCaptured(event)) {
                std::fprintf(stderr, "FAIL: 用例 %s 转发失败：%s\n", name.toUtf8().constData(),
                             sender.lastError().toUtf8().constData());
                ++failures;
                break;
            }
        }

        if (static_cast<int>(sent.size()) != static_cast<int>(expects.size())) {
            std::fprintf(stderr, "FAIL: 用例 %s 产出 %lld 帧，向量期望 %lld 帧\n",
                         name.toUtf8().constData(), static_cast<long long>(sent.size()),
                         static_cast<long long>(expects.size()));
            ++failures;
            continue;
        }
        for (int i = 0; i < expects.size(); ++i) {
            const QJsonObject want = expects.at(i).toObject();
            const std::uint8_t wantType = frameTypeOf(want.value(QStringLiteral("frame_type")).toString());
            const QString wantPayload = want.value(QStringLiteral("payload_hex")).toString();
            if (sent.at(i).frameType != wantType) {
                std::fprintf(stderr, "FAIL: 用例 %s 第 %d 帧类型是 0x%02x，向量期望 0x%02x\n",
                             name.toUtf8().constData(), i, sent.at(i).frameType, wantType);
                ++failures;
            }
            const QString gotPayload = QString::fromLatin1(sent.at(i).payload.toHex());
            if (gotPayload != wantPayload) {
                std::fprintf(stderr, "FAIL: 用例 %s 第 %d 帧载荷是 %s，向量期望 %s\n",
                             name.toUtf8().constData(), i, gotPayload.toUtf8().constData(),
                             wantPayload.toUtf8().constData());
                ++failures;
            }
        }
        ++ran;
    }

    std::printf("input_vector_test: 跑了 %d 个用例（from=linux），跳过 %d 个（其它平台）\n", ran, skipped);
    if (ran == 0) {
        std::fprintf(stderr, "FAIL: 一个用例都没跑——向量文件里没有 from_platform=linux 的用例？\n");
        return EXIT_FAILURE;
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("input_vector_test: OK\n");
    return EXIT_SUCCESS;
}
