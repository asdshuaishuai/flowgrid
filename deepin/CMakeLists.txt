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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus)

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
    src/core/protocolapi.h        src/core/protocolapi.cpp
    src/core/devicemanager.h      src/core/devicemanager.cpp

    src/ui/svgsubset.h            src/ui/svgsubset.cpp
    src/ui/icons.h                src/ui/icons.cpp
    src/ui/widgets.h              src/ui/widgets.cpp
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

# 两个目标的公共配置：头文件路径、链接库、编译选项、宏。
function(flowgrid_configure_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${GO_PROTOCOL_DIR}/cgo)
    target_link_libraries(${target} PRIVATE
        ${FLOWGRID_DTK_LIBS}
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::DBus
        ${GO_PROTOCOL_A}
        Threads::Threads)
    target_compile_options(${target} PRIVATE -Wall -Wextra)
    # 应用身份：深链接、任务栏与单实例都按这个 id。
    target_compile_definitions(${target} PRIVATE
        FLOWGRID_APP_ID="io.github.asdshuaishuai.flowgrid"
        FLOWGRID_VERSION="${PROJECT_VERSION}")
endfunction()

add_executable(flowgrid-deepin src/main.cpp ${FLOWGRID_SOURCES} ${GO_PROTOCOL_A} ${GO_PROTOCOL_H})
flowgrid_configure_target(flowgrid-deepin)

# 界面自检：无头可跑，验那些「截图看不出、按下去才知道」的部分（托盘快捷键、
# Esc 收起浮层、焦点环真的画出来）。不进安装包，只是开发时跑。
add_executable(flowgrid-uitest tests/uitest.cpp ${FLOWGRID_SOURCES} ${GO_PROTOCOL_A} ${GO_PROTOCOL_H})
flowgrid_configure_target(flowgrid-uitest)

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
install(FILES resources/icons/flowgrid.svg
        DESTINATION share/icons/hicolor/scalable/apps)

# DConfig 按固定约定定位 meta 文件：/usr/share/<appId>/<name>.json。
# 这里的 appId="flowgrid"、name="flowgrid-deepin" 来自 src/core/settings.cpp 里的
# DConfig::create() 调用，两处必须一致；放错目录的表现是 create() 返回不可用对象、
# 设置全部静默回退默认值，而不是报错。
install(FILES resources/flowgrid-deepin.json
        DESTINATION share/flowgrid)
