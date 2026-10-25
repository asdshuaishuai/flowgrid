//go:build linux

// Package evdev 是 oscontrol 的 Linux 实现：evdev 捕获 + uinput 注入。
//
// 它是「OS 机制」那一层：只负责把 Linux 的事件翻译成 protocol.Input、把
// protocol.Input 翻译成 Linux 的动作。协议语义（帧怎么拼、键位怎么重映射）
// 全在协议层——本包一行都不碰。
//
// 行为对齐 deepin 客户端现有那套 C++ 引擎（deepin/src/core/input/input_engine.cpp），
// 两边读的是同一份协议语义，所以结果必须一致——由协议层的黄金向量钉住。
package evdev

import (
	"errors"
	"fmt"
	"os"
	"sync"
	"syscall"
	"unsafe"

	"github.com/flowgrid/protocol"
	"github.com/flowgrid/protocol/oscontrol"
)

// VirtualDeviceName 是注入侧造出来的虚拟设备名。
//
// 捕获侧必须按名字跳过它：否则注入出去的事件会被自己再抓一遍，形成回环
// （deepin 的捕获引擎按名字跳过，mac 用注入标记跳过——同一目的，两种 OS 机制）。
const VirtualDeviceName = "FlowGrid Virtual Input"

const (
	ioctlRead  = 0x80000000
	ioctlWrite = 0x40000000

	evKey   = 0x01
	evRel   = 0x02
	evSyn   = 0x00
	evMax   = 0x1f
	keyMax  = 0x2ff
	inputEventSize = 24
)

// pollFd 是内核 struct pollfd 的布局（标准库 syscall 里没有 Poll，
// x/sys/unix 才有——这里不想为一个调用拖一个额外依赖）。
type pollFd struct {
	Fd      int32
	Events  int16
	Revents int16
}

const (
	pollIn   = 0x0001
	pollErr  = 0x0008
	pollHup  = 0x0010
	pollNval = 0x0020
)

// pollFds 调内核的 poll(2)。
func pollFds(fds []pollFd, timeoutMs int) (int, error) {
	if len(fds) == 0 {
		return 0, nil
	}
	n, _, errno := syscall.Syscall(syscall.SYS_POLL, uintptr(unsafe.Pointer(&fds[0])),
		uintptr(len(fds)), uintptr(timeoutMs))
	if errno != 0 {
		return 0, errno
	}
	return int(n), nil
}

// ioctl 编号（照 Linux 的 _IO/_IOR 公式算，跟 <linux/input.h> 一致）。
//
// **nr 字段是 ev + 0x20**，不是 ev：<linux/input.h> 里 EVIOCGBIT 的定义带了
// 这个偏移。写成 nr = ev 的话，命令编码跟内核对不上，内核的 ioctl 分发器
// 直接回 EINVAL——而 EVIOCGNAME 之类不偏的调用照样成功，症状特别难查。
func eviocgbit(ev, size int) uintptr {
	return ioctlRead | uintptr(size)<<16 | uintptr('E')<<8 | uintptr(ev+0x20)
}

func eviocgname(size int) uintptr {
	return ioctlRead | uintptr(size)<<16 | uintptr('E')<<8 | 0x06
}

// ioctl 编号照 <linux/uinput.h> 的 _IO/_IOW 公式算。
//
// 注意 **UI_DEV_CREATE 是 _IO（没有 size 字段）**，不是 _IOW：写成 _IOW 的
// 编码（多带一个 size=4）的话，内核的 ioctl 分发器按命令编码比对不上，
// 直接回 EINVAL——而且前面那些 UI_SET_* 都成功，看着像参数问题，很难查。
const (
	uiSetEvbit   = ioctlWrite | uintptr('U')<<8 | 4<<16 | 100
	uiSetKeybit  = ioctlWrite | uintptr('U')<<8 | 4<<16 | 101
	uiSetRelbit  = ioctlWrite | uintptr('U')<<8 | 4<<16 | 102
	uiDevCreate  = uintptr('U')<<8 | 1
	uiDevDestroy = uintptr('U')<<8 | 2
)

// 内核结构体（布局照 <linux/input.h> / <linux/uinput.h>，64 位下 event 24 字节）。
type inputEvent struct {
	Time  [16]byte
	Type  uint16
	Code  uint16
	Value int32
}

