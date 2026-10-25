cmake_minimum_required(VERSION 3.20)
project(flowgrid-deepin VERSION 1.1.0 LANGUAGES CXX)

# deepin (DDE 25) 专属客户端：DTK6 原生 C++ Widgets + Go 协议 c-archive。
# 不用 QML——设计稿是逐像素给定几何的定尺浮板，Widget 树更容易 1:1 复刻。
#
# 约束（来自 AGENTS.md）：
#   · 协议只经 protocol/cgo 消费，本目录不实现任何帧编解码；
#   · 配色只经 DPalette 语义角色，禁止硬编码主题色；
#   · 窗口几何来自 design/unified/tokens.css，不得擅自改动。

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# glibc >= 2.34 把 pthread 并入 libc，FindThreads 可能不给 -pthread，
# 而 gcc 的 gthread 头在缺 -pthread 时会退化成单线程类型，把 libstdc++ 弄坏。
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_compile_options(-pthread)

# ── Qt6 / DTK6 定位 ────────────────────────────────────────────────────
# 本机的 Qt6 与 DTK6 开发文件（头、库、CMake 配置）都只存在于用户级
# dd-sysroot 下，系统目录没有。因此 find_package 的搜索根必须显式指过去，
# 否则会退化成"找不到 Qt6"。
set(FLOWGRID_DTK_SYSROOT "$ENV{HOME}/.local/dd-sysroot/usr" CACHE PATH
    "Qt6/DTK6 development sysroot")

if(EXISTS "${FLOWGRID_DTK_SYSROOT}/lib/x86_64-linux-gnu/cmake")
    list(APPEND CMAKE_PREFIX_PATH "${FLOWGRID_DTK_SYSROOT}/lib/x86_64-linux-gnu/cmake")
endif()
list(APPEND CMAKE_PREFIX_PATH "${FLOWGRID_DTK_SYSROOT}")
list(APPEND CMAKE_PREFIX_PATH "${FLOWGRID_DTK_SYSROOT}/lib/x86_64-linux-gnu")

# sysroot 里没有 libc 头（无 stdio.h），因此只加 -I、绝不能 -isystem 整个
# include 目录——那会让 sysroot 的 c++ 头遮蔽系统 glibc，libstdc++ 随即炸掉。
# 这是旧客户端在 ~/.local/qt6-dev 上踩过的坑，这里对 dd-sysroot 同样适用。
#
# 注意必须加四个子目录而不是只加 dtk6 根：DTK 的头文件是 DGuiApplicationHelper
# / DPalette / DApplication 这种「无扩展名」形式，只有直接命中所在子目录，
# `#include <DGuiApplicationHelper>` 才成立。
if(EXISTS "${FLOWGRID_DTK_SYSROOT}/include/dtk6")
    foreach(_sub DCore DGui DLog DWidget)
        if(EXISTS "${FLOWGRID_DTK_SYSROOT}/include/dtk6/${_sub}")
            include_directories(SYSTEM "${FLOWGRID_DTK_SYSROOT}/include/dtk6/${_sub}")
        endif()
    endforeach()
endif()
if(EXISTS "${FLOWGRID_DTK_SYSROOT}/include/x86_64-linux-gnu/qt6")
    include_directories(SYSTEM "${FLOWGRID_DTK_SYSROOT}/include/x86_64-linux-gnu/qt6")
endif()

# 运行期与链接期统一走 sysroot 的库目录，避免混用系统 Qt。
set(CMAKE_BUILD_RPATH "${FLOWGRID_DTK_SYSROOT}/lib/x86_64-linux-gnu")
set(CMAKE_INSTALL_RPATH "${FLOWGRID_DTK_SYSROOT}/lib/x86_64-linux-gnu")

# Network 是智能连接（LAN 私有链发现）的 UDP 多播适配器要的：QUdpSocket /
# QHostAddress / QNetworkDatagram 都在 Qt6::Network 里，不在 Core 里。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Network)

find_package(Dtk6Core QUIET)
find_package(Dtk6Gui QUIET)
find_package(Dtk6Widget QUIET)
find_package(Dtk6Log QUIET)

