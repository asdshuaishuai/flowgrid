// Package oscontrol 是「操作系统输入控制」的抽象层。
//
// 分层：
//
//	protocol（线上格式与换算语义）  ← 纯逻辑，不依赖任何 OS
//	        ↑ 调用
//	oscontrol（本包：OS 控制契约）   ← 只定义接口，不依赖任何 OS
//	        ↑ 实现
//	各平台（evdev/uinput、CGEvent、Windows SendInput…）
//
// 为什么要有这一层：各平台原本各自定义「捕获到的事件」（deepin 是
// EventKind + pressed 位，mac 是 CGEvent 原生字段），于是「修饰键怎么表达」
// 「水平滚轮哪边算正」这些**协议语义**被各端各写一遍，迟早分叉。本包把
// 边界定死：平台只做「OS 原生事件 ↔ protocol.Input」的翻译，帧怎么拼、
// 键位怎么换算，都在协议层。
//
// **依赖倒置**：本包不 import 任何平台的东西。各平台在启动时把自己的实现
// 注册进来（Register），协议层按平台号取（For）。没注册就是取不到——
// 流水线安静地不跑，而不是让协议层去依赖某个具体 OS。
package oscontrol

import (
	"errors"
	"fmt"
	"sync"

	"github.com/flowgrid/protocol"
)

// ErrNotSupported 表示该平台没有注册 OS 控制实现。
var ErrNotSupported = errors.New("oscontrol: 该平台没有注册实现")

// Controller 是各平台实现的「操作系统输入控制」。
//
// 契约（各平台照着实现，跨端行为一致性靠它 + 黄金向量一起钉住）：
//
//   - Capture 返回的必须是 protocol.Input，字段单位按协议来：Key 是 HID
//     Usage ID、Modifiers 是 §6.2 位掩码、水平滚轮正=向左、垂直正=向上。
//     平台自己的方向约定要在实现里翻正了再交上来。
//   - 修饰键本身也要作为 KeyDown/KeyUp 交上来，并且 Modifiers 里带上当前
//     按着的修饰位——协议层据此判断组合键。
//   - 鼠标按钮必须能区分按下与松开（Pressed 位）。
//   - Capture 阻塞直到有事件；返回错误表示捕获结束（设备拔了、权限没了）。
//   - Inject 收到的 Input 已经是协议格式，实现只负责把它变成 OS 的动作。
//   - ReleasePressed 必须松开所有还按着的键与按钮：注入出去的事件不会因为
//     对端消失而自动收尾，不主动松开，对端会一直以为那个键按着。
type Controller interface {
	// Platform 返回这个实现对应的平台号（§3.1.2）。
	Platform() protocol.Platform

	// Capture 阻塞直到捕获到一个输入事件。
	Capture() (protocol.Input, error)

	// Inject 把一次输入事件注入本机。
	Inject(in protocol.Input) error

	// ReleasePressed 松开所有还按着的键与鼠标按钮。
	ReleasePressed() error

	// Close 释放底层资源（设备 fd、事件 tap 等）。
	Close() error
}

var (
	registryMu sync.RWMutex
	factories  = map[protocol.Platform]func() (Controller, error){}
)

// Register 注册某平台的实现。
//
// 同一平台重复注册会覆盖（后注册者赢）——测试里换实现时方便。
func Register(platform protocol.Platform, factory func() (Controller, error)) {
	registryMu.Lock()
	defer registryMu.Unlock()
	factories[platform] = factory
}

// Unregister 撤掉某平台的注册（测试用）。
func Unregister(platform protocol.Platform) {
	registryMu.Lock()
	defer registryMu.Unlock()
	delete(factories, platform)
}

// For 取出某平台的实现。
//
// 没注册就返回 ErrNotSupported——**不返回 nil**：调用方必须处理「这台机器上
// 这个平台没有实现」的情况（多半是没编进来）。
func For(platform protocol.Platform) (Controller, error) {
	registryMu.RLock()
	factory, ok := factories[platform]
	registryMu.RUnlock()
	if !ok {
		return nil, fmt.Errorf("%w: 平台 0x%02x", ErrNotSupported, uint8(platform))
	}
	return factory()
}

// Registered 列出已注册的平台（诊断用）。
func Registered() []protocol.Platform {
	registryMu.RLock()
	defer registryMu.RUnlock()
	out := make([]protocol.Platform, 0, len(factories))
	for platform := range factories {
		out = append(out, platform)
	}
	return out
}
