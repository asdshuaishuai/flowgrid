// Package main exports FlowGrid protocol functions via cgo C ABI.
package main

/*
// C-side notes for whoever binds this header. cgo copies this block verbatim
// into cgo.h, the only FFI surface a Rust/C client sees -- Go doc comments do
// not travel with it, so anything a C caller must know has to live here.
//
// fg_wifidirect_request_encode's pskHash parameter is IGNORED. It is kept only
// so the existing C ABI does not change. Protocol v1.1 removed the PSK hash
// from the Wi-Fi Direct discovery payload (PROTOCOL.md 3.1.3): broadcasting it
// let an attacker run an offline dictionary attack on the PSK. Do not compute
// one, do not send one -- bytes 15-18 of the discovery payload are zero. PSK
// ownership is proved in the DTLS/TLS pairing handshake instead.
//
// fg_frame_validate returns 0 when the frame may go on the wire, otherwise a
// PROTOCOL.md 12 error code: 0x11 unknownFrameType (drop the frame) or 0x12
// invalidPayload (PayloadLen does not match the payload -- fix the encoder).
// It never returns a negative value: it takes no handle, so there is no
// "invalid handle" case to report. Error codes can be turned into text with
// fg_error_string.
#include <stdlib.h>
#include <stdint.h>
*/
import "C"

import (
	"crypto/ecdsa"
	"encoding/binary"
	"errors"
	"net"
	"sync"
	"time"
	"unsafe"

	"github.com/flowgrid/protocol"
)

// registry is a concurrency-safe handle table for Rust/Qt callers, which may
// invoke these exports from multiple OS threads concurrently.
type registry[T any] struct {
	mu    sync.Mutex
	next  uintptr
	items map[uintptr]T
}

func (r *registry[T]) put(v T) uintptr {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.items == nil {
		r.items = make(map[uintptr]T)
	}
	// Handles start at 1: C/Rust callers treat 0 as the null/error sentinel
	// (e.g. fg_keymap_load_file returns 0 on failure), so a valid handle
	// must never be 0.
	r.next++
	id := r.next
	r.items[id] = v
	return id
}

func (r *registry[T]) get(id uintptr) (T, bool) {
	r.mu.Lock()
	defer r.mu.Unlock()
	v, ok := r.items[id]
	return v, ok
}

func (r *registry[T]) remove(id uintptr) {
	r.mu.Lock()
	defer r.mu.Unlock()
	delete(r.items, id)
}

// goString copies a NUL-terminated C string, tolerating NULL (returns "").
func goString(s *C.char) string {
	if s == nil {
		return ""
	}
	return C.GoString(s)
}

// cBuffer allocates C memory for a returned buffer and copies src into it.
// Returns nil on allocation failure; callers must signal the error to the
// C/Rust side instead of building a slice over nil memory.
//
// SAFETY: buf comes from C.malloc and the unsafe.Slice view of it is confined
// to this call; the bytes copied in are plain data, never Go pointers.
// Ownership transfers to the C/Rust caller, who must release it exactly once
// via fg_free_buffer.
func cBuffer(src []byte) *C.uint8_t {
	if len(src) == 0 {
		return nil
	}
	buf := C.malloc(C.size_t(len(src)))
	if buf == nil {
		return nil
	}
	copy(unsafe.Slice((*byte)(buf), len(src)), src)
	return (*C.uint8_t)(buf)
}

var (
	latencyMonitors      registry[*protocol.LatencyMonitor]
	heartbeatTrackers    registry[*protocol.HeartbeatTracker]
	reconnectStates      registry[*protocol.ReconnectState]
	sequenceTrackers     registry[*protocol.SequenceTracker]
	fragmentReassemblers registry[*protocol.FragmentReassembler]
	keymapTables         registry[*protocol.KeyMapTable]
	lanIDIdentities      registry[*ecdsa.PrivateKey]
	lanIDAnnouncers      registry[*protocol.LanIDAnnouncer]
	lanIDListeners       registry[*protocol.LanIDListener]
	trustChains          registry[*protocol.TrustChain]
)

// --- Frame ---

//export fg_frame_encode
func fg_frame_encode(frameType C.uint8_t, sequence C.uint16_t, timestamp C.uint64_t, payload *C.uint8_t, payloadLen C.uint8_t, outLen *C.size_t) *C.uint8_t {
	p := make([]byte, payloadLen)
	if payloadLen > 0 && payload != nil {
		src := unsafe.Slice((*byte)(unsafe.Pointer(payload)), int(payloadLen))
		copy(p, src)
	}
	f := &protocol.HIDFrame{
		Header: protocol.FrameHeader{
			FrameType:  uint8(frameType),
			Sequence:   uint16(sequence),
			Timestamp:  uint64(timestamp),
			PayloadLen: uint8(payloadLen),
		},
		Payload: p,
	}
	data, err := f.EncodeBytes()
	if err != nil {
		*outLen = 0
		return nil
	}
	out := cBuffer(data)
	if out == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(data))
	return out
}

//export fg_frame_decode
func fg_frame_decode(data *C.uint8_t, len C.size_t, outType *C.uint8_t, outSeq *C.uint16_t, outTs *C.uint64_t, outPayloadLen *C.uint8_t, outPayload **C.uint8_t) C.int {
	if data == nil || len == 0 {
		return -1
	}
	src := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(len))
	f, consumed, err := protocol.DecodeFrameBytes(src)
	if err != nil {
		return -1
	}
	_ = consumed
	*outType = C.uint8_t(f.Header.FrameType)
	*outSeq = C.uint16_t(f.Header.Sequence)
	*outTs = C.uint64_t(f.Header.Timestamp)
	*outPayloadLen = C.uint8_t(f.Header.PayloadLen)
	if f.Header.PayloadLen > 0 {
		buf := cBuffer(f.Payload)
		if buf == nil {
			return -1
		}
		*outPayload = buf
	} else {
		*outPayload = nil
	}
	return 0
}

//export fg_frame_encode_with_hmac
func fg_frame_encode_with_hmac(frameType C.uint8_t, sequence C.uint16_t, timestamp C.uint64_t, payload *C.uint8_t, payloadLen C.uint8_t, hmacKey *C.uint8_t, hmacKeyLen C.size_t, outLen *C.size_t) *C.uint8_t {
	p := make([]byte, payloadLen)
	if payloadLen > 0 && payload != nil {
		src := unsafe.Slice((*byte)(unsafe.Pointer(payload)), int(payloadLen))
		copy(p, src)
	}
	var key []byte
	if hmacKeyLen > 0 && hmacKey != nil {
		key = unsafe.Slice((*byte)(unsafe.Pointer(hmacKey)), int(hmacKeyLen))
	}
	f := &protocol.HIDFrame{
		Header: protocol.FrameHeader{
			FrameType:  uint8(frameType),
			Sequence:   uint16(sequence),
			Timestamp:  uint64(timestamp),
			PayloadLen: uint8(payloadLen),
		},
		Payload: p,
	}
	data, err := f.EncodeBytesWithHMAC(key)
	if err != nil {
		*outLen = 0
		return nil
	}
	out := cBuffer(data)
	if out == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(data))
	return out
}

