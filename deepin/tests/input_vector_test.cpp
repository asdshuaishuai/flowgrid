// 跨客户端输入行为黄金向量测试：deepin 侧对着协议层生成的那份向量文件比对。
//
// 为什么要有这个：mac / Windows / deepin 各自实现捕获与注入的 HAL，但
// 「某个输入事件在协议层应该变成什么字节」只有协议层知道。以前这条只靠
// 各端单测里的零散断言守着，两端各写各的期望值，行为迟早分叉，而且分叉了
// 也没人知道是哪端错了。
//
// 现在协议层把向量算好落到 protocol/testdata/input_vectors.json，各端读
// **同一份文件**。本测试做两件事：
//   1. 常量镜像对齐：deepin 代码里手抄的帧类型 / 平台 / 角色 / 能力位，逐个
//      跟向量文件里的真值比对——协议层改了而客户端忘了改，这里立刻红
//      （以前没有任何机制保证这件事）。
//   2. 载荷字节对齐：把向量里的输入事件喂给 **cgo 边界**（fg_input_encode），
//      逐个核对产出的帧类型与载荷字节。
//
// 迁移到 oscontrol 之后，本测试的覆盖范围**变窄了但更准了**：
//   · 修饰位 / 鼠标按钮号这些常量，deepin 侧**不再镜像**了——它们随 C++ 的
//     输入引擎一起删掉，改由协议层的 oscontrol/evdev 独家持有。所以这里
//     不再比对它们（没有可比的了）；它们的真值由协议层的向量漂移守卫
//     （protocol/input_vectors_test.go）盯着。
//   · 剩下要盯的是 **FFI 边界**：C++ 调 fg_input_encode 拿到的字节，必须
//     跟协议层自己算出来的一模一样。这一段以前没人盯——它是「各端各写一份
//     载荷」时代的最后一道缝。
//
// 只跑 from_platform == "linux" 的用例（deepin 是 Linux 客户端）；
// 其余平台的用例留给 mac / Windows 客户端读同一份文件。
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

// 向量里的一个输入事件 → 喂给 fg_input_encode 的字段。
//
// kind 传**帧类型**常量：协议层里操作类型与帧类型一一对应且同值，客户端
// 因此不必再镜像一套「操作类型」常量。
std::uint8_t kindToFrameType(const QString &kind)
{
    if (kind == QLatin1String("key_down")) return proto::frametype::kKeyDown;
    if (kind == QLatin1String("key_up")) return proto::frametype::kKeyUp;
    if (kind == QLatin1String("mouse_move")) return proto::frametype::kMouseMove;
    if (kind == QLatin1String("mouse_button")) return proto::frametype::kMouseButton;
    if (kind == QLatin1String("scroll")) return proto::frametype::kScroll;
    return 0;
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

        const std::uint8_t fromPlatform = platformOf(from);
        if (fromPlatform == 0) {
            std::fprintf(stderr, "FAIL: 用例 %s 的本机平台 %s 不认识\n", name.toUtf8().constData(),
                         from.toUtf8().constData());
            ++failures;
            continue;
        }

        // FFI 边界：把向量里的输入事件喂给 fg_input_encode，核对产出的帧类型
        // 与载荷字节。这正是「各端各写一份载荷」时代最后剩下的那条缝。
        const QJsonArray events = caseObject.value(QStringLiteral("events")).toArray();
        const QJsonArray expects = caseObject.value(QStringLiteral("expect")).toArray();
        std::vector<std::uint8_t> sentTypes;
        std::vector<QByteArray> sentPayloads;
        for (const QJsonValue &eventValue : events) {
            const QJsonObject event = eventValue.toObject();
            const std::uint8_t kind = kindToFrameType(event.value(QStringLiteral("kind")).toString());
            if (kind == 0) {
                std::fprintf(stderr, "FAIL: 用例 %s 有不认识的事件\n", name.toUtf8().constData());
                ++failures;
                break;
            }
            std::uint8_t frameType = 0;
            std::uint16_t outKey = 0;
            std::uint8_t outModifiers = 0;
            const proto::GoBuffer payload = proto::encodeInput(
                kind,
                static_cast<std::uint16_t>(event.value(QStringLiteral("key")).toDouble()),
                static_cast<std::uint8_t>(event.value(QStringLiteral("modifiers")).toDouble()),
                static_cast<std::int16_t>(event.value(QStringLiteral("delta_x")).toDouble()),
                static_cast<std::int16_t>(event.value(QStringLiteral("delta_y")).toDouble()),
                static_cast<std::uint8_t>(event.value(QStringLiteral("button")).toDouble()),
                event.value(QStringLiteral("pressed")).toBool(),
                fromPlatform, peerPlatform, nullptr, QString(),
                &frameType, &outKey, &outModifiers);
            if (payload.isNull()) {
                std::fprintf(stderr, "FAIL: 用例 %s 编码失败\n", name.toUtf8().constData());
                ++failures;
                break;
            }
            sentTypes.push_back(frameType);
            sentPayloads.push_back(payload.toByteArray());
        }

        if (static_cast<int>(sentTypes.size()) != static_cast<int>(expects.size())) {
            std::fprintf(stderr, "FAIL: 用例 %s 产出 %lld 帧，向量期望 %lld 帧\n",
                         name.toUtf8().constData(), static_cast<long long>(sentTypes.size()),
                         static_cast<long long>(expects.size()));
            ++failures;
            continue;
        }
        for (int i = 0; i < expects.size(); ++i) {
            const QJsonObject want = expects.at(i).toObject();
            const std::uint8_t wantType = frameTypeOf(want.value(QStringLiteral("frame_type")).toString());
            const QString wantPayload = want.value(QStringLiteral("payload_hex")).toString();
            if (sentTypes.at(i) != wantType) {
                std::fprintf(stderr, "FAIL: 用例 %s 第 %d 帧类型是 0x%02x，向量期望 0x%02x\n",
                             name.toUtf8().constData(), i, sentTypes.at(i), wantType);
                ++failures;
            }
            const QString gotPayload = QString::fromLatin1(sentPayloads.at(i).toHex());
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