# DTK6 导出的目标名是 Dtk6::Log / Gui / Core / Widget（不带组件名前缀），
# 与 KDE 的 Dtk5::Dtk6Widgets 不同，这里按实际导出名取，取不到再退回 soname 直连。
set(FLOWGRID_DTK_LIBS "")
set(_dtk_comps Log Gui Core Widget)
set(_dtk_sonames libdtk6log.so libdtk6gui.so libdtk6core.so libdtk6widget.so)
list(LENGTH _dtk_comps _n)
math(EXPR _last "${_n} - 1")
foreach(_i RANGE ${_last})
    list(GET _dtk_comps ${_i} _comp)
    list(GET _dtk_sonames ${_i} _soname)
    if(TARGET Dtk6::${_comp})
        list(APPEND FLOWGRID_DTK_LIBS Dtk6::${_comp})
    elseif(EXISTS "${FLOWGRID_DTK_SYSROOT}/lib/x86_64-linux-gnu/${_soname}")
        list(APPEND FLOWGRID_DTK_LIBS "${FLOWGRID_DTK_SYSROOT}/lib/x86_64-linux-gnu/${_soname}")
    endif()
endforeach()
if(NOT FLOWGRID_DTK_LIBS)
    message(FATAL_ERROR
        "未找到 DTK6（dtk6core/gui/widget/log）。请安装 libdtk6widget-dev 等开发包，"
        "或用 -DFLOWGRID_DTK_SYSROOT=<prefix> 指向 Qt6/DTK6 开发树。")
endif()
message(STATUS "DTK6: ${FLOWGRID_DTK_LIBS}")

