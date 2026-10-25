//go:build !linux

// OS 控制层的 C ABI 桥（非 Linux 平台的桩）。
//
// macOS / Windows 客户端目前**不走**这一层：它们各有自己的 HAL（mac 用
// CGEvent，Windows 用 SendInput），且都还没实现 oscontrol 的平台层。
//
// 但导出的符号必须在每个平台都存在——c-archive 是整包编译的，mac 端链接
// 时找不到符号会直接失败。所以这里给一份「诚实的桩」：调用它会得到明确
// 的「该平台没编进来」，而不是崩溃或静默不做事。
package main

/*
#include <stdint.h>
#include <stddef.h>
*/
import "C"

import (
	"unsafe"

	"github.com/flowgrid/protocol"
)

// exportNotSupported 是这一层在非 Linux 平台上的统一说法。
const exportNotSupported = "oscontrol: 该平台没有编入 OS 控制层"

//export fg_oscontrol_new
//
// fg_oscontrol_new 造一个 OS 控制器。非 Linux 平台一律返回 0（失败哨兵）。
//
//export fg_oscontrol_new
func fg_oscontrol_new(platform C.uint8_t) C.uintptr_t {
	return 0
}

//export fg_oscontrol_free
func fg_oscontrol_free(handle C.uintptr_t) {}

//export fg_oscontrol_set_peer
func fg_oscontrol_set_peer(handle C.uintptr_t, peerPlatform C.uint8_t,
	tableHandle unsafe.Pointer, context *C.char) C.int {
	return -1
}

//export fg_oscontrol_start_capture
func fg_oscontrol_start_capture(handle C.uintptr_t) C.int {
	return -1
}

//export fg_oscontrol_stop_capture
func fg_oscontrol_stop_capture(handle C.uintptr_t) {}

//export fg_oscontrol_next_frame
func fg_oscontrol_next_frame(handle C.uintptr_t, timeoutMs C.int,
	outType *C.uint8_t, outPayload **C.uint8_t, outLen *C.size_t) C.int {
	return -1
}

//export fg_oscontrol_release_captured
func fg_oscontrol_release_captured(handle C.uintptr_t) C.int {
	return -1
}

//export fg_oscontrol_start_injection
func fg_oscontrol_start_injection(handle C.uintptr_t) C.int {
	return -1
}

//export fg_oscontrol_stop_injection
func fg_oscontrol_stop_injection(handle C.uintptr_t) {}

//export fg_oscontrol_inject_frame
func fg_oscontrol_inject_frame(handle C.uintptr_t, frameType C.uint8_t,
	payload *C.uint8_t, length C.size_t) C.int {
	return -1
}

//export fg_oscontrol_release_injected
func fg_oscontrol_release_injected(handle C.uintptr_t) C.int {
	return -1
}

//export fg_oscontrol_last_error
func fg_oscontrol_last_error(handle C.uintptr_t, buf *C.char, bufLen C.size_t) C.int {
	message := exportNotSupported
	if buf == nil || bufLen == 0 {
		return C.int(len(message))
	}
	size := int(bufLen)
	raw := []byte(message)
	if len(raw) > size-1 {
		raw = raw[:size-1]
	}
	copy(unsafe.Slice((*byte)(unsafe.Pointer(buf)), size), raw)
	*(*byte)(unsafe.Pointer(uintptr(unsafe.Pointer(buf)) + uintptr(len(raw)))) = 0
	return C.int(len(message))
}

// 保持 protocol 包的导入（FrameKeyUp 等常量在 Linux 版里用，这里对齐一下
// 免得两份文件的导出签名走偏）。
var _ = protocol.FrameKeyUp