type inputID struct {
	Bustype uint16
	Vendor  uint16
	Product uint16
	Version uint16
}

type uinputUserDev struct {
	Name         [80]byte
	ID           inputID
	FFEffectsMax uint32
	AbsMax       [64]uint32
	AbsMin       [64]uint32
	AbsFuzz      [64]uint32
	AbsFlat      [64]uint32
}

// 协议修饰位按固定顺序遍历（行为可预期：合成/释放修饰键的顺序稳定）。
var modifierBits = []uint8{
	protocol.ModLeftCtrl, protocol.ModLeftShift, protocol.ModLeftAlt, protocol.ModLeftMeta,
	protocol.ModRightCtrl, protocol.ModRightShift, protocol.ModRightAlt, protocol.ModRightMeta,
}

// Controller 是 Linux 的 OS 控制实现。
//
// 一个实例既能捕获也能注入（回环联调时两个都要）。生产上通常是两个实例：
// 控制端只开捕获，被控端只开注入。
type Controller struct {
	platform protocol.Platform

	mu         sync.Mutex
	queue      []protocol.Input // 已翻译好、等着被 Capture 取走的事件
	pendingX   int32
	pendingY   int32
	modifiers  uint8
	devices    []captureDevice
	closed     bool

	// 注入侧状态
	uinputFd       int
	injectedMods   uint8
	syntheticMods  uint8
	injectedKeys   map[uint16]bool
	injectedButtons uint8
}

type captureDevice struct {
	fd       int
	keyboard bool
	mouse    bool
}

// New 造一个 Linux 控制器（还没打开任何设备）。
func New() *Controller {
	return &Controller{
		platform:     protocol.PlatformLinux,
		uinputFd:     -1,
		injectedKeys: map[uint16]bool{},
	}
}

// Platform 实现 oscontrol.Controller。
func (c *Controller) Platform() protocol.Platform { return c.platform }

// StartCapture 打开 /dev/input/event* 开始捕获。
//
// 只收「像键盘或鼠标」的设备（跟 deepin 现有引擎同一套判据），并且**跳过
// 我们自己造的虚拟设备**。
func (c *Controller) StartCapture() error {
	entries, err := os.ReadDir("/dev/input")
	if err != nil {
		return fmt.Errorf("打开 /dev/input 失败：%w", err)
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	for _, entry := range entries {
		name := entry.Name()
		if len(name) < 5 || name[:5] != "event" {
			continue
		}
		fd, err := syscall.Open("/dev/input/"+name, syscall.O_RDONLY|syscall.O_NONBLOCK|syscall.O_CLOEXEC, 0)
		if err != nil {
			continue // 权限不足：跳过，最后按「一个都没有」报错
		}
		if deviceName(fd) == VirtualDeviceName {
			syscall.Close(fd)
			continue
		}
		keyboard, mouse, ok := classifyDevice(fd)
		if !ok {
			syscall.Close(fd)
			continue
		}
		c.devices = append(c.devices, captureDevice{fd: fd, keyboard: keyboard, mouse: mouse})
	}
	if len(c.devices) == 0 {
		return errors.New("没有可读的 /dev/input/event* 键盘/鼠标（检查 99-flowgrid-input.rules 与 input 组权限）")
	}
	return nil
}

// StartInjection 打开 /dev/uinput 造出虚拟键鼠。
func (c *Controller) StartInjection() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.uinputFd >= 0 {
		return nil
	}
	fd, err := syscall.Open("/dev/uinput", syscall.O_WRONLY|syscall.O_NONBLOCK|syscall.O_CLOEXEC, 0)
	if err != nil {
		return fmt.Errorf("打开 /dev/uinput 失败：%w（需要 uinput 权限）", err)
	}
	if err := createUinputDevice(fd, VirtualDeviceName); err != nil {
		syscall.Close(fd)
		return err
	}
	c.uinputFd = fd
	return nil
}