//export fg_frame_decode_with_hmac
func fg_frame_decode_with_hmac(data *C.uint8_t, len C.size_t, hmacKey *C.uint8_t, hmacKeyLen C.size_t, outType *C.uint8_t, outSeq *C.uint16_t, outTs *C.uint64_t, outPayloadLen *C.uint8_t, outPayload **C.uint8_t) C.int {
	if data == nil || len == 0 {
		return -1
	}
	src := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(len))
	var key []byte
	if hmacKeyLen > 0 && hmacKey != nil {
		key = unsafe.Slice((*byte)(unsafe.Pointer(hmacKey)), int(hmacKeyLen))
	}
	f, consumed, err := protocol.DecodeFrameBytesWithHMAC(src, key)
	if err != nil {
		return -1
	}
	_ = consumed
	*outType = C.uint8_t(f.Header.FrameType)
	*outSeq = C.uint16_t(f.Header.Sequence)
	*outTs = C.uint64_t(f.Header.Timestamp)
	*outPayloadLen = C.uint8_t(f.Header.PayloadLen)
	if f.Header.PayloadLen > 0 {
		buf := cBuffer(f.Payload)
		if buf == nil {
			return -1
		}
		*outPayload = buf
	} else {
		*outPayload = nil
	}
	return 0
}

//export fg_frame_validate
func fg_frame_validate(frameType C.uint8_t, sequence C.uint16_t, timestamp C.uint64_t, payloadLen C.uint8_t, payload *C.uint8_t) C.int {
	p := make([]byte, payloadLen)
	if payloadLen > 0 && payload != nil {
		src := unsafe.Slice((*byte)(unsafe.Pointer(payload)), int(payloadLen))
		copy(p, src)
	}
	f := &protocol.HIDFrame{
		Header: protocol.FrameHeader{
			FrameType:  uint8(frameType),
			Sequence:   uint16(sequence),
			Timestamp:  uint64(timestamp),
			PayloadLen: uint8(payloadLen),
		},
		Payload: p,
	}
	if err := f.Validate(); err != nil {
		// 返回 §12 错误码（0x11 类型不认识 / 0x12 长度不符）而不是笼统的 -1：
		// 前者整条丢帧即可，后者说明发送侧编码有 bug，要修。
		return C.int(errCode(err))
	}
	return 0
}

// --- Payload builders ---

//export fg_payload_keydown
func fg_payload_keydown(keyCode C.uint16_t, modifiers C.uint8_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.KeyDownPayload{KeyCode: uint16(keyCode), Modifiers: uint8(modifiers)}).Marshal()
	buf := cBuffer(p)
	if buf == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(p))
	return buf
}

//export fg_payload_keyup
func fg_payload_keyup(keyCode C.uint16_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.KeyUpPayload{KeyCode: uint16(keyCode)}).Marshal()
	buf := cBuffer(p)
	if buf == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(p))
	return buf
}

//export fg_payload_mousemove
func fg_payload_mousemove(deltaX C.int16_t, deltaY C.int16_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.MouseMovePayload{DeltaX: int16(deltaX), DeltaY: int16(deltaY)}).Marshal()
	buf := cBuffer(p)
	if buf == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(p))
	return buf
}

//export fg_payload_mousebtn
func fg_payload_mousebtn(buttonID C.uint8_t, state C.uint8_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.MouseButtonPayload{ButtonID: uint8(buttonID), State: uint8(state)}).Marshal()
	buf := cBuffer(p)
	if buf == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(p))
	return buf
}

//export fg_payload_scroll
func fg_payload_scroll(deltaY C.int16_t, deltaX C.int16_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.ScrollPayload{DeltaY: int16(deltaY), DeltaX: int16(deltaX)}).Marshal()
	buf := cBuffer(p)
	if buf == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(p))
	return buf
}

//export fg_payload_latency_ping
func fg_payload_latency_ping(timestamp C.uint64_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.LatencyPingPayload{Timestamp: uint64(timestamp)}).Marshal()
	buf := cBuffer(p)
	if buf == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(p))
	return buf
}

//export fg_payload_latency_pong
func fg_payload_latency_pong(timestamp C.uint64_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.LatencyPongPayload{Timestamp: uint64(timestamp)}).Marshal()
	buf := cBuffer(p)
	if buf == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(p))
	return buf
}

//export fg_payload_disconnect
func fg_payload_disconnect(reason C.uint8_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.DisconnectPayload{Reason: uint8(reason)}).Marshal()
	buf := cBuffer(p)
	if buf == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(p))
	return buf
}

//export fg_payload_error
func fg_payload_error(code C.uint8_t, msg *C.char, outLen *C.size_t) *C.uint8_t {
	message := goString(msg)
	p, err := (&protocol.ErrorPayload{Code: protocol.ErrorCode(code), Message: message}).Marshal()
	if err != nil {
		*outLen = 0
		return nil
	}
	buf := cBuffer(p)
	if buf == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(p))
	return buf
}

//export fg_payload_unmarshal_keydown
func fg_payload_unmarshal_keydown(data *C.uint8_t, len C.size_t, outKeyCode *C.uint16_t, outModifiers *C.uint8_t) C.int {
	if data == nil || len < 3 {
		return -1
	}
	src := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(len))
	p, err := protocol.UnmarshalKeyDown(src)
	if err != nil {
		return -1
	}
	*outKeyCode = C.uint16_t(p.KeyCode)
	*outModifiers = C.uint8_t(p.Modifiers)
	return 0
}

//export fg_payload_unmarshal_keyup
func fg_payload_unmarshal_keyup(data *C.uint8_t, len C.size_t, outKeyCode *C.uint16_t) C.int {
	if data == nil || len < 2 {
		return -1
	}
	src := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(len))
	p, err := protocol.UnmarshalKeyUp(src)
	if err != nil {
		return -1
	}
	*outKeyCode = C.uint16_t(p.KeyCode)
	return 0
}

//export fg_payload_unmarshal_mousemove
func fg_payload_unmarshal_mousemove(data *C.uint8_t, len C.size_t, outDX *C.int16_t, outDY *C.int16_t) C.int {
	if data == nil || len < 4 {
		return -1
	}
	src := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(len))
	p, err := protocol.UnmarshalMouseMove(src)
	if err != nil {
		return -1
	}
	*outDX = C.int16_t(p.DeltaX)
	*outDY = C.int16_t(p.DeltaY)
	return 0
}

//export fg_payload_unmarshal_mousebtn
func fg_payload_unmarshal_mousebtn(data *C.uint8_t, len C.size_t, outBtn *C.uint8_t, outState *C.uint8_t) C.int {
	if data == nil || len < 2 {
		return -1
	}
	src := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(len))
	p, err := protocol.UnmarshalMouseButton(src)
	if err != nil {
		return -1
	}
	*outBtn = C.uint8_t(p.ButtonID)
	*outState = C.uint8_t(p.State)
	return 0
}

//export fg_payload_unmarshal_scroll
func fg_payload_unmarshal_scroll(data *C.uint8_t, len C.size_t, outDY *C.int16_t, outDX *C.int16_t) C.int {
	if data == nil || len < 4 {
		return -1
	}
	src := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(len))
	p, err := protocol.UnmarshalScroll(src)
	if err != nil {
		return -1
	}
	*outDY = C.int16_t(p.DeltaY)
	*outDX = C.int16_t(p.DeltaX)
	return 0
}