# ── Go 协议 c-archive ─────────────────────────────────────────────────
# 唯一合法的协议接入面（AGENTS.md §28）。若尚未构建，尝试调用 go 生成；
# go 不在 PATH 时给出可执行的提示而不是含糊报错。
set(GO_PROTOCOL_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/../protocol" CACHE PATH "Go 协议库目录")
set(GO_PROTOCOL_A     "${GO_PROTOCOL_DIR}/cgo/libflowgrid_protocol.a")
set(GO_PROTOCOL_H     "${GO_PROTOCOL_DIR}/cgo/libflowgrid_protocol.h")

find_program(FLOWGRID_GO NAMES go
             HINTS "$ENV{HOME}/sdk/go/bin" "$ENV{GOROOT}/bin")
if(FLOWGRID_GO)
    message(STATUS "Go: ${FLOWGRID_GO}")
    # 依赖整个协议包的 .go 文件，而不是手列几个：手列的清单迟早会漏，
    # 漏掉的那次改动不会触发重建，于是客户端链接的是过期 archive——
    # 而 c-archive 不会报错，只会让新函数在运行期「不存在」。
    file(GLOB GO_PROTOCOL_SRC CONFIGURE_DEPENDS "${GO_PROTOCOL_DIR}/*.go")
    # cgo 包自己也是一个 Go 包（bridge.go 就住在那儿），上面那条 glob 只盖到
    # 协议根包：改了 FFI 桥不重建 archive，客户端链接的仍是旧实现——正是这段
    # 注释警告的「新函数在运行期不存在」，只不过漏的是 cgo 那一半。
    file(GLOB GO_CGO_SRC CONFIGURE_DEPENDS "${GO_PROTOCOL_DIR}/cgo/*.go")
    add_custom_command(
        OUTPUT ${GO_PROTOCOL_A} ${GO_PROTOCOL_H}
        COMMAND ${CMAKE_COMMAND} -E env
                CGO_ENABLED=1
                "CC=/usr/bin/gcc" "CXX=/usr/bin/g++"
                ${FLOWGRID_GO} build -buildmode=c-archive -o libflowgrid_protocol.a .
        WORKING_DIRECTORY ${GO_PROTOCOL_DIR}/cgo
        COMMENT "Building Go protocol c-archive"
        DEPENDS ${GO_PROTOCOL_SRC} ${GO_CGO_SRC}
        VERBATIM)
else()
    message(WARNING "未找到 go，跳过自动构建 c-archive；将使用已存在的 ${GO_PROTOCOL_A}")
endif()

if(NOT EXISTS "${GO_PROTOCOL_A}")
    message(FATAL_ERROR
        "协议 c-archive 不存在：${GO_PROTOCOL_A}\n"
        "请先执行：cd ${GO_PROTOCOL_DIR}/cgo && CGO_ENABLED=1 CC=/usr/bin/gcc CXX=/usr/bin/g++ go build -buildmode=c-archive -o libflowgrid_protocol.a .\n"
        "（注意 CC/CXX 必须指向真正的 C 编译器：若 PATH 上的 gcc 是包装到 g++ 的脚本，"
        "cgo 编译 .c 会走 C++ 前端并在 -Werror 下失败。）")
endif()

# ── 目标 ──────────────────────────────────────────────────────────────
# 不含 main.cpp：应用与自检两个目标共用同一批界面源码，自检带自己的 main。
# 拆开是为了让自检能直接用真实控件、真实事件路径，而不是另写一套假界面。
set(FLOWGRID_SOURCES
    src/core/designtokens.h
    src/core/settings.h           src/core/settings.cpp
    src/core/theme.h              src/core/theme.cpp
    src/core/hosttheme.h          src/core/hosttheme.cpp
    src/core/hostinfo.h           src/core/hostinfo.cpp
    src/core/linkprobe.h          src/core/linkprobe.cpp
    src/core/protocolapi.h        src/core/protocolapi.cpp
    src/core/lanidlink.h          src/core/lanidlink.cpp
    src/core/laniddiscovery.h     src/core/laniddiscovery.cpp
    src/core/peercontroller.h     src/core/peercontroller.cpp
    src/core/devicemanager.h      src/core/devicemanager.cpp
    src/core/input/oscontrol.h     src/core/input/oscontrol.cpp
    src/core/keyrules.h            src/core/keyrules.cpp
    src/core/input/peer_session.h     src/core/input/peer_session.cpp

    src/ui/svgsubset.h            src/ui/svgsubset.cpp
    src/ui/icons.h                src/ui/icons.cpp
    src/ui/widgets.h              src/ui/widgets.cpp
    src/ui/surface.h              src/ui/surface.cpp
    src/ui/panel.h                src/ui/panel.cpp
    src/ui/traypopup.h            src/ui/traypopup.cpp
    src/ui/hotkeys.h              src/ui/hotkeys.cpp
    src/ui/connectionspanel.h     src/ui/connectionspanel.cpp
    src/ui/latencypanel.h         src/ui/latencypanel.cpp
    src/ui/settingspanel.h        src/ui/settingspanel.cpp
    src/ui/keymappanel.h          src/ui/keymappanel.cpp
    src/ui/pairingpanel.h         src/ui/pairingpanel.cpp
    src/ui/clipboardpanel.h      src/ui/clipboardpanel.cpp
    src/ui/filespanel.h          src/ui/filespanel.cpp

    resources/flowgrid.qrc
)

# ── DConfig（DSG）身份 ──────────────────────────────────────────────────
# appId 决定 meta 的目录，name 决定文件名。三处必须一致：
#   · src/core/settings.cpp 的 DConfig::create(appId, name)
#   · 下面的 install(FILES ... DESTINATION ...)
#   · tests/uitest.cpp 里的对表检查
# 所以**以本处为唯一来源**，编译期当成宏发给两个目标——否则改了 settings.cpp
# 里的 appId 却忘了改安装目录，就会得到一次「设置静默失效」的回归。
#
# 目录名不是拍脑袋的：DTK 自己给的（DConfigMeta::applicationMetaDirs("", appId)
# 返回 /usr/share/dsg/configs/<appId>），dconfig 守护进程也只扫
# /usr/share/dsg 与 /var/lib/linglong/entries/share/dsg
# （见 dde-dconfig-daemon.service 的 DSG_DATA_DIRS）。
set(FLOWGRID_DCONFIG_APPID "flowgrid")
set(FLOWGRID_DCONFIG_NAME "flowgrid-deepin")
# meta 的**根目录**——守护进程 DSG_DATA_DIRS 里的一个条目，完整布局是
#     <根>/configs/<appId>/<name>.json
# 默认 /usr/share/dsg：标准发行版上守护进程扫的就是它。
#
# 但 ostree 系统（deepin 25 的不可变安装）把 /usr 挂成**只读 overlay**，那个
# 目录物理上建不出来，cmake 会报 "file cannot create directory"。这种机器上
# 改用 /usr/local/share/dsg（/usr/local 是独立可写挂载），并由 install.sh
# 把这个根注册进守护进程的 DSG_DATA_DIRS。
set(FLOWGRID_DCONFIG_META_ROOT "/usr/share/dsg" CACHE STRING
    "DConfig meta 根目录（dconfig 守护进程 DSG_DATA_DIRS 里的一个条目）")
set(FLOWGRID_DCONFIG_META_DIR "${FLOWGRID_DCONFIG_META_ROOT}/configs/${FLOWGRID_DCONFIG_APPID}")

# 应用图标的安装目录。install() 与运行期查找（main.cpp 的 installedIconPaths）
# 用的是同一个值——写成两处字面量，早晚会对不上，而症状是「图标明明装好了，
# 托盘里却没有」，属于最难查的一类。
#
# 必须是**绝对路径**：DDE 的 dock 画托盘图标是按 SNI 的 IconName 去它自己的
# 图标主题里查的，查不到就不画、也不回退 IconPixmap。所以托盘图标最终给的是
# 这个目录下的**文件绝对路径**，而不是主题名「flowgrid」。
set(FLOWGRID_ICON_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps"
    CACHE STRING "应用图标安装目录（install() 与运行期查找共用）")

# 两个目标的公共配置：头文件路径、链接库、编译选项、宏。
function(flowgrid_configure_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${GO_PROTOCOL_DIR}/cgo)
    target_link_libraries(${target} PRIVATE
        ${FLOWGRID_DTK_LIBS}
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::DBus Qt6::Network
        ${GO_PROTOCOL_A}
        Threads::Threads)
    target_compile_options(${target} PRIVATE -Wall -Wextra)
    # 应用身份：深链接、任务栏与单实例都按这个 id。
    target_compile_definitions(${target} PRIVATE
        FLOWGRID_APP_ID="io.github.asdshuaishuai.flowgrid"
        FLOWGRID_VERSION="${PROJECT_VERSION}"
        FLOWGRID_DCONFIG_APPID="${FLOWGRID_DCONFIG_APPID}"
        FLOWGRID_DCONFIG_NAME="${FLOWGRID_DCONFIG_NAME}"
        FLOWGRID_INSTALL_ICON_DIR="${FLOWGRID_ICON_INSTALL_DIR}")
endfunction()

add_executable(flowgrid-deepin src/main.cpp ${FLOWGRID_SOURCES} ${GO_PROTOCOL_A} ${GO_PROTOCOL_H})
flowgrid_configure_target(flowgrid-deepin)

# 界面自检：无头可跑，验那些「截图看不出、按下去才知道」的部分（托盘快捷键、
# Esc 收起浮层、焦点环真的画出来）。不进安装包，只是开发时跑。
add_executable(flowgrid-uitest tests/uitest.cpp ${FLOWGRID_SOURCES} ${GO_PROTOCOL_A} ${GO_PROTOCOL_H})
flowgrid_configure_target(flowgrid-uitest)
# 自检里要读 meta 源文件、并核对「装的目录」与「DTK 期望的目录」是否一致。
# CANONICAL 是 DTK 客户端侧认的规范路径（/usr/share/dsg/configs/<appId>）；
# 在 /usr 只读的 ostree 机器上实际装的根会不同，那时要靠守护进程的
# DSG_DATA_DIRS 里多出的那个根，自检只核对布局形状与格式。
target_compile_definitions(flowgrid-uitest PRIVATE
    FLOWGRID_META_FILE="${CMAKE_CURRENT_SOURCE_DIR}/resources/flowgrid-deepin.json"
    FLOWGRID_META_DIR="${FLOWGRID_DCONFIG_META_DIR}"
    FLOWGRID_DCONFIG_CANONICAL_DIR="/usr/share/dsg/configs/${FLOWGRID_DCONFIG_APPID}")

# 自检要造「自发键事件」才能让 Qt 去查快捷键表，而 QEvent::setSpontaneous() 在
# Qt 6 里是私有的——公开的转发头在 QtTest 模块里（纯头文件，不链接任何库）。
# 没有 QtTest 开发包时自检降级为「只核对绑定，不核对派发」，并如实打印说明。
find_package(Qt6 QUIET COMPONENTS Test)
if(TARGET Qt6::Test)
    target_link_libraries(flowgrid-uitest PRIVATE Qt6::Test)
    target_compile_definitions(flowgrid-uitest PRIVATE FLOWGRID_HAVE_QTTEST=1)
else()
    message(STATUS "未找到 Qt6Test：界面自检将跳过快捷键派发检查（只核对绑定）")
endif()

enable_testing()
add_test(NAME uitest COMMAND flowgrid-uitest)

# OS 控制层（cgo 桥）单测：句柄建立与销毁、错误路径、错误信息可取。
#
# 只测**不需要 /dev/uinput、不需要 input 组**的那部分——真机才有的权限不该是
# 单测的前提（真机那条在 input_hal_test.cpp）。
#
# 原来的「键鼠映射表单测」与「转发通路单测」都随 C++ 输入引擎一起删了：
# HID↔evdev 表与重映射/组帧语义都搬到了协议层
# （protocol/oscontrol/evdev + protocol/oscontrol），由 Go 侧的测试守着。
add_executable(flowgrid-inputoscontroltest tests/input_oscontrol_test.cpp
    src/core/input/oscontrol.h src/core/input/oscontrol.cpp
    src/core/protocolapi.h src/core/protocolapi.cpp
    ${GO_PROTOCOL_A} ${GO_PROTOCOL_H})
target_include_directories(flowgrid-inputoscontroltest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GO_PROTOCOL_DIR}/cgo)
target_link_libraries(flowgrid-inputoscontroltest PRIVATE
    Qt6::Core ${GO_PROTOCOL_A} Threads::Threads)