// createUinputDevice 在已打开的 /dev/uinput 上造一个虚拟设备。
//
// 抽成包级函数：测试要造一个「物理」设备来喂捕获引擎，名字与虚拟设备不同。
func createUinputDevice(fd int, name string) error {
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), uiSetEvbit, uintptr(evKey)); errno != 0 {
		return fmt.Errorf("配置 uinput 失败：%w", errno)
	}
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), uiSetEvbit, uintptr(evRel)); errno != 0 {
		return fmt.Errorf("配置 uinput 失败：%w", errno)
	}
	for key := 1; key <= keyMax; key++ {
		if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), uiSetKeybit, uintptr(key)); errno != 0 {
			return fmt.Errorf("配置 uinput 失败：%w", errno)
		}
	}
	for _, rel := range []int{REL_X, REL_Y, REL_WHEEL, REL_HWHEEL} {
		if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), uiSetRelbit, uintptr(rel)); errno != 0 {
			return fmt.Errorf("配置 uinput 失败：%w", errno)
		}
	}

	var device uinputUserDev
	copy(device.Name[:], name)
	device.ID = inputID{Bustype: 0x03, Vendor: 0x1209, Product: 0x0001, Version: 1} // BUS_USB + pid.codes VID
	raw := unsafe.Slice((*byte)(unsafe.Pointer(&device)), unsafe.Sizeof(device))
	size := len(raw)
	written := 0
	for written < size {
		n, err := syscall.Write(fd, raw[written:])
		if err != nil {
			return fmt.Errorf("写 uinput 设备信息失败：%w", err)
		}
		written += n
	}
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), uiDevCreate, 0); errno != 0 {
		return fmt.Errorf("创建 uinput 设备失败：%w", errno)
	}
	return nil
}

// Capture 实现 oscontrol.Controller：阻塞直到捕获到一个输入事件。
func (c *Controller) Capture() (protocol.Input, error) {
	for {
		c.mu.Lock()
		if c.closed {
			c.mu.Unlock()
			return protocol.Input{}, errors.New("捕获已关闭")
		}
		if len(c.queue) > 0 {
			in := c.queue[0]
			c.queue = c.queue[1:]
			c.mu.Unlock()
			return in, nil
		}
		devices := c.devices
		c.mu.Unlock()

		if len(devices) == 0 {
			return protocol.Input{}, errors.New("没有在捕获的设备")
		}
		fds := make([]pollFd, len(devices))
		for i, device := range devices {
			fds[i] = pollFd{Fd: int32(device.fd), Events: pollIn}
		}
		ready, err := pollFds(fds, 5) // 5ms：位移攒够 5ms 就吐出去，手感才跟手
		if err != nil {
			if err == syscall.EINTR {
				continue
			}
			return protocol.Input{}, fmt.Errorf("poll(/dev/input) 失败：%w", err)
		}
		if ready == 0 {
			// 超时：把攒着的位移吐出来（鼠标停住时最后一段位移要送到）。
			c.mu.Lock()
			c.flushMoveLocked()
			c.mu.Unlock()
			continue
		}
		for i := range fds {
			if fds[i].Revents == 0 {
				continue
			}
			if fds[i].Revents&(pollErr|pollHup|pollNval) != 0 {
				continue // 设备被拔掉：跳过这一个
			}
			events, err := readEvents(devices[i].fd)
			if err != nil {
				continue
			}
			c.mu.Lock()
			c.translateLocked(events)
			c.mu.Unlock()
		}
	}
}

