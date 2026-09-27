cmake_minimum_required(VERSION 3.20)
project(flowgrid-deepin VERSION 1.0.0 LANGUAGES CXX)

# deepin client: DTK (C++ widgets) UI + Rust core via the C ABI bridge in
# native/ (staticlib) + the Go protocol c-archive. No QML, no cxx-qt.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# glibc >= 2.34 folds pthreads into libc, so FindThreads may omit -pthread;
# without it gcc's gthread headers fall back to single-thread types and
# libstdc++ breaks. Force the flag project-wide.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_compile_options(-pthread)

# The user-level dev tree (~/.local/qt6-dev) mixes libc headers with dev
# headers; find_path hits them for OpenGL/OpenSSL/Cups/Vulkan and those
# -isystem dirs shadow the system glibc, breaking libstdc++'s gthread types.
# Confine every such include dir to a "safe" dir holding only GL/KHR/cups.
set(FG_SAFE_INC ${CMAKE_BINARY_DIR}/safe-include)
file(MAKE_DIRECTORY ${FG_SAFE_INC})
foreach(_d GL KHR cups)
  if(EXISTS "$ENV{HOME}/.local/qt6-dev/usr/include/${_d}")
    file(COPY "$ENV{HOME}/.local/qt6-dev/usr/include/${_d}" DESTINATION ${FG_SAFE_INC})
  endif()
endforeach()
foreach(_v OPENGL_INCLUDE_DIR OPENGL_EGL_INCLUDE_DIR OPENSSL_INCLUDE_DIR CUPS_INCLUDE_DIR Vulkan_INCLUDE_DIR)
  set(${_v} ${FG_SAFE_INC} CACHE PATH "" FORCE)
endforeach()

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus)

# FindOpenGL refuses to create the legacy OpenGL::GL target on this host
# (header layout quirk); Qt6Gui's FindWrapOpenGL needs it, so provide it.
if(NOT TARGET OpenGL::GL)
    add_library(OpenGL::GL UNKNOWN IMPORTED)
    # CMAKE_PREFIX_PATH points at <qtroot>/lib/<triplet>/cmake; GL lives two levels up.
    set(FG_GL_SEARCH "")
    foreach(_p ${CMAKE_PREFIX_PATH})
        get_filename_component(_libdir "${_p}/../.." ABSOLUTE)
        list(APPEND FG_GL_SEARCH "${_libdir}")
    endforeach()
    find_library(FG_GL_LIBRARY NAMES GL libGL.so.1 HINTS ${FG_GL_SEARCH} /usr/lib/x86_64-linux-gnu)
    if(NOT FG_GL_LIBRARY)
        set(FG_GL_LIBRARY /usr/lib/x86_64-linux-gnu/libGL.so.1)
    endif()
    set_target_properties(OpenGL::GL PROPERTIES IMPORTED_LOCATION "${FG_GL_LIBRARY}")
endif()

# DTK6: dev packages normally provide the CMake configs; when the headers and
# .so links live in a non-system root (FLOWGRID_QT6_DEV_ROOT), CMake picks them
# up through CMAKE_PREFIX_PATH. We only take include dirs from the DTK targets
# and link the runtime sonames directly, which keeps NEEDED entries correct.
find_package(Dtk6Widget QUIET)
find_package(Dtk6Gui QUIET)
find_package(Dtk6Core QUIET)

set(DTK6_INCLUDE_DIRS "")
if(TARGET Dtk6::Widget)
    get_target_property(_dtk_w_inc Dtk6::Widget INTERFACE_INCLUDE_DIRECTORIES)
    list(APPEND DTK6_INCLUDE_DIRS ${_dtk_w_inc})
endif()
if(TARGET Dtk6::Gui)
    get_target_property(_dtk_g_inc Dtk6::Gui INTERFACE_INCLUDE_DIRECTORIES)
    list(APPEND DTK6_INCLUDE_DIRS ${_dtk_g_inc})
endif()
if(TARGET Dtk6::Core)
    get_target_property(_dtk_c_inc Dtk6::Core INTERFACE_INCLUDE_DIRECTORIES)
    list(APPEND DTK6_INCLUDE_DIRS ${_dtk_c_inc})
