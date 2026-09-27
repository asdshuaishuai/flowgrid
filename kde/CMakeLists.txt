cmake_minimum_required(VERSION 3.20)
project(flowgrid VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Quick Network Bluetooth)

# Find KF6
find_package(KF6 QUIET COMPONENTS Kirigami Config CoreAddons WindowSystem StatusNotifierItem)

# Find ECM
find_package(ECM QUIET)
if(ECM_FOUND)
    list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
endif()

# Go Protocol: build c-archive via custom command
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
)

add_custom_target(go_protocol_build ALL DEPENDS ${GO_PROTOCOL_A} ${GO_PROTOCOL_H})

# Rust build via Cargo
set(CARGO_TARGET_DIR ${CMAKE_SOURCE_DIR}/rust/target)
set(FLOWGRID_APP ${CARGO_TARGET_DIR}/release/flowgrid-app)

add_custom_command(
    OUTPUT ${FLOWGRID_APP}
    COMMAND ${CMAKE_COMMAND} -E env
        QMAKE=${Qt6Core_DIR}/../../../bin/qmake6
        FLOWGRID_PROTOCOL_A=${GO_PROTOCOL_A}
        FLOWGRID_PROTOCOL_H=${GO_PROTOCOL_H}
        cargo build --release --manifest-path ${CMAKE_SOURCE_DIR}/rust/Cargo.toml
    COMMENT "Building Rust flowgrid-app"
    DEPENDS go_protocol_build
)

add_custom_target(flowgrid_rust ALL DEPENDS ${FLOWGRID_APP})

# Install
install(PROGRAMS ${FLOWGRID_APP} DESTINATION bin RENAME flowgrid)
install(FILES resources/org.flowgrid.desktop DESTINATION share/applications)
install(FILES resources/icons/flowgrid.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES resources/99-flowgrid.rules DESTINATION lib/udev/rules.d)
install(FILES resources/flowgrid.service DESTINATION lib/systemd/user)

# Install QML files
install(DIRECTORY rust/cxx-qt-bridge/qml/ DESTINATION share/flowgrid/qml)