target_compile_options(flowgrid-inputoscontroltest PRIVATE -Wall -Wextra)
add_test(NAME inputoscontrol COMMAND flowgrid-inputoscontroltest)

# 对端会话端到端单测：真实 TCP（回环）+ IDENTIFY 握手 + 帧往返。
# 不需要第二台机器、不需要 uinput 权限——被控端与控制端都在本进程里。
add_executable(flowgrid-peersessiontest tests/peer_session_test.cpp
    src/core/input/peer_session.h src/core/input/peer_session.cpp
    src/core/protocolapi.h src/core/protocolapi.cpp
    ${GO_PROTOCOL_A} ${GO_PROTOCOL_H})
target_include_directories(flowgrid-peersessiontest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GO_PROTOCOL_DIR}/cgo)
target_link_libraries(flowgrid-peersessiontest PRIVATE
    Qt6::Core Qt6::Network ${GO_PROTOCOL_A} Threads::Threads)
target_compile_options(flowgrid-peersessiontest PRIVATE -Wall -Wextra)
add_test(NAME peersession COMMAND flowgrid-peersessiontest)

# 协议级真实操作测试：捕获事件序列 → 协议层编码 → 真实 TCP → 对端解码。
# 走完整条链路，只把 OS HAL（evdev/uinput）换成假的——那两样要权限，
# 由 input_hal_test 单独验（权限不足时它如实报跳过）。
add_executable(flowgrid-inpute2etest tests/input_protocol_e2e_test.cpp
    src/core/input/peer_session.h src/core/input/peer_session.cpp
    src/core/protocolapi.h src/core/protocolapi.cpp
    ${GO_PROTOCOL_A} ${GO_PROTOCOL_H})