//export fg_payload_unmarshal_disconnect
func fg_payload_unmarshal_disconnect(data *C.uint8_t, len C.size_t, outReason *C.uint8_t) C.int {
	if data == nil || len < 1 {
		return -1
	}
	src := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(len))
	p, err := protocol.UnmarshalDisconnect(src)
	if err != nil {
		return -1
	}
	*outReason = C.uint8_t(p.Reason)
	return 0
}

//export fg_payload_clipboard
func fg_payload_clipboard(mime *C.char, data *C.uint8_t, dataLen C.size_t, outLen *C.size_t) *C.uint8_t {
	p := &protocol.ClipboardPayload{
		MIMEType: goString(mime),
		FragTot:  1,
		Data:     make([]byte, dataLen),
	}
	if dataLen > 0 && data != nil {
		copy(p.Data, unsafe.Slice((*byte)(unsafe.Pointer(data)), int(dataLen)))
	}
	encoded, err := p.Marshal()
	if err != nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(encoded))
	buf := C.malloc(C.size_t(len(encoded)))
	copy(unsafe.Slice((*byte)(buf), len(encoded)), encoded)
	return (*C.uint8_t)(buf)
}

//export fg_payload_unmarshal_clipboard
func fg_payload_unmarshal_clipboard(data *C.uint8_t, n C.size_t, outMimeLen *C.uint8_t, outMime **C.uint8_t, outDataLen *C.size_t, outData **C.uint8_t) C.int {
	if data == nil || n < 5 {
		return -1
	}
	src := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(n))
	p, err := protocol.UnmarshalClipboard(src)
	if err != nil {
		return -1
	}
	mimeBytes := []byte(p.MIMEType)
	mimeBuf := cBuffer(mimeBytes)
	if mimeBuf == nil && len(mimeBytes) > 0 {
		return -1
	}
	*outMimeLen = C.uint8_t(len(mimeBytes))
	*outMime = mimeBuf
	*outDataLen = C.size_t(len(p.Data))
	dataBuf := cBuffer(p.Data)
	if dataBuf == nil && len(p.Data) > 0 {
		return -1
	}
	*outData = dataBuf
	return 0
}

// --- Keymap ---

//export fg_keymap_load_file
func fg_keymap_load_file(path *C.char, outVersion **C.char, outRuleCount *C.size_t) uintptr {
	p := goString(path)
	kmf, err := protocol.LoadKeyMapFile(p)
	if err != nil {
		*outVersion = nil
		*outRuleCount = 0
		return 0
	}
	*outVersion = C.CString(kmf.Version)
	*outRuleCount = C.size_t(len(kmf.Rules))
	return keymapTables.put(protocol.NewKeyMapTable(kmf))
}

//export fg_keymap_lookup
func fg_keymap_lookup(handle unsafe.Pointer, fromPlatform C.uint8_t, toPlatform C.uint8_t, keyCode C.uint16_t, modifiers C.uint8_t, context *C.char, outKeyCode *C.uint16_t, outModifiers *C.uint8_t, outFound *C.int) C.int {
	table, ok := keymapTables.get(uintptr(handle))
	if !ok {
		return -1
	}
	ctx := protocol.ContextGlobal
	if s := goString(context); s != "" {
		ctx = protocol.KeyMapContext(s)
	}
	key, mods, found := table.Lookup(
		protocol.Platform(fromPlatform),
		protocol.Platform(toPlatform),
		uint16(keyCode),
		uint8(modifiers),
		ctx,
	)
	*outKeyCode = C.uint16_t(key)
	*outModifiers = C.uint8_t(mods)
	if found {
		*outFound = 1
	} else {
		*outFound = 0
	}
	return 0
}

//export fg_remap_modifiers
func fg_remap_modifiers(mods C.uint8_t, fromPlatform C.uint8_t, toPlatform C.uint8_t) C.uint8_t {
	result := protocol.RemapModifiers(uint8(mods), protocol.Platform(fromPlatform), protocol.Platform(toPlatform))
	return C.uint8_t(result)
}

//export fg_keymap_free
func fg_keymap_free(handle unsafe.Pointer) {
	keymapTables.remove(uintptr(handle))
}

//export fg_keymap_free_string
func fg_keymap_free_string(s *C.char) {
	if s != nil {
		C.free(unsafe.Pointer(s))
	}
}

//export fg_free_buffer
func fg_free_buffer(buf unsafe.Pointer) {
	if buf != nil {
		C.free(buf)
	}
}

// --- Discovery ---

//export fg_ble_advertising_encode
func fg_ble_advertising_encode(protocolVer C.uint8_t, platform C.uint8_t, caps C.uint8_t, wifiChan C.uint16_t, outLen *C.size_t) *C.uint8_t {
	d := &protocol.BLEAdvertisingData{
		ProtocolVersion: uint8(protocolVer),
		DevicePlatform:  protocol.Platform(platform),
		CapabilityFlags: protocol.CapabilityFlags(caps),
		WiFiDirectChan:  uint16(wifiChan),
	}
	data := d.MarshalBLEAdvertising()
	out := cBuffer(data)
	if out == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(data))
	return out
}

//export fg_ble_advertising_decode
func fg_ble_advertising_decode(data *C.uint8_t, len C.size_t, outVer *C.uint8_t, outPlatform *C.uint8_t, outCaps *C.uint8_t, outChan *C.uint16_t) C.int {
	if data == nil || len < 7 {
		return -1
	}
	src := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(len))
	d, err := protocol.UnmarshalBLEAdvertising(src)
	if err != nil {
		return -1
	}
	*outVer = C.uint8_t(d.ProtocolVersion)
	*outPlatform = C.uint8_t(d.DevicePlatform)
	*outCaps = C.uint8_t(d.CapabilityFlags)
	*outChan = C.uint16_t(d.WiFiDirectChan)
	return 0
}

//export fg_classify_rssi
func fg_classify_rssi(rssi C.int) C.int {
	q := protocol.ClassifyRSSI(int(rssi))
	return C.int(q)
}

// --- WiFi Direct ---

// pskHash is ignored: v1.1 removed the PSK hash from the wire format (§3.1.3 —
// broadcasting it enabled offline PSK guessing). The parameter is kept so the
// C ABI in cgo.h stays stable until clients are rebuilt.
//
//export fg_wifidirect_request_encode
func fg_wifidirect_request_encode(version C.uint8_t, port C.uint16_t, ipAddr *C.uint8_t, subnet *C.uint8_t, channel C.uint8_t, band C.uint8_t, security C.uint8_t, pskHash *C.uint8_t, outLen *C.size_t) *C.uint8_t {
	_ = pskHash
	r := &protocol.WiFiDirectRequest{
		Version:  uint8(version),
		Port:     uint16(port),
		Channel:  uint8(channel),
		Band:     uint8(band),
		Security: uint8(security),
	}
	if ipAddr != nil {
		copy(r.IPAddr[:], unsafe.Slice((*byte)(unsafe.Pointer(ipAddr)), 4))
	}
	if subnet != nil {
		copy(r.Subnet[:], unsafe.Slice((*byte)(unsafe.Pointer(subnet)), 4))
	}
	data := r.Marshal()
	out := cBuffer(data)
	if out == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(data))
	return out
}