// translateLocked 把一批 evdev 事件翻译成协议事件，塞进队列。
func (c *Controller) translateLocked(events []inputEvent) {
	for _, raw := range events {
		switch raw.Type {
		case evKey:
			down := raw.Value == 1
			if raw.Value != 0 && raw.Value != 1 {
				continue // 自动重复交给目标机
			}
			if button := evdevToButton(raw.Code); button != 0 {
				c.flushMoveLocked()
				c.queue = append(c.queue, protocol.Input{
					Kind:      protocol.InputMouseButton,
					Button:    button,
					Pressed:   down,
					Modifiers: c.modifiers,
				})
				continue
			}
			hid := evdevToHid(raw.Code)
			if hid == 0 {
				continue
			}
			if bit := evdevToModifier(raw.Code); bit != 0 {
				if down {
					c.modifiers |= bit
				} else {
					c.modifiers &^= bit
				}
			}
			c.flushMoveLocked()
			kind := protocol.InputKeyUp
			if down {
				kind = protocol.InputKeyDown
			}
			c.queue = append(c.queue, protocol.Input{
				Kind:      kind,
				Key:       hid,
				Modifiers: c.modifiers,
			})
		case evRel:
			switch raw.Code {
			case REL_X:
				c.pendingX += int32(raw.Value)
			case REL_Y:
				c.pendingY += int32(raw.Value)
			case REL_WHEEL:
				// 垂直：evdev 正=向上 = 协议正=向上，不翻。
				c.flushMoveLocked()
				c.queue = append(c.queue, protocol.Input{
					Kind:      protocol.InputScroll,
					DeltaY:    int16(raw.Value),
					Modifiers: c.modifiers,
				})
			case REL_HWHEEL:
				// 水平：evdev 正=向右，协议正=向左，翻一次。
				c.flushMoveLocked()
				c.queue = append(c.queue, protocol.Input{
					Kind:      protocol.InputScroll,
					DeltaX:    horizontalScrollSign(int16(raw.Value)),
					Modifiers: c.modifiers,
				})
			}
		}
	}
}

// flushMoveLocked 把攒着的位移吐成一个 mouse_move 事件。
func (c *Controller) flushMoveLocked() {
	if c.pendingX == 0 && c.pendingY == 0 {
		return
	}
	c.queue = append(c.queue, protocol.Input{
		Kind:      protocol.InputMouseMove,
		DeltaX:    clampInt16(c.pendingX),
		DeltaY:    clampInt16(c.pendingY),
		Modifiers: c.modifiers,
	})
	c.pendingX, c.pendingY = 0, 0
}

func clampInt16(v int32) int16 {
	if v > 32767 {
		return 32767
	}
	if v < -32768 {
		return -32768
	}
	return int16(v)
}

// Inject 实现 oscontrol.Controller：把一次协议事件注入本机。
func (c *Controller) Inject(in protocol.Input) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.uinputFd < 0 {
		return errors.New("uinput 尚未初始化")
	}
	switch in.Kind {
	case protocol.InputKeyDown:
		return c.injectKeyLocked(in.Key, true, in.Modifiers)
	case protocol.InputKeyUp:
		return c.injectKeyLocked(in.Key, false, in.Modifiers)
	case protocol.InputMouseMove:
		if in.DeltaX == 0 && in.DeltaY == 0 {
			return nil
		}
		events := make([]inputEvent, 0, 3)
		if in.DeltaX != 0 {
			events = append(events, inputEvent{Type: evRel, Code: REL_X, Value: int32(in.DeltaX)})
		}
		if in.DeltaY != 0 {
			events = append(events, inputEvent{Type: evRel, Code: REL_Y, Value: int32(in.DeltaY)})
		}
		events = append(events, inputEvent{Type: evSyn, Code: 0})
		return c.writeEventsLocked(events)
	case protocol.InputMouseButton:
		code, ok := buttonEvdev[in.Button]
		if !ok {
			return nil // §6.4 未列出的按钮：静默忽略
		}
		value := int32(0)
		if in.Pressed {
			value = 1
		}
		if err := c.writeEventsLocked([]inputEvent{
			{Type: evKey, Code: code, Value: value},
			{Type: evSyn, Code: 0},
		}); err != nil {
			return err
		}
		if in.Pressed {
			c.injectedButtons |= 1 << (in.Button - 1)
		} else {
			c.injectedButtons &^= 1 << (in.Button - 1)
		}
		return nil
	case protocol.InputScroll:
		events := make([]inputEvent, 0, 3)
		if in.DeltaY != 0 {
			events = append(events, inputEvent{Type: evRel, Code: REL_WHEEL, Value: int32(in.DeltaY)})
		}
		if in.DeltaX != 0 {
			// 水平：协议正=向左，evdev 正=向右，翻一次。
			events = append(events, inputEvent{Type: evRel, Code: REL_HWHEEL, Value: int32(horizontalScrollSign(in.DeltaX))})
		}
		if len(events) == 0 {
			return nil
		}
		events = append(events, inputEvent{Type: evSyn, Code: 0})
		return c.writeEventsLocked(events)
	default:
		return fmt.Errorf("不认识的操作类型 %d", uint8(in.Kind))
	}
}