target_include_directories(flowgrid-inpute2etest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GO_PROTOCOL_DIR}/cgo)
target_link_libraries(flowgrid-inpute2etest PRIVATE
    Qt6::Core Qt6::Network ${GO_PROTOCOL_A} Threads::Threads)
target_compile_options(flowgrid-inpute2etest PRIVATE -Wall -Wextra)
add_test(NAME inpute2e COMMAND flowgrid-inpute2etest)

# 跨客户端输入行为黄金向量测试：deepin 侧对着协议层生成的那份向量文件比对。
#
# 同一份文件（protocol/testdata/input_vectors.json）也给 mac / Windows 客户端用：
# 「两端行为一致」从此由机器盯着，而不是靠各端单测里各写各的期望值。
# 这条测试同时干两件事：核对 deepin 手抄的常量（帧类型/平台/角色/能力位）与
# 协议层一致，以及核对 **cgo 边界**（fg_input_encode）产出的载荷字节与向量
# 一致——迁移到 oscontrol 之后，deepin 侧不再镜像修饰位/按钮号，那份真值由
# 协议层的向量漂移守卫盯着。
add_executable(flowgrid-inputvectortest tests/input_vector_test.cpp
    src/core/protocolapi.h src/core/protocolapi.cpp
    ${GO_PROTOCOL_A} ${GO_PROTOCOL_H})