//export fg_wifidirect_response_encode
func fg_wifidirect_response_encode(version C.uint8_t, status C.uint8_t, port C.uint16_t, channel C.uint16_t, band C.uint8_t, outLen *C.size_t) *C.uint8_t {
	r := &protocol.WiFiDirectResponse{
		Version: uint8(version),
		Status:  uint8(status),
		Port:    uint16(port),
		Channel: uint16(channel),
		Band:    uint8(band),
	}
	data := r.Marshal()
	out := cBuffer(data)
	if out == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(data))
	return out
}

// --- Latency ---

//export fg_latency_monitor_new
func fg_latency_monitor_new(maxSamples C.int) uintptr {
	m := protocol.NewLatencyMonitor(int(maxSamples))
	return latencyMonitors.put(m)
}

//export fg_latency_record_ping
func fg_latency_record_ping(id uintptr, sendTimeUs C.uint64_t) {
	if m, ok := latencyMonitors.get(id); ok {
		m.RecordPing(uint64(sendTimeUs))
	}
}

//export fg_latency_record_pong
func fg_latency_record_pong(id uintptr, pongTimestampUs C.uint64_t, nowUs C.uint64_t, outMs *C.double, outJitter *C.double) C.int {
	m, ok := latencyMonitors.get(id)
	if !ok {
		return -1
	}
	sample, ok := m.RecordPong(uint64(pongTimestampUs), uint64(nowUs))
	if !ok {
		return -1 // rejected: future/stale/implausible pong timestamp
	}
	*outMs = C.double(sample.ValueMs)
	*outJitter = C.double(sample.Jitter)
	return 0
}

//export fg_latency_latest
func fg_latency_latest(id uintptr, outMs *C.double, outJitter *C.double, outQuality *C.int) C.int {
	m, ok := latencyMonitors.get(id)
	if !ok {
		return -1
	}
	s := m.Latest()
	if s == nil {
		return -1
	}
	*outMs = C.double(s.ValueMs)
	*outJitter = C.double(s.Jitter)
	*outQuality = C.int(protocol.ClassifyLatency(s.ValueMs))
	return 0
}

//export fg_latency_consecutive_bad
func fg_latency_consecutive_bad(id uintptr) C.int {
	m, ok := latencyMonitors.get(id)
	if !ok {
		return -1
	}
	return C.int(m.ConsecutiveBad())
}

//export fg_latency_stats
func fg_latency_stats(id uintptr, outMin *C.double, outMax *C.double, outAvg *C.double) C.int {
	m, ok := latencyMonitors.get(id)
	if !ok {
		return -1
	}
	min, max, avg := m.Stats()
	*outMin = C.double(min)
	*outMax = C.double(max)
	*outAvg = C.double(avg)
	return 0
}

//export fg_latency_monitor_free
func fg_latency_monitor_free(id uintptr) {
	latencyMonitors.remove(id)
}

// --- Heartbeat ---

//export fg_heartbeat_tracker_new
func fg_heartbeat_tracker_new() uintptr {
	return heartbeatTrackers.put(protocol.NewHeartbeatTracker(nil))
}

//export fg_heartbeat_received
func fg_heartbeat_received(id uintptr) {
	if t, ok := heartbeatTrackers.get(id); ok {
		t.ReceivedHeartbeat()
	}
}

//export fg_heartbeat_tick
func fg_heartbeat_tick(id uintptr) C.int {
	t, ok := heartbeatTrackers.get(id)
	if !ok {
		return -1
	}
	if t.Tick() {
		return 1 // connection lost
	}
	return 0
}

//export fg_heartbeat_tracker_free
func fg_heartbeat_tracker_free(id uintptr) {
	heartbeatTrackers.remove(id)
}

// --- Reconnect ---

//export fg_reconnect_state_new
func fg_reconnect_state_new() uintptr {
	return reconnectStates.put(protocol.NewReconnectState(protocol.ReconnectCallback{}))
}

//export fg_reconnect_start
func fg_reconnect_start(id uintptr, outBackoffMs *C.uint64_t, outOK *C.int) {
	s, ok := reconnectStates.get(id)
	if !ok {
		*outBackoffMs = 0
		*outOK = 0
		return
	}
	backoff, ok := s.StartReconnect()
	*outBackoffMs = C.uint64_t(backoff.Milliseconds())
	if ok {
		*outOK = 1
	} else {
		*outOK = 0
	}
}

//export fg_reconnect_set_connected
func fg_reconnect_set_connected(id uintptr) {
	if s, ok := reconnectStates.get(id); ok {
		s.SetConnected()
	}
}

//export fg_reconnect_reset
func fg_reconnect_reset(id uintptr) {
	if s, ok := reconnectStates.get(id); ok {
		s.Reset()
	}
}

//export fg_reconnect_state_free
func fg_reconnect_state_free(id uintptr) {
	reconnectStates.remove(id)
}

// --- Sequence ---

//export fg_sequence_tracker_new
func fg_sequence_tracker_new(seqInit C.uint32_t) uintptr {
	return sequenceTrackers.put(protocol.NewSequenceTracker(uint32(seqInit)))
}

//export fg_sequence_next
func fg_sequence_next(id uintptr) C.uint16_t {
	if t, ok := sequenceTrackers.get(id); ok {
		return C.uint16_t(t.Next())
	}
	return 0
}

//export fg_sequence_check
func fg_sequence_check(id uintptr, received C.uint16_t) C.int {
	t, ok := sequenceTrackers.get(id)
	if !ok {
		return -1
	}
	if err := t.Check(uint16(received)); err != nil {
		return 1 // gap detected
	}
	return 0
}

//export fg_sequence_reset
func fg_sequence_reset(id uintptr, seqInit C.uint32_t) {
	if t, ok := sequenceTrackers.get(id); ok {
		t.Reset(uint32(seqInit))
	}
}

//export fg_sequence_tracker_free
func fg_sequence_tracker_free(id uintptr) {
	sequenceTrackers.remove(id)
}

// --- Fragment ---

//export fg_fragment_reassembler_new
func fg_fragment_reassembler_new() uintptr {
	return fragmentReassemblers.put(protocol.NewFragmentReassembler())
}

//export fg_fragment_feed
func fg_fragment_feed(id uintptr, data *C.uint8_t, dataLen C.size_t, outLen *C.size_t, outData **C.uint8_t) C.int {
	r, ok := fragmentReassemblers.get(id)
	if !ok {
		return -1
	}
	if data == nil || dataLen == 0 {
		return -1
	}
	src := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(dataLen))
	assembled, err := r.Feed(src)
	if err != nil {
		return -1
	}
	if assembled == nil {
		*outLen = 0
		*outData = nil
		return 0 // more fragments needed
	}
	buf := cBuffer(assembled)
	if buf == nil {
		return -1
	}
	*outLen = C.size_t(len(assembled))
	*outData = buf
	return 1 // assembled
}

//export fg_fragment_cleanup
func fg_fragment_cleanup(id uintptr) {
	if r, ok := fragmentReassemblers.get(id); ok {
		r.Cleanup()
	}
}

//export fg_fragment_reassembler_free
func fg_fragment_reassembler_free(id uintptr) {
	fragmentReassemblers.remove(id)
}

// --- Errors ---

//export fg_error_string
func fg_error_string(code C.uint8_t) *C.char {
	return C.CString(protocol.ErrorCodeString(protocol.ErrorCode(code)))
}

