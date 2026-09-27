// Package main exports FlowGrid protocol functions via cgo C ABI.
package main

/*
#include <stdlib.h>
#include <stdint.h>
*/
import "C"

import (
	"sync"
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
	id := r.next
	r.next++
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

var (
	latencyMonitors     registry[*protocol.LatencyMonitor]
	heartbeatTrackers   registry[*protocol.HeartbeatTracker]
	reconnectStates     registry[*protocol.ReconnectState]
	sequenceTrackers    registry[*protocol.SequenceTracker]
	fragmentReassemblers registry[*protocol.FragmentReassembler]
	keymapTables        registry[*protocol.KeyMapTable]
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
	*outLen = C.size_t(len(data))
	buf := C.malloc(C.size_t(len(data)))
	copy(unsafe.Slice((*byte)(buf), len(data)), data)
	return (*C.uint8_t)(buf)
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
		buf := C.malloc(C.size_t(f.Header.PayloadLen))
		copy(unsafe.Slice((*byte)(buf), int(f.Header.PayloadLen)), f.Payload)
		*outPayload = (*C.uint8_t)(buf)
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
	*outLen = C.size_t(len(data))
	buf := C.malloc(C.size_t(len(data)))
	copy(unsafe.Slice((*byte)(buf), len(data)), data)
	return (*C.uint8_t)(buf)
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
		buf := C.malloc(C.size_t(f.Header.PayloadLen))
		copy(unsafe.Slice((*byte)(buf), int(f.Header.PayloadLen)), f.Payload)
		*outPayload = (*C.uint8_t)(buf)
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
		return -1
	}
	return 0
}

// --- Payload builders ---

//export fg_payload_keydown
func fg_payload_keydown(keyCode C.uint16_t, modifiers C.uint8_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.KeyDownPayload{KeyCode: uint16(keyCode), Modifiers: uint8(modifiers)}).Marshal()
	*outLen = C.size_t(len(p))
	buf := C.malloc(C.size_t(len(p)))
	copy(unsafe.Slice((*byte)(buf), len(p)), p)
	return (*C.uint8_t)(buf)
}

//export fg_payload_keyup
func fg_payload_keyup(keyCode C.uint16_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.KeyUpPayload{KeyCode: uint16(keyCode)}).Marshal()
	*outLen = C.size_t(len(p))
	buf := C.malloc(C.size_t(len(p)))
	copy(unsafe.Slice((*byte)(buf), len(p)), p)
	return (*C.uint8_t)(buf)
}

//export fg_payload_mousemove
func fg_payload_mousemove(deltaX C.int16_t, deltaY C.int16_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.MouseMovePayload{DeltaX: int16(deltaX), DeltaY: int16(deltaY)}).Marshal()
	*outLen = C.size_t(len(p))
	buf := C.malloc(C.size_t(len(p)))
	copy(unsafe.Slice((*byte)(buf), len(p)), p)
	return (*C.uint8_t)(buf)
}

//export fg_payload_mousebtn
func fg_payload_mousebtn(buttonID C.uint8_t, state C.uint8_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.MouseButtonPayload{ButtonID: uint8(buttonID), State: uint8(state)}).Marshal()
	*outLen = C.size_t(len(p))
	buf := C.malloc(C.size_t(len(p)))
	copy(unsafe.Slice((*byte)(buf), len(p)), p)
	return (*C.uint8_t)(buf)
}

//export fg_payload_scroll
func fg_payload_scroll(deltaY C.int16_t, deltaX C.int16_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.ScrollPayload{DeltaY: int16(deltaY), DeltaX: int16(deltaX)}).Marshal()
	*outLen = C.size_t(len(p))
	buf := C.malloc(C.size_t(len(p)))
	copy(unsafe.Slice((*byte)(buf), len(p)), p)
	return (*C.uint8_t)(buf)
}

//export fg_payload_latency_ping
func fg_payload_latency_ping(timestamp C.uint64_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.LatencyPingPayload{Timestamp: uint64(timestamp)}).Marshal()
	*outLen = C.size_t(len(p))
	buf := C.malloc(C.size_t(len(p)))
	copy(unsafe.Slice((*byte)(buf), len(p)), p)
	return (*C.uint8_t)(buf)
}

//export fg_payload_latency_pong
func fg_payload_latency_pong(timestamp C.uint64_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.LatencyPongPayload{Timestamp: uint64(timestamp)}).Marshal()
	*outLen = C.size_t(len(p))
	buf := C.malloc(C.size_t(len(p)))
	copy(unsafe.Slice((*byte)(buf), len(p)), p)
	return (*C.uint8_t)(buf)
}