// injectKeyLocked 注入一个按键。
//
// 修饰键的处理跟 deepin 现有引擎一致：KEY_DOWN 载荷里的掩码可能在目标机上
// 没被真的按住（对端只发了主键），注入侧要**补按**这些修饰键；主键松开时
// 再把「合成」的修饰键松开。少了这一步，对端的应用看到的就是「按了 C」，
// 而不是「Ctrl+C」。
func (c *Controller) injectKeyLocked(hid uint16, down bool, modifiers uint8) error {
	code := hidToEvdev(hid)
	if code == 0 {
		return nil // §6.1 未覆盖的键：静默忽略
	}
	if bit := evdevToModifier(code); bit != 0 {
		if down {
			if c.injectedMods&bit == 0 {
				if err := c.emitKeyLocked(code, true); err != nil {
					return err
				}
				c.injectedMods |= bit
			}
			// 真实按键收到后，修饰键的所有权从「合成」交还物理键。
			c.syntheticMods &^= bit
		} else {
			if c.injectedMods&bit != 0 {
				if err := c.emitKeyLocked(code, false); err != nil {
					return err
				}
				c.injectedMods &^= bit
			}
			c.syntheticMods &^= bit
		}
		return nil
	}

	wanted := modifiers & (protocol.ModCtrl | protocol.ModShift | protocol.ModAlt | protocol.ModMeta)
	if down {
		// 掩码里有、但目标机上还没按住的修饰键，补按。
		for _, bit := range modifierBits {
			if wanted&bit != 0 && c.injectedMods&bit == 0 {
				if err := c.emitKeyLocked(modifierEvdev[bit], true); err != nil {
					return err
				}
				c.injectedMods |= bit
				c.syntheticMods |= bit
			}
		}
		// 不再需要的合成修饰键立刻松开；真实按下的位保持。
		for _, bit := range modifierBits {
			if c.syntheticMods&bit != 0 && wanted&bit == 0 {
				if err := c.emitKeyLocked(modifierEvdev[bit], false); err != nil {
					return err
				}
				c.injectedMods &^= bit
				c.syntheticMods &^= bit
			}
		}
		if err := c.emitKeyLocked(code, true); err != nil {
			return err
		}
		c.injectedKeys[hid] = true
		return nil
	}
	if err := c.emitKeyLocked(code, false); err != nil {
		return err
	}
	delete(c.injectedKeys, hid)
	return c.releaseSyntheticLocked()
}

// releaseSyntheticLocked 松开「合成」的修饰键。
func (c *Controller) releaseSyntheticLocked() error {
	for _, bit := range modifierBits {
		if c.syntheticMods&bit == 0 {
			continue
		}
		if err := c.emitKeyLocked(modifierEvdev[bit], false); err != nil {
			return err
		}
		c.injectedMods &^= bit
		c.syntheticMods &^= bit
	}
	return nil
}

func (c *Controller) emitKeyLocked(code uint16, down bool) error {
	value := int32(0)
	if down {
		value = 1
	}
	return c.writeEventsLocked([]inputEvent{
		{Type: evKey, Code: code, Value: value},
		{Type: evSyn, Code: 0},
	})
}

func (c *Controller) writeEventsLocked(events []inputEvent) error {
	raw := unsafe.Slice((*byte)(unsafe.Pointer(&events[0])), len(events)*inputEventSize)
	size := len(raw)
	written := 0
	for written < size {
		n, err := syscall.Write(c.uinputFd, raw[written:])
		if err != nil {
			return fmt.Errorf("写入 uinput 失败：%w", err)
		}
		written += n
	}
	return nil
}

// ReleasePressed 实现 oscontrol.Controller：松开所有还按着的键与按钮。
//
// 必须做：注入出去的事件不会因为对端消失而自动收尾，不主动松开，对端会
// 一直以为那个键按着（典型症状是「远程机器上有个键卡住了」）。
func (c *Controller) ReleasePressed() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.uinputFd < 0 {
		return nil
	}
	// 先松鼠标键与普通键，再松修饰键：与真实输入设备的收尾顺序一致。
	for button := uint8(1); button <= 5; button++ {
		if c.injectedButtons&(1<<(button-1)) != 0 {
			if code, ok := buttonEvdev[button]; ok {
				_ = c.emitKeyLocked(code, false)
			}
		}
	}
	for hid := range c.injectedKeys {
		if code := hidToEvdev(hid); code != 0 {
			_ = c.emitKeyLocked(code, false)
		}
	}
	for _, bit := range modifierBits {
		if c.injectedMods&bit != 0 {
			_ = c.emitKeyLocked(modifierEvdev[bit], false)
		}
	}
	c.injectedButtons = 0
	c.injectedKeys = map[uint16]bool{}
	c.injectedMods = 0
	c.syntheticMods = 0
	return nil
}