// --- Identify ---

//export fg_identify_payload_encode
func fg_identify_payload_encode(version C.uint8_t, platform C.uint8_t, role C.uint8_t, caps C.uint8_t, seqInit C.uint32_t, name *C.char, outLen *C.size_t) *C.uint8_t {
	p := &protocol.IdentifyPayload{
		Version:  uint8(version),
		Platform: protocol.Platform(platform),
		Role:     protocol.Role(role),
		Caps:     protocol.CapabilityFlags(caps),
		SeqInit:  uint32(seqInit),
		Name:     goString(name),
	}
	data := p.Marshal()
	out := cBuffer(data)
	if out == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(data))
	return out
}

//export fg_identify_response_encode
func fg_identify_response_encode(status C.uint8_t, platform C.uint8_t, caps C.uint8_t, name *C.char, outLen *C.size_t) *C.uint8_t {
	p := &protocol.IdentifyResponsePayload{
		Status:   uint8(status),
		Platform: protocol.Platform(platform),
		Caps:     protocol.CapabilityFlags(caps),
		Name:     goString(name),
	}
	data := p.Marshal()
	out := cBuffer(data)
	if out == nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(data))
	return out
}

// --- 会话层：ClipboardSession / FileSender / FileReceiver (v1.1 §5.3 / §7.4) ---
//
// 会话模块的回调是 Go 函数，无法直接跨 C ABI 传递。这里一律改成「Go 侧缓存
// 结果、C 侧轮询读取」的形态：投递方调 HandleFrame/Handle(pump)，读取方在
// 自己的定时器里取。这既省掉了函数指针编组这层脆弱机制，也和客户端本来就要
// 跑的采样定时器对齐。

// clipInboxMax 是暂存上限。客户端「同步历史」最多留 200 条（设置里的档位是
// 50/100/200），再多它也展示不出来；到顶时丢最旧的一条，保住「最近」。单条
// 上限 63,240 B，最坏是十几 MB 的短时驻留，而 C 侧一开面板就会把队列取空。
const clipInboxMax = 200

// clipSession 包装 ClipboardSession，并把重组完成的剪贴板按到达顺序排队，
// 供 fg_clipboard_take_pending 逐条取走——客户端那边是一个 while 循环。
//
// 早先这里只有一个槽位（新的直接盖掉旧的），于是面板没开着的时候，除最后一条
// 以外全部的下行剪贴板都无声消失了，「同步历史」等于只留一条。
type clipSession struct {
	sess *protocol.ClipboardSession

	mu      sync.Mutex
	pending []protocol.Clipboard
}

// popPending 取走最早的一条；调用方必须已持锁。取出后要清掉尾槽——只把切片
// 前移的话，被取走的那个载荷还挂在下层数组里，内存收不回来。
func (cs *clipSession) popPending() (protocol.Clipboard, bool) {
	if len(cs.pending) == 0 {
		return protocol.Clipboard{}, false
	}
	c := cs.pending[0]
	copy(cs.pending, cs.pending[1:])
	cs.pending[len(cs.pending)-1] = protocol.Clipboard{}
	cs.pending = cs.pending[:len(cs.pending)-1]
	return c, true
}

// send seam：会话层把「帧类型 + 载荷」交给 emitFrame，这里入队；C 侧用
// fg_session_poll_out 取走，自行打序列号/时间戳、按需加 HMAC 后写传输。
//
// 为什么是轮询而不是回调：Go 回调无法直接跨 C ABI（函数指针编组脆弱），
// 本文件其余各处也一律「Go 缓存、C 轮询」。队列是全局的——v1.1 只做单连接
// 演示；多连接版本需把队列随句柄存（见 docs/PROTOCOL-INTEGRATION.md §8）。
//
// 帧在入队前拷贝：会话层可能复用底层缓冲，只存切片会拿到被改写的字节。
const sessionOutMax = 4096

type sessionFrame struct {
	frameType uint8
	payload   []byte
}

var (
	sessionOutOnMu sync.RWMutex
	sessionOutOn   bool // 客户端是否已声明「传输就绪、会来取帧」
	sessionOut     = make(chan sessionFrame, sessionOutMax)
)

// errCode 把任意 error 归一成协议错误码：*ProtocolError 直接取 Code，
// 其它 error 一律归到 ErrSendFailed。C 侧只认数字错误码，不认 Go 的 error 树。
func errCode(err error) int {
	if err == nil {
		return 0
	}
	var pe *protocol.ProtocolError
	if errors.As(err, &pe) {
		return int(pe.Code)
	}
	return int(protocol.ErrSendFailed)
}

// fg_session_set_sink(userdata≠0) 声明传输就绪：此后 emitFrame 才会入队。
// 未声明时 emitFrame 返回 ErrNotConnected，于是 SendClipboard/Pump 直接报错，
// 而不是把帧悄悄丢掉又报成功。userdata=0 撤下并清空队列。
//
//export fg_session_set_sink
func fg_session_set_sink(userdata uintptr) {
	sessionOutOnMu.Lock()
	sessionOutOn = userdata != 0
	sessionOutOnMu.Unlock()
	if userdata == 0 {
		for len(sessionOut) > 0 {
			<-sessionOut
		}
	}
}

func emitFrame(frameType uint8, payload []byte) error {
	sessionOutOnMu.RLock()
	on := sessionOutOn
	sessionOutOnMu.RUnlock()
	if !on {
		return protocol.NewError(protocol.ErrNotConnected, "no session sink registered")
	}
	cp := make([]byte, len(payload))
	copy(cp, payload)
	select {
	case sessionOut <- sessionFrame{frameType: frameType, payload: cp}:
		return nil
	default:
		return protocol.NewError(protocol.ErrSendFailed, "session outbox full: client not draining")
	}
}

// pollSessionOut 取一帧，ok=false 表示队列空。抽成独立函数是为了能脱离 cgo
// 指针编组测试出队语义。
func pollSessionOut() (frameType uint8, payload []byte, ok bool) {
	select {
	case f := <-sessionOut:
		return f.frameType, f.payload, true
	default:
		return 0, nil, false
	}
}

// 取一帧待发：有则返回 1，payload 交调用方用 fg_free_buffer 释放；队列空返回 0。
//
//export fg_session_poll_out
func fg_session_poll_out(outType *C.uint8_t, outPayload **C.uint8_t, outLen *C.size_t) C.int {
	ft, payload, ok := pollSessionOut()
	if !ok {
		return 0
	}
	*outType = C.uint8_t(ft)
	*outLen = C.size_t(len(payload))
	*outPayload = cBuffer(payload)
	return 1
}

//export fg_clipboard_session_new
func fg_clipboard_session_new() uintptr {
	cs := &clipSession{sess: protocol.NewClipboardSession(emitFrame)}
	cs.sess.OnClipboard = func(c protocol.Clipboard) {
		cs.mu.Lock()
		if len(cs.pending) >= clipInboxMax {
			cs.popPending() // 队列满：先腾掉最旧的一条
		}
		cs.pending = append(cs.pending, c)
		cs.mu.Unlock()
	}
	return clipSessions.put(cs)
}

//export fg_clipboard_session_free
func fg_clipboard_session_free(handle uintptr) {
	clipSessions.remove(handle)
}