//export fg_payload_disconnect
func fg_payload_disconnect(reason C.uint8_t, outLen *C.size_t) *C.uint8_t {
	p := (&protocol.DisconnectPayload{Reason: uint8(reason)}).Marshal()
	*outLen = C.size_t(len(p))
	buf := C.malloc(C.size_t(len(p)))
	copy(unsafe.Slice((*byte)(buf), len(p)), p)
	return (*C.uint8_t)(buf)
}

//export fg_payload_error
func fg_payload_error(code C.uint8_t, msg *C.char, outLen *C.size_t) *C.uint8_t {
	message := C.GoString(msg)
	p, err := (&protocol.ErrorPayload{Code: protocol.ErrorCode(code), Message: message}).Marshal()
	if err != nil {
		*outLen = 0
		return nil
	}
	*outLen = C.size_t(len(p))
	buf := C.malloc(C.size_t(len(p)))
	copy(unsafe.Slice((*byte)(buf), len(p)), p)
	return (*C.uint8_t)(buf)
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
		MIMEType: C.GoString(mime),
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
	*outMimeLen = C.uint8_t(len(mimeBytes))
	mimeBuf := C.malloc(C.size_t(len(mimeBytes)))
	copy(unsafe.Slice((*byte)(mimeBuf), len(mimeBytes)), mimeBytes)
	*outMime = (*C.uint8_t)(mimeBuf)
	*outDataLen = C.size_t(len(p.Data))
	if len(p.Data) > 0 {
		dataBuf := C.malloc(C.size_t(len(p.Data)))
		copy(unsafe.Slice((*byte)(dataBuf), len(p.Data)), p.Data)
		*outData = (*C.uint8_t)(dataBuf)
	} else {
		*outData = nil
	}
	return 0
}

// --- Keymap ---

//export fg_keymap_load_file
func fg_keymap_load_file(path *C.char, outVersion **C.char, outRuleCount *C.size_t) uintptr {
	p := C.GoString(path)
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
	if context != nil {
		if s := C.GoString(context); s != "" {
			ctx = protocol.KeyMapContext(s)
		}
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
	*outLen = C.size_t(len(data))
	buf := C.malloc(C.size_t(len(data)))
	copy(unsafe.Slice((*byte)(buf), len(data)), data)
	return (*C.uint8_t)(buf)
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

//export fg_wifidirect_request_encode
func fg_wifidirect_request_encode(version C.uint8_t, port C.uint16_t, ipAddr *C.uint8_t, subnet *C.uint8_t, channel C.uint8_t, band C.uint8_t, security C.uint8_t, pskHash *C.uint8_t, outLen *C.size_t) *C.uint8_t {
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
	if pskHash != nil {
		copy(r.PSKHash[:], unsafe.Slice((*byte)(unsafe.Pointer(pskHash)), 4))
	}
	data := r.Marshal()
	*outLen = C.size_t(len(data))
	buf := C.malloc(C.size_t(len(data)))
	copy(unsafe.Slice((*byte)(buf), len(data)), data)
	return (*C.uint8_t)(buf)
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
	*outLen = C.size_t(len(data))
	buf := C.malloc(C.size_t(len(data)))
	copy(unsafe.Slice((*byte)(buf), len(data)), data)
	return (*C.uint8_t)(buf)
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
	sample := m.RecordPong(uint64(pongTimestampUs), uint64(nowUs))
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
	*outLen = C.size_t(len(assembled))
	buf := C.malloc(C.size_t(len(assembled)))
	copy(unsafe.Slice((*byte)(buf), len(assembled)), assembled)
	*outData = (*C.uint8_t)(buf)
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
		Name:     C.GoString(name),
	}
	data := p.Marshal()
	*outLen = C.size_t(len(data))
	buf := C.malloc(C.size_t(len(data)))
	copy(unsafe.Slice((*byte)(buf), len(data)), data)
	return (*C.uint8_t)(buf)
}

//export fg_identify_response_encode
func fg_identify_response_encode(status C.uint8_t, platform C.uint8_t, caps C.uint8_t, name *C.char, outLen *C.size_t) *C.uint8_t {
	p := &protocol.IdentifyResponsePayload{
		Status:   uint8(status),
		Platform: protocol.Platform(platform),
		Caps:     protocol.CapabilityFlags(caps),
		Name:     C.GoString(name),
	}
	data := p.Marshal()
	*outLen = C.size_t(len(data))
	buf := C.malloc(C.size_t(len(data)))
	copy(unsafe.Slice((*byte)(buf), len(data)), data)
	return (*C.uint8_t)(buf)
}

// --- Main (required for c-archive) ---
func main() {}