target_include_directories(flowgrid-inputvectortest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GO_PROTOCOL_DIR}/cgo)
# 向量文件路径写进编译期定义：测试不靠「当前工作目录在哪」找文件。
target_compile_definitions(flowgrid-inputvectortest PRIVATE
    FLOWGRID_VECTOR_FILE="${GO_PROTOCOL_DIR}/testdata/input_vectors.json")
target_link_libraries(flowgrid-inputvectortest PRIVATE
    Qt6::Core Qt6::Network ${GO_PROTOCOL_A} Threads::Threads)
target_compile_options(flowgrid-inputvectortest PRIVATE -Wall -Wextra)
add_test(NAME inputvector COMMAND flowgrid-inputvectortest)

# HAL 层真机测试：真实 evdev 捕获 → 真实 uinput 注入 → 读回验证。
# 需要 /dev/uinput 可写 + /dev/input/event* 可读；权限不足时**如实报跳过**
# （退出码 77），不假装通过。
add_executable(flowgrid-inputhaltest tests/input_hal_test.cpp
    src/core/input/oscontrol.h src/core/input/oscontrol.cpp
    src/core/input/peer_session.h src/core/input/peer_session.cpp
    src/core/protocolapi.h src/core/protocolapi.cpp
    ${GO_PROTOCOL_A} ${GO_PROTOCOL_H})
target_include_directories(flowgrid-inputhaltest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GO_PROTOCOL_DIR}/cgo)
target_link_libraries(flowgrid-inputhaltest PRIVATE
    Qt6::Core Qt6::Network ${GO_PROTOCOL_A} Threads::Threads)
target_compile_options(flowgrid-inputhaltest PRIVATE -Wall -Wextra)

# 额外验证「X 光标真的动了」：注入类的东西光看返回值不算数，得看桌面上的
# 光标有没有跟着走。X11 是可选依赖——没有它就只跑 evdev 那一半验证，
# 测试照常能编能跑（不因为缺个可选依赖把整个构建搞挂）。
find_path(FLOWGRID_X11_INCLUDE_DIR X11/Xlib.h
    HINTS "${FLOWGRID_DTK_SYSROOT}/include" /usr/include)
find_library(FLOWGRID_X11_LIBRARY NAMES X11
    HINTS "${FLOWGRID_DTK_SYSROOT}/lib/x86_64-linux-gnu" /usr/lib/x86_64-linux-gnu)
if(FLOWGRID_X11_INCLUDE_DIR AND FLOWGRID_X11_LIBRARY)
    target_compile_definitions(flowgrid-inputhaltest PRIVATE FLOWGRID_HAL_X11=1)
    target_include_directories(flowgrid-inputhaltest SYSTEM PRIVATE ${FLOWGRID_X11_INCLUDE_DIR})
    target_link_libraries(flowgrid-inputhaltest PRIVATE ${FLOWGRID_X11_LIBRARY})