// passes<1 视作 1；返回 0 表示成功，否则是 protocol 错误码。
//
//export fg_clipboard_send
func fg_clipboard_send(handle uintptr, mime *C.char, data *C.uint8_t, dataLen C.size_t, passes C.int) C.int {
	cs, ok := clipSessions.get(handle)
	if !ok {
		return C.int(int(protocol.ErrInvalidPayload))
	}
	var buf []byte
	if dataLen > 0 && data != nil {
		buf = unsafe.Slice((*byte)(unsafe.Pointer(data)), int(dataLen))
	}
	if err := cs.sess.SendClipboardPasses(goString(mime), buf, int(passes)); err != nil {
		return C.int(errCode(err))
	}
	return 0
}

//export fg_clipboard_handle_frame
func fg_clipboard_handle_frame(handle uintptr, payload *C.uint8_t, n C.size_t) C.int {
	cs, ok := clipSessions.get(handle)
	if !ok {
		return C.int(int(protocol.ErrInvalidPayload))
	}
	if n > 0 && payload != nil {
		if _, err := cs.sess.HandleFrame(unsafe.Slice((*byte)(unsafe.Pointer(payload)), int(n))); err != nil {
			return C.int(errCode(err))
		}
	}
	return 0
}

// 返回 1 表示取到一份重组完成的剪贴板，MIME 与数据交给调用方用 fg_free_buffer 释放。
//
//export fg_clipboard_take_pending
func fg_clipboard_take_pending(handle uintptr, outMime **C.uint8_t, outMimeLen *C.size_t, outData **C.uint8_t, outDataLen *C.size_t) C.int {
	c, ok := takePendingClipboard(handle)
	if !ok {
		return 0
	}
	mime := []byte(c.MIMEType)
	// 长度必须跟着缓冲一起交出去：只把 out*Len 留 0 的话，调用方拿到的指针
	// 长度为零，每一份入站剪贴板都会被读成「空 MIME + 空数据」（C++ 侧 GoBuffer
	// 就按这个长度切片），下面 fg_file_receiver_* 几个同款出参都是照 len 填的。
	*outMimeLen = C.size_t(len(mime))
	*outDataLen = C.size_t(len(c.Data))
	*outMime = cBuffer(mime)
	*outData = cBuffer(c.Data)
	return 1
}

// takePendingClipboard 取走队首那条待取剪贴板，ok=false 表示队列已空。
//
// 抽成不碰 C 指针的普通函数（同 pollSessionOut 的理由）：cgo 包里的 _test.go
// 不能再 import "C"，出参编组这一层在 Go 侧测不到，而「队列空 / 到达顺序 /
// 满时丢最旧」这三条语义全在这里，得测得住。
func takePendingClipboard(handle uintptr) (protocol.Clipboard, bool) {
	cs, ok := clipSessions.get(handle)
	if !ok {
		return protocol.Clipboard{}, false
	}
	cs.mu.Lock()
	c, ok := cs.popPending()
	cs.mu.Unlock()
	return c, ok
}

var (
	clipSessions  registry[*clipSession]
	fileSenders   registry[*fileSender]
	fileReceivers registry[*fileReceiver]
)

// fg_file_sender_poll 的 state 出参取值，与 C++ FileSender::State 同序。
const (
	fileSenderIdle   = 0
	fileSenderActive = 1
	fileSenderDone   = 2
	fileSenderFailed = 3
)

// fileSender 包装 FileSender 并把回调结果缓存下来供轮询。
type fileSender struct {
	sess *protocol.FileSender

	mu      sync.Mutex
	sent    int
	total   int
	state   int
	errCode int
}

// snapshot 取一次状态给 C 侧，并消费掉已经报出去的终态与进度。
//
// 出参编组那层要 *C.int、测试里不能 import "C"，所以状态消费的逻辑放这儿，
// 只留一层薄薄的赋值在导出函数里。
func (fs *fileSender) snapshot() (state, sent, total, code int) {
	fs.mu.Lock()
	defer fs.mu.Unlock()
	state, sent, total, code = fs.state, fs.sent, fs.total, fs.errCode
	if fs.state == fileSenderDone || fs.state == fileSenderFailed {
		// 终态只报一次，报完连进度一起清掉：留着的话，下一路传输在发出第一帧
		// 之前，客户端拉到的就是上一路的最终进度（100%），而它只能按「第一条
		// 进行中的条目」去匹配，分不出这是旧数字。
		fs.state, fs.errCode = fileSenderIdle, 0
		fs.sent, fs.total = 0, 0
	}
	return state, sent, total, code
}

//export fg_file_sender_new
func fg_file_sender_new() uintptr {
	fs := &fileSender{sess: protocol.NewFileSender(emitFrame)}
	fs.sess.OnProgress = func(_ uint16, sent, total int) {
		fs.mu.Lock()
		fs.sent, fs.total, fs.state = sent, total, fileSenderActive
		fs.mu.Unlock()
	}
	fs.sess.OnDone = func(_ uint16, err error) {
		fs.mu.Lock()
		fs.state = fileSenderDone
		fs.errCode = 0
		if err != nil {
			fs.state = fileSenderFailed
			fs.errCode = errCode(err)
		}
		fs.mu.Unlock()
	}
	return fileSenders.put(fs)
}

//export fg_file_sender_free
func fg_file_sender_free(handle uintptr) { fileSenders.remove(handle) }

//export fg_file_sender_offer
func fg_file_sender_offer(handle uintptr, name *C.char, data *C.uint8_t, dataLen C.size_t, outID *C.uint16_t) C.int {
	fs, ok := fileSenders.get(handle)
	if !ok {
		return C.int(int(protocol.ErrInvalidPayload))
	}
	var buf []byte
	if dataLen > 0 && data != nil {
		buf = unsafe.Slice((*byte)(unsafe.Pointer(data)), int(dataLen))
	}
	id, err := fs.sess.OfferFile(goString(name), buf)
	if err != nil {
		return C.int(errCode(err))
	}
	*outID = C.uint16_t(id)
	return 0
}

// 返回实际发出的分块数；outPumped=0 且返回码非 0 表示出错。
//
//export fg_file_sender_pump
func fg_file_sender_pump(handle uintptr, maxChunks C.int, outPumped *C.int) C.int {
	fs, ok := fileSenders.get(handle)
	if !ok {
		return C.int(int(protocol.ErrInvalidPayload))
	}
	n, err := fs.sess.Pump(int(maxChunks))
	*outPumped = C.int(n)
	if err != nil {
		return C.int(errCode(err))
	}
	return 0
}

//export fg_file_sender_handle_frame
func fg_file_sender_handle_frame(handle uintptr, frameType C.uint8_t, payload *C.uint8_t, n C.size_t) C.int {
	fs, ok := fileSenders.get(handle)
	if !ok {
		return C.int(int(protocol.ErrInvalidPayload))
	}
	if n > 0 && payload != nil {
		if err := fs.sess.HandleFrame(uint8(frameType), unsafe.Slice((*byte)(unsafe.Pointer(payload)), int(n))); err != nil {
			return C.int(errCode(err))
		}
	}
	return 0
}