endif()
if(NOT DTK6_INCLUDE_DIRS)
    message(FATAL_ERROR "DTK6 dev headers not found (install libdtk6widget-dev / libdtk6gui-dev / libdtk6core-dev)")
endif()

# ── Go protocol c-archive (shared with the KDE client) ────────────────────
set(GO_PROTOCOL_DIR ${CMAKE_SOURCE_DIR}/../protocol)
set(GO_PROTOCOL_A ${GO_PROTOCOL_DIR}/cgo/libflowgrid_protocol.a)
set(GO_PROTOCOL_H ${GO_PROTOCOL_DIR}/cgo/libflowgrid_protocol.h)

add_custom_command(
    OUTPUT ${GO_PROTOCOL_A} ${GO_PROTOCOL_H}
    COMMAND ${CMAKE_COMMAND} -E env CGO_ENABLED=1 go build -buildmode=c-archive -o libflowgrid_protocol.a
    WORKING_DIRECTORY ${GO_PROTOCOL_DIR}/cgo
    COMMENT "Building Go protocol c-archive"
    DEPENDS
        ${GO_PROTOCOL_DIR}/frame.go
        ${GO_PROTOCOL_DIR}/payload.go
        ${GO_PROTOCOL_DIR}/keymap.go
        ${GO_PROTOCOL_DIR}/keymap_rule.go
        ${GO_PROTOCOL_DIR}/identify.go
        ${GO_PROTOCOL_DIR}/discovery.go
        ${GO_PROTOCOL_DIR}/wifidirect.go
        ${GO_PROTOCOL_DIR}/sequence.go
        ${GO_PROTOCOL_DIR}/fragment.go
        ${GO_PROTOCOL_DIR}/heartbeat.go
        ${GO_PROTOCOL_DIR}/reconnect.go
        ${GO_PROTOCOL_DIR}/errors.go
        ${GO_PROTOCOL_DIR}/platform.go
        ${GO_PROTOCOL_DIR}/crypto.go
        ${GO_PROTOCOL_DIR}/framing.go
        ${GO_PROTOCOL_DIR}/apipa.go
)
add_custom_target(go_protocol_build ALL DEPENDS ${GO_PROTOCOL_A} ${GO_PROTOCOL_H})

# ── Rust core C ABI bridge (deepin/native) ────────────────────────────────
# Toolchain env for cargo is captured at configure time: export OPENSSL_DIR,
# OPENSSL_LIB_DIR, OPENSSL_INCLUDE_DIR, PKG_CONFIG_PATH and
# PKG_CONFIG_SYSROOT_DIR before configuring when using a non-system dev root
# (e.g. ~/.local/qt6-dev). With system packages installed the defaults work.
set(OPENSSL_DIR "$ENV{OPENSSL_DIR}" CACHE PATH "OpenSSL root for the Rust bridge build")
set(OPENSSL_LIB_DIR "$ENV{OPENSSL_LIB_DIR}" CACHE PATH "OpenSSL lib dir")
set(OPENSSL_INCLUDE_DIR "$ENV{OPENSSL_INCLUDE_DIR}" CACHE PATH "OpenSSL include dir")
set(PKG_CONFIG_PATH_BRIDGE "$ENV{PKG_CONFIG_PATH}" CACHE STRING "pkg-config path for the Rust bridge build")
set(PKG_CONFIG_SYSROOT_DIR_BRIDGE "$ENV{PKG_CONFIG_SYSROOT_DIR}" CACHE STRING "pkg-config sysroot")

set(NATIVE_DIR ${CMAKE_SOURCE_DIR}/native)

# -DCMAKE_BUILD_TYPE=Release builds the Rust bridge with --release (LTO).
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CARGO_PROFILE_FLAG --release)
    set(CARGO_PROFILE_DIR release)
else()
    set(CARGO_PROFILE_FLAG "")
    set(CARGO_PROFILE_DIR debug)