// Close 实现 oscontrol.Controller：释放设备。
func (c *Controller) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.closed = true
	for i := range c.devices {
		syscall.Close(c.devices[i].fd)
	}
	c.devices = nil
	if c.uinputFd >= 0 {
		syscall.Syscall(syscall.SYS_IOCTL, uintptr(c.uinputFd), uiDevDestroy, 0)
		syscall.Close(c.uinputFd)
		c.uinputFd = -1
	}
	return nil
}

// 编译期确认实现了 oscontrol.Controller。
var _ oscontrol.Controller = (*Controller)(nil)

// ── 小工具 ────────────────────────────────────────────────────────────

func deviceName(fd int) string {
	buf := make([]byte, 256)
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), eviocgname(len(buf)), uintptr(unsafe.Pointer(&buf[0]))); errno != 0 {
		return ""
	}
	for i, b := range buf {
		if b == 0 {
			return string(buf[:i])
		}
	}
	return string(buf)
}

// classifyDevice 判断这个 evdev 设备是不是键盘或鼠标（跟 deepin 现有引擎同一套判据）。
func classifyDevice(fd int) (keyboard bool, mouse bool, ok bool) {
	// EVIOCGBIT(0, len) 拿的是「支持哪些事件类型」；EVIOCGBIT(EV_KEY, len)
	// 拿的才是键码位图——两者别搞混（搞混了 hasEvKey 变成「有没有 ESC 键」，
	// 纯鼠标设备会被判成不是鼠标）。
	evBits := make([]byte, (evMax/8)+1)
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), eviocgbit(0, len(evBits)), uintptr(unsafe.Pointer(&evBits[0]))); errno != 0 {
		return false, false, false
	}
	keyBits := make([]byte, (keyMax/8)+1)
	syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), eviocgbit(evKey, len(keyBits)), uintptr(unsafe.Pointer(&keyBits[0])))
	relBits := make([]byte, (0x0f/8)+1)
	syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), eviocgbit(evRel, len(relBits)), uintptr(unsafe.Pointer(&relBits[0])))

	bitSet := func(bits []byte, index int) bool {
		if index/8 >= len(bits) {
			return false
		}
		return bits[index/8]&(1<<uint(index%8)) != 0
	}
	hasEvKey := bitSet(evBits, evKey)
	hasEvRel := bitSet(evBits, evRel)
	keyboard = hasEvKey && (bitSet(keyBits, KEY_A) || bitSet(keyBits, KEY_Z) || bitSet(keyBits, KEY_SPACE))
	mouse = hasEvKey && bitSet(keyBits, BTN_LEFT) && (hasEvRel || (bitSet(relBits, REL_X) && bitSet(relBits, REL_Y)))
	return keyboard, mouse, keyboard || mouse
}

func readEvents(fd int) ([]inputEvent, error) {
	buf := make([]byte, 64*inputEventSize)
	n, err := syscall.Read(fd, buf)
	if err != nil {
		if err == syscall.EAGAIN || err == syscall.EINTR {
			return nil, nil
		}
		return nil, err
	}
	count := n / inputEventSize
	events := make([]inputEvent, count)
	for i := 0; i < count; i++ {
		offset := i * inputEventSize
		events[i].Type = uint16(buf[offset+16]) | uint16(buf[offset+17])<<8
		events[i].Code = uint16(buf[offset+18]) | uint16(buf[offset+19])<<8
		events[i].Value = int32(uint32(buf[offset+20]) | uint32(buf[offset+21])<<8 |
			uint32(buf[offset+22])<<16 | uint32(buf[offset+23])<<24)
	}
	return events, nil
}