// state: 0 空闲 / 1 传输中 / 2 完成 / 3 失败。读取本身会清掉 2、3 两个终态，
// 让 UI 只弹一次「完成 / 失败」。
//
//export fg_file_sender_poll
func fg_file_sender_poll(handle uintptr, outState *C.int, outSent *C.int, outTotal *C.int, outErr *C.int) C.int {
	fs, ok := fileSenders.get(handle)
	if !ok {
		return -1
	}
	state, sent, total, code := fs.snapshot()
	*outState = C.int(state)
	*outSent = C.int(sent)
	*outTotal = C.int(total)
	*outErr = C.int(code)
	return 0
}

// fileReceiver 包装 FileReceiver，并暂存「收到 offer」与「完成/失败」两类结果。
type fileReceiver struct {
	sess *protocol.FileReceiver

	mu       sync.Mutex
	offer    *protocol.FileOffer
	complete *protocol.FileOffer
	data     []byte
	errCode  int
	hasDone  bool
}

func (fr *fileReceiver) takeOffer() *protocol.FileOffer {
	fr.mu.Lock()
	defer fr.mu.Unlock()
	o := fr.offer
	fr.offer = nil
	return o
}

//export fg_file_receiver_new
func fg_file_receiver_new() uintptr {
	fr := &fileReceiver{sess: protocol.NewFileReceiver(emitFrame)}
	fr.sess.OnOffer = func(offer protocol.FileOffer) {
		cpy := offer
		fr.mu.Lock()
		fr.offer = &cpy
		fr.mu.Unlock()
	}
	fr.sess.OnComplete = func(offer protocol.FileOffer, data []byte, err error) {
		cpy := offer
		fr.mu.Lock()
		fr.complete = &cpy
		fr.data = data
		fr.hasDone = true
		fr.errCode = 0
		if err != nil {
			fr.errCode = errCode(err)
		}
		fr.mu.Unlock()
	}
	return fileReceivers.put(fr)
}

//export fg_file_receiver_free
func fg_file_receiver_free(handle uintptr) { fileReceivers.remove(handle) }

// outHandled=1 表示该帧属于文件传输协议，分发器可以据此决定不再往下传。
//
//export fg_file_receiver_handle_frame
func fg_file_receiver_handle_frame(handle uintptr, frameType C.uint8_t, payload *C.uint8_t, n C.size_t, outHandled *C.int) C.int {
	fr, ok := fileReceivers.get(handle)
	if !ok {
		return C.int(int(protocol.ErrInvalidPayload))
	}
	var buf []byte
	if n > 0 && payload != nil {
		buf = unsafe.Slice((*byte)(unsafe.Pointer(payload)), int(n))
	}
	handled, err := fr.sess.HandleFrame(uint8(frameType), buf)
	if handled {
		*outHandled = 1
	} else {
		*outHandled = 0
	}
	if err != nil {
		return C.int(errCode(err))
	}
	return 0
}

// 返回 1 表示有一个待用户确认的 offer；name 需用 fg_free_buffer 释放。
//
//export fg_file_receiver_take_offer
func fg_file_receiver_take_offer(handle uintptr, outID *C.uint16_t, outName **C.uint8_t, outNameLen *C.size_t, outSize *C.uint64_t) C.int {
	fr, ok := fileReceivers.get(handle)
	if !ok {
		return 0
	}
	o := fr.takeOffer()
	if o == nil {
		return 0
	}
	*outID = C.uint16_t(o.TransferID)
	*outSize = C.uint64_t(o.Size)
	nb := []byte(o.Name)
	*outNameLen = C.size_t(len(nb))
	*outName = cBuffer(nb)
	return 1
}

//export fg_file_receiver_accept
func fg_file_receiver_accept(handle uintptr, id C.uint16_t) C.int {
	fr, ok := fileReceivers.get(handle)
	if !ok {
		return C.int(int(protocol.ErrInvalidPayload))
	}
	if err := fr.sess.Accept(uint16(id)); err != nil {
		return C.int(errCode(err))
	}
	return 0
}

//export fg_file_receiver_reject
func fg_file_receiver_reject(handle uintptr, id C.uint16_t, status C.uint8_t) C.int {
	fr, ok := fileReceivers.get(handle)
	if !ok {
		return C.int(int(protocol.ErrInvalidPayload))
	}
	if err := fr.sess.Reject(uint16(id), protocol.FileAckStatus(status)); err != nil {
		return C.int(errCode(err))
	}
	return 0
}

// 返回 1 表示有一次终态结果；name 与 data 需用 fg_free_buffer 释放。
// outErr 非 0 表示校验失败或传输失败，data 为空。
//
//export fg_file_receiver_take_complete
func fg_file_receiver_take_complete(handle uintptr, outName **C.uint8_t, outNameLen *C.size_t, outData **C.uint8_t, outDataLen *C.size_t, outErr *C.int) C.int {
	fr, ok := fileReceivers.get(handle)
	if !ok {
		return 0
	}
	fr.mu.Lock()
	o := fr.complete
	data := fr.data
	code := fr.errCode
	fr.complete, fr.data, fr.hasDone, fr.errCode = nil, nil, false, 0
	fr.mu.Unlock()
	if o == nil {
		return 0
	}
	*outErr = C.int(code)
	nb := []byte(o.Name)
	*outNameLen = C.size_t(len(nb))
	*outName = cBuffer(nb)
	*outDataLen = C.size_t(len(data))
	*outData = cBuffer(data)
	return 1
}

// --- LanID 身份密钥（ECDSA P-256，持久） ---

//export fg_lanid_identity_generate
func fg_lanid_identity_generate() uintptr {
	priv, err := protocol.GenerateIdentityKeyPair()
	if err != nil {
		return 0
	}
	return lanIDIdentities.put(priv)
}

//export fg_lanid_identity_export
func fg_lanid_identity_export(id uintptr, outLen *C.size_t) *C.uint8_t {
	priv, ok := lanIDIdentities.get(id)
	if !ok {
		*outLen = 0
		return nil
	}
	data, err := protocol.ExportIdentityPrivateKey(priv)
	if err != nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(data))
	return cBuffer(data)
}

//export fg_lanid_identity_import
func fg_lanid_identity_import(data *C.uint8_t, length C.size_t) uintptr {
	if data == nil || length == 0 {
		return 0
	}
	src := make([]byte, int(length))
	copy(src, unsafe.Slice((*byte)(unsafe.Pointer(data)), int(length)))
	priv, err := protocol.ImportIdentityPrivateKey(src)
	if err != nil {
		return 0
	}
	return lanIDIdentities.put(priv)
}

//export fg_lanid_identity_public_key
func fg_lanid_identity_public_key(id uintptr, outLen *C.size_t) *C.uint8_t {
	priv, ok := lanIDIdentities.get(id)
	if !ok {
		*outLen = 0
		return nil
	}
	pub := protocol.IdentityPublicKeyBytes(priv)
	*outLen = C.size_t(len(pub))
	return cBuffer(pub)
}

//export fg_lanid_identity_free
func fg_lanid_identity_free(id uintptr) {
	lanIDIdentities.remove(id)
}

// --- LanID 通告器（纯逻辑：Announce 产生字节，客户端发送） ---

//export fg_lanid_announcer_new
func fg_lanid_announcer_new(identID uintptr, platform C.uint8_t, caps C.uint8_t, port C.uint16_t, name *C.char) uintptr {
	priv, ok := lanIDIdentities.get(identID)
	if !ok {
		return 0
	}
	ann, err := protocol.NewLanIDAnnouncer(priv, protocol.Platform(platform), protocol.CapabilityFlags(caps), uint16(port), goString(name))
	if err != nil {
		return 0
	}
	return lanIDAnnouncers.put(ann)
}