endif()
set(NATIVE_A ${NATIVE_DIR}/target/${CARGO_PROFILE_DIR}/libflowgrid_deepin_native.a)

add_custom_command(
    OUTPUT ${NATIVE_A}
    COMMAND ${CMAKE_COMMAND} -E env
        OPENSSL_DIR=${OPENSSL_DIR}
        OPENSSL_LIB_DIR=${OPENSSL_LIB_DIR}
        OPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}
        PKG_CONFIG_PATH=${PKG_CONFIG_PATH_BRIDGE}
        PKG_CONFIG_SYSROOT_DIR=${PKG_CONFIG_SYSROOT_DIR_BRIDGE}
        cargo build ${CARGO_PROFILE_FLAG} --manifest-path ${NATIVE_DIR}/Cargo.toml
    COMMENT "Building Rust core C ABI bridge"
    DEPENDS ${NATIVE_DIR}/src/lib.rs ${NATIVE_DIR}/Cargo.toml
)
add_custom_target(deepin_native_build ALL DEPENDS ${NATIVE_A})

# ── UI executable ──────────────────────────────────────────────────────────
add_executable(flowgrid-deepin
    ui/main.cpp
    ui/core_client.cpp
    ui/main_window.cpp
    ui/add_device_dialog.cpp
    ui/latency_panel.cpp
    ui/tray_popup.cpp
    ui/icons.qrc
)

target_include_directories(flowgrid-deepin PRIVATE
    ${DTK6_INCLUDE_DIRS}
    ${NATIVE_DIR}
)
# DTK runtime: find the sonames wherever they live (system or user dev root).
find_library(FG_DTK_WIDGET_LIB dtk6widget PATHS /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu $ENV{HOME}/.local/qt6-dev/usr/lib/x86_64-linux-gnu)
find_library(FG_DTK_GUI_LIB dtk6gui PATHS /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu $ENV{HOME}/.local/qt6-dev/usr/lib/x86_64-linux-gnu)
find_library(FG_SSL_LIB ssl PATHS /usr/lib/x86_64-linux-gnu $ENV{HOME}/.local/qt6-dev/usr/lib/x86_64-linux-gnu)
find_library(FG_CRYPTO_LIB crypto PATHS /usr/lib/x86_64-linux-gnu $ENV{HOME}/.local/qt6-dev/usr/lib/x86_64-linux-gnu)
find_library(FG_DBUS_LIB dbus-1 PATHS /usr/lib/x86_64-linux-gnu $ENV{HOME}/.local/qt6-dev/usr/lib/x86_64-linux-gnu)
find_library(FG_DTK_CORE_LIB dtk6core PATHS /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu $ENV{HOME}/.local/qt6-dev/usr/lib/x86_64-linux-gnu)

target_link_libraries(flowgrid-deepin PRIVATE
    Qt6::Widgets
    Qt6::DBus
    ${FG_DTK_WIDGET_LIB}
    ${FG_DTK_GUI_LIB}
    ${FG_DTK_CORE_LIB}
    ${NATIVE_A}
    ${GO_PROTOCOL_A}
    pthread
    dl
    m
    ${FG_SSL_LIB}
    ${FG_CRYPTO_LIB}
    ${FG_DBUS_LIB}
)
add_dependencies(flowgrid-deepin go_protocol_build deepin_native_build)

# ── Install ────────────────────────────────────────────────────────────────
install(TARGETS flowgrid-deepin DESTINATION bin)
install(FILES resources/flowgrid-deepin.desktop DESTINATION share/applications)
install(FILES resources/icons/flowgrid-deepin.svg DESTINATION share/icons/hicolor/scalable/apps)
# DDE system integration: launch at session start, tray-resident
install(FILES resources/flowgrid-deepin-autostart.desktop DESTINATION /etc/xdg/autostart)
# uinput permissions and the systemd service are shared with the KDE client
install(FILES ${CMAKE_SOURCE_DIR}/../kde/resources/99-flowgrid.rules DESTINATION lib/udev/rules.d)
install(FILES ${CMAKE_SOURCE_DIR}/../kde/resources/flowgrid.service DESTINATION lib/systemd/user)