else()
    message(STATUS "flowgrid: 没找到 X11，HAL 测试跳过「光标位移」那一项验证")
endif()

add_test(NAME inputhal COMMAND flowgrid-inputhaltest)
# 退出码 77 = skip：ctest 与 dev.sh 都要认这个约定，别把它当成失败。
set_tests_properties(inputhal PROPERTIES SKIP_RETURN_CODE 77)

install(TARGETS flowgrid-deepin RUNTIME DESTINATION bin)
install(FILES resources/flowgrid-deepin.desktop
        DESTINATION share/applications)
# 自启项必须落到 /etc/xdg/autostart——绝对路径 destination（相对路径会被拼到
# CMAKE_INSTALL_PREFIX 下）。装进 share/applications 是错的：那是「菜单项」目录，
# 而这个文件 NoDisplay=true，结果既不进菜单也不自启，成了一枚死文件。
# 与 resources/flowgrid-deepin-autostart.desktop 里的 NoDisplay/X-GNOME-Autostart
# 设置配套；用户关掉设置屏的「开机自启」时，是在 ~/.config/autostart 写一个同名
# 文件 Hidden=true 覆盖它（见 settingspanel.cpp）。
install(FILES resources/flowgrid-deepin-autostart.desktop
        DESTINATION /etc/xdg/autostart)
# uinput 权限规则：键鼠软模拟要写 /dev/uinput，普通用户默认没这个权限，
# 引擎的 setupUinput() 会直接失败（lastError 里写着原因）。规则把
# /dev/uinput 交给 input 组，用户加入该组后即可注入。
#
# 同样是绝对路径（DESTINATION 相对会被拼到 CMAKE_INSTALL_PREFIX 下），
# 落在 udev 只认的那一个目录：/etc/udev/rules.d。
# 装完要 udevadm control --reload-rules 才生效——install.sh 里那一步跟着做。
install(FILES resources/99-flowgrid-input.rules
        DESTINATION /etc/udev/rules.d)
install(FILES resources/icons/flowgrid.svg
        DESTINATION ${FLOWGRID_ICON_INSTALL_DIR})
# 键位规则表（协议 §11 格式）：装成**系统级默认规则**。
#
# 落在 /usr/local/share/flowgrid/ 而不是 /usr/share/flowgrid/：deepin 25 的
# ostree 机器上 /usr 是只读 overlay，建不出 /usr/share/flowgrid/；/usr/local
# 可写。查找顺序（core/keyrules.cpp）：
#   用户级 ~/.config/flowgrid/keymap.yaml
#     → /usr/local/share/flowgrid/keymap.yaml（本文件装到的地方）
#     → /usr/share/flowgrid/keymap.yaml（发行版打包的）
#     → 应用自带的 :/keymap.yaml（没装也能跑）
#
# 装这份文件只是为了让管理员看得到、改得动；内容与应用自带的那份一致，
# 用户级那份一存在就以用户级的为准。
install(FILES resources/keymap.yaml
        DESTINATION share/flowgrid)
# DConfig（DSG）按固定约定定位 meta 文件：
#     <FLOWGRID_DCONFIG_META_ROOT>/configs/<appId>/<name>.json
# 根目录默认 /usr/share/dsg，是 DTK 自己给的（不是猜的）：
#     DConfigMeta::applicationMetaDirs("", appId) → /usr/share/dsg/configs/<appId>
# dconfig 守护进程只扫 DSG_DATA_DIRS 里的那几个根（见它的 systemd 单元），
# 装到根之外它看不见。appId / name 见上面 FLOWGRID_DCONFIG_APPID /
# FLOWGRID_DCONFIG_NAME——那是唯一来源，settings.cpp 编译时拿的是同一份，
# 不会出现「代码改了、安装目录没改」这种静默失效。
# 放错位置的表现是 create() 返回不可用对象、设置全部静默回退默认值，
# 而不是报错。
#
# DESTINATION 必须是**绝对路径**：相对路径会被拼到 CMAKE_INSTALL_PREFIX 下。
install(FILES resources/flowgrid-deepin.json
        DESTINATION ${FLOWGRID_DCONFIG_META_DIR})