//export fg_lanid_announcer_announce
func fg_lanid_announcer_announce(id uintptr, outLen *C.size_t) *C.uint8_t {
	ann, ok := lanIDAnnouncers.get(id)
	if !ok {
		*outLen = 0
		return nil
	}
	wire, err := ann.Announce()
	if err != nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(wire))
	return cBuffer(wire)
}

//export fg_lanid_announcer_free
func fg_lanid_announcer_free(id uintptr) {
	lanIDAnnouncers.remove(id)
}

// --- LanID 监听器（纯逻辑：Feed 喂字节，Poll/Peers 取结果） ---

//export fg_lanid_listener_new
func fg_lanid_listener_new() uintptr {
	return lanIDListeners.put(protocol.NewLanIDListener())
}

//export fg_lanid_listener_feed
func fg_lanid_listener_feed(id uintptr, data *C.uint8_t, length C.size_t, fromIP *C.uint8_t, fromIPLen C.size_t) C.int {
	l, ok := lanIDListeners.get(id)
	if !ok || data == nil || length == 0 {
		return -1
	}
	src := make([]byte, int(length))
	copy(src, unsafe.Slice((*byte)(unsafe.Pointer(data)), int(length)))
	var ip net.IP
	if fromIP != nil && fromIPLen > 0 {
		ip = make(net.IP, int(fromIPLen))
		copy(ip, unsafe.Slice((*byte)(unsafe.Pointer(fromIP)), int(fromIPLen)))
	}
	l.Feed(src, ip)
	return 0
}

//export fg_lanid_listener_peers_count
func fg_lanid_listener_peers_count(id uintptr) C.int {
	l, ok := lanIDListeners.get(id)
	if !ok {
		return -1
	}
	return C.int(len(l.Peers()))
}

//export fg_lanid_listener_peer
func fg_lanid_listener_peer(id uintptr, index C.int, outBuf *C.uint8_t, outBufLen C.size_t) C.int {
	l, ok := lanIDListeners.get(id)
	if !ok {
		return -1
	}
	peers := l.Peers()
	i := int(index)
	if i < 0 || i >= len(peers) {
		return -1
	}
	flat := serializeLanIDPeer(peers[i])
	if outBuf == nil || C.size_t(len(flat)) > outBufLen {
		return C.int(len(flat)) // 缓冲区不足：返回所需长度
	}
	copy(unsafe.Slice((*byte)(unsafe.Pointer(outBuf)), len(flat)), flat)
	return C.int(len(flat))
}

//export fg_lanid_listener_prune
func fg_lanid_listener_prune(id uintptr, maxAgeMs C.int64_t) C.int {
	l, ok := lanIDListeners.get(id)
	if !ok {
		return -1
	}
	return C.int(l.Prune(time.Duration(maxAgeMs) * time.Millisecond))
}

//export fg_lanid_listener_free
func fg_lanid_listener_free(id uintptr) {
	lanIDListeners.remove(id)
}

// serializeLanIDPeer 序列化对端为扁平字节（客户端顺序解析）：
// NameLen(1) + Name + Platform(1) + Caps(1) + Port(2 BE) + AddrLen(1) + Addr + PubKey(65) + Fingerprint(32 hex)
func serializeLanIDPeer(p protocol.LanIDPeer) []byte {
	nameBytes := []byte(p.Name)
	var addr []byte
	if p.Addr != nil {
		addr = []byte(p.Addr.To16())
	}
	fpBytes := []byte(p.Fingerprint)
	buf := make([]byte, 0, 1+len(nameBytes)+2+2+1+len(addr)+protocol.PublicKeySize+len(fpBytes))
	buf = append(buf, byte(len(nameBytes)))
	buf = append(buf, nameBytes...)
	buf = append(buf, byte(p.Platform), byte(p.Caps))
	var port [2]byte
	binary.BigEndian.PutUint16(port[:], p.Port)
	buf = append(buf, port[:]...)
	buf = append(buf, byte(len(addr)))
	buf = append(buf, addr...)
	buf = append(buf, p.PubKey...)
	buf = append(buf, fpBytes...)
	return buf
}

// --- LanID 私有链信任 ---

//export fg_lanid_trust_new
func fg_lanid_trust_new(identID uintptr) uintptr {
	priv, ok := lanIDIdentities.get(identID)
	if !ok {
		return 0
	}
	return trustChains.put(protocol.NewTrustChain(priv))
}

//export fg_lanid_trust_append
func fg_lanid_trust_append(id uintptr, peerID *C.uint8_t, peerIDLen C.size_t, decision C.uint8_t) C.int {
	c, ok := trustChains.get(id)
	if !ok || peerID == nil || peerIDLen == 0 {
		return -1
	}
	src := make([]byte, int(peerIDLen))
	copy(src, unsafe.Slice((*byte)(unsafe.Pointer(peerID)), int(peerIDLen)))
	if err := c.Append(src, uint8(decision)); err != nil {
		return -1
	}
	return 0
}

//export fg_lanid_trust_is_trusted
func fg_lanid_trust_is_trusted(id uintptr, peerID *C.uint8_t, peerIDLen C.size_t) C.int {
	c, ok := trustChains.get(id)
	if !ok || peerID == nil || peerIDLen == 0 {
		return -1
	}
	src := make([]byte, int(peerIDLen))
	copy(src, unsafe.Slice((*byte)(unsafe.Pointer(peerID)), int(peerIDLen)))
	if c.IsTrusted(src) {
		return 1
	}
	return 0
}

//export fg_lanid_trust_verify
func fg_lanid_trust_verify(id uintptr) C.int {
	c, ok := trustChains.get(id)
	if !ok {
		return -1
	}
	if err := c.Verify(); err != nil {
		return -1
	}
	return 0
}

//export fg_lanid_trust_count
func fg_lanid_trust_count(id uintptr) C.int {
	c, ok := trustChains.get(id)
	if !ok {
		return -1
	}
	return C.int(len(c.Records()))
}

//export fg_lanid_trust_free
func fg_lanid_trust_free(id uintptr) {
	trustChains.remove(id)
}

// --- 链路选择 ---

//export fg_link_plan
func fg_link_plan(preferred C.uint8_t, mdnsPeers C.int, lanPeers C.int, hasManual C.int) C.int {
	plan := protocol.PlanLinks(protocol.PreferredLink(preferred), int(mdnsPeers), int(lanPeers), hasManual != 0)
	if plan.Exhausted {
		return -1
	}
	// 打包顺序：0=[intelligent,direct] 1=[direct,intelligent] 2=[intelligent] 3=[direct]
	switch {
	case len(plan.Order) == 2 && plan.Order[0] == protocol.LinkIntelligent:
		return 0
	case len(plan.Order) == 2 && plan.Order[0] == protocol.LinkDirect:
		return 1
	case len(plan.Order) == 1 && plan.Order[0] == protocol.LinkIntelligent:
		return 2
	case len(plan.Order) == 1 && plan.Order[0] == protocol.LinkDirect:
		return 3
	default:
		return -1
	}
}

// --- Main (required for c-archive) ---
func main() {}
