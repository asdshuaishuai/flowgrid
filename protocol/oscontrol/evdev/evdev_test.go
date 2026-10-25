//go:build linux

package evdev

import (
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"testing"
	"time"
	"unsafe"

	"github.com/flowgrid/protocol"
)

func TestHidEvdevTableRoundTrip(t *testing.T) {
	if len(hidToEvdevTable) != 118 {
		t.Errorf("映射表 %d 条，期望 118 条（跟客户端那张表一致）", len(hidToEvdevTable))
	}
	for hid, evdev := range hidToEvdevTable {
		if evdev == 0 {
			t.Errorf("HID 0x%04x 映射到了 evdev 0", hid)
		}
		if back := evdevToHid(evdev); back != hid {
			t.Errorf("evdev 0x%04x 反查得到 HID 0x%04x，期望 0x%04x", evdev, back, hid)
		}
	}
	// 几个必须对的：字母键、修饰键、回车。
	for hid, want := range map[uint16]uint16{
		protocol.HIDKeyA:       KEY_A,
		protocol.HIDKeyC:       KEY_C,
		protocol.HIDKeySpace:   KEY_SPACE,
		protocol.HIDKeyLCtrl:   KEY_LEFTCTRL,
		protocol.HIDKeyLMeta:   KEY_LEFTMETA,
		protocol.HIDKeyEnter:  KEY_ENTER,
	} {
		if got := hidToEvdev(hid); got != want {
			t.Errorf("HID 0x%04x → evdev 0x%04x，期望 0x%04x", hid, got, want)
		}
	}
}

func TestModifierMapping(t *testing.T) {
	for bit, code := range modifierEvdev {
		if got := evdevToModifier(code); got != bit {
			t.Errorf("evdev 0x%04x → 修饰位 0x%02x，期望 0x%02x", code, got, bit)
		}
	}
	if evdevToModifier(KEY_A) != 0 {
		t.Error("普通键不该被认成修饰键")
	}
}

func TestButtonMapping(t *testing.T) {
	if got := evdevToButton(BTN_LEFT); got != protocol.MouseButtonLeft {
		t.Errorf("BTN_LEFT → 按钮 %d，期望 %d", got, protocol.MouseButtonLeft)
	}
	if got := evdevToButton(BTN_RIGHT); got != protocol.MouseButtonRight {
		t.Errorf("BTN_RIGHT → 按钮 %d，期望 %d", got, protocol.MouseButtonRight)
	}
	if got := evdevToButton(KEY_A); got != 0 {
		t.Errorf("键盘键不该被认成鼠标按钮（得到 %d）", got)
	}
}

func TestHorizontalScrollSign(t *testing.T) {
	// 协议约定：水平正=向左；evdev 的 REL_HWHEEL 正=向右 → 翻符号。
	if got := horizontalScrollSign(3); got != -3 {
		t.Errorf("evdev +3 → 协议 %d，期望 -3", got)
	}
	if got := horizontalScrollSign(-2); got != 2 {
		t.Errorf("evdev -2 → 协议 %d，期望 2", got)
	}
}

func TestInjectWithoutUinputFails(t *testing.T) {
	controller := New()
	if err := controller.Inject(protocol.Input{Kind: protocol.InputMouseMove, DeltaX: 1}); err == nil {
		t.Error("没开 uinput 就注入应该报错")
	}
}

func TestCaptureWithoutDevicesFails(t *testing.T) {
	controller := New()
	if _, err := controller.Capture(); err == nil {
		t.Error("没有设备时捕获应该报错")
	}
}

// 真机链路：造一个「物理键鼠」→ 捕获引擎抓到 → 注入引擎写进虚拟设备 →
// 从虚拟设备的 evdev 流读回来核对。
//
// 需要 /dev/uinput 可写 + /dev/input/event* 可读；权限不足时**如实跳过**
// （t.Skip），不假装通过。
func TestHalCaptureThenInject(t *testing.T) {
	if !haveHalPermissions(t) {
		return
	}

	// ① 造一个「物理键鼠」（名字与虚拟设备**故意不同**，否则捕获会按名字跳过）。
	const testDeviceName = "FlowGrid oscontrol Test Device"
	physFd, err := createTestDevice(testDeviceName)
	if err != nil {
		t.Fatalf("造测试设备失败：%v", err)
	}
	defer syscall.Close(physFd)

	// ② **先等 udev 把节点建出来**：捕获引擎是在启动时枚举设备的，
	//    节点还没出现的话，这个设备根本不会被捕获到。
	if !waitForDevice(t, testDeviceName, 5*time.Second) {
		t.Skip("udev 没为测试设备建出 evdev 节点")
	}
	// 节点读不回来就跳过：uinput 造出来的虚拟设备是 root:input 0660，
	// 本用户不在 input 组就打不开（udev 的 uaccess 不覆盖虚拟设备——
	// 它们没有物理路径，systemd 分不出 seat）。
	if !canReadDevice(t, testDeviceName) {
		t.Skip("虚拟设备的 evdev 节点读不回来（本用户不在 input 组；补权限：sudo usermod -aG input $USER）")
	}

	// ③ 捕获端
	capture := New()
	if err := capture.StartCapture(); err != nil {
		t.Fatalf("开捕获失败：%v", err)
	}
	defer capture.Close()

	// ④ 注入端
	inject := New()
	if err := inject.StartInjection(); err != nil {
		t.Fatalf("开注入失败：%v", err)
	}
	defer inject.Close()
	if !waitForDevice(t, VirtualDeviceName, 5*time.Second) {
		t.Skip("udev 没为虚拟设备建出 evdev 节点")
	}

	// ⑤ 捕获在独立 goroutine 里跑（Capture 是阻塞的），主 goroutine 负责发事件与收结果。
	events := make(chan protocol.Input, 32)
	go func() {
		for {
			in, err := capture.Capture()
			if err != nil {
				close(events)
				return
			}
			events <- in
		}
	}()

	// ⑥ 从「物理」设备发出真实的移动与按键
	time.Sleep(200 * time.Millisecond)
	emitMove(physFd, 120, -60)
	emitKey(physFd, KEY_A, 1)
	emitKey(physFd, KEY_A, 0)

	// ⑦ 收结果
	var gotMove, gotDown, gotUp bool
	deadline := time.After(3 * time.Second)
	for !(gotMove && gotDown && gotUp) {
		select {
		case in, ok := <-events:
			if !ok {
				t.Fatal("捕获线程退出了")
			}
			switch in.Kind {
			case protocol.InputMouseMove:
				if in.DeltaX == 120 && in.DeltaY == -60 {
					gotMove = true
				}
			case protocol.InputKeyDown:
				if in.Key == protocol.HIDKeyA {
					gotDown = true
				}
			case protocol.InputKeyUp:
				if in.Key == protocol.HIDKeyA {
					gotUp = true
				}
			}
		case <-deadline:
			if !gotMove {
				t.Error("没捕获到鼠标移动（ΔX=120 ΔY=-60）")
			}
			if !gotDown {
				t.Error("没捕获到按键 A 按下")
			}
			if !gotUp {
				t.Error("没捕获到按键 A 松开")
			}
			return
		}
	}
}

// haveHalPermissions 检查 HAL 需要的权限；不够就跳过测试。
func haveHalPermissions(t *testing.T) bool {
	t.Helper()
	fd, err := syscall.Open("/dev/uinput", syscall.O_WRONLY|syscall.O_NONBLOCK, 0)
	if err != nil {
		t.Skipf("跳过真机链路：/dev/uinput 打不开（%v）", err)
	}
	syscall.Close(fd)

	entries, err := os.ReadDir("/dev/input")
	if err != nil {
		t.Skipf("跳过真机链路：/dev/input 打不开（%v）", err)
	}
	for _, entry := range entries {
		if len(entry.Name()) < 5 || entry.Name()[:5] != "event" {
			continue
		}
		fd, err := syscall.Open("/dev/input/"+entry.Name(), syscall.O_RDONLY|syscall.O_NONBLOCK, 0)
		if err == nil {
			syscall.Close(fd)
			return true
		}
	}
	t.Skip("跳过真机链路：/dev/input 下没有可读的 event 设备")
	return false
}

// canReadDevice 判断指定名字的设备节点能不能被本用户打开。
func canReadDevice(t *testing.T, name string) bool {
	t.Helper()
	entries, err := os.ReadDir("/sys/class/input")
	if err != nil {
		return false
	}
	for _, entry := range entries {
		if len(entry.Name()) < 5 || entry.Name()[:5] != "event" {
			continue
		}
		buf, err := os.ReadFile(filepath.Join("/sys/class/input", entry.Name(), "device", "name"))
		if err != nil || strings.TrimRight(string(buf), "\n") != name {
			continue
		}
		fd, err := syscall.Open("/dev/input/"+entry.Name(), syscall.O_RDONLY|syscall.O_NONBLOCK, 0)
		if err != nil {
			return false
		}
		syscall.Close(fd)
		return true
	}
	return false
}

// waitForDevice 等 udev 把指定名字的设备节点建出来。
func waitForDevice(t *testing.T, name string, timeout time.Duration) bool {
	t.Helper()
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		entries, err := os.ReadDir("/sys/class/input")
		if err == nil {
			for _, entry := range entries {
				if len(entry.Name()) < 5 || entry.Name()[:5] != "event" {
					continue
				}
				buf, err := os.ReadFile(filepath.Join("/sys/class/input", entry.Name(), "device", "name"))
				// sysfs 的属性文件带行尾换行，比名字前要削掉。
				if err == nil && strings.TrimRight(string(buf), "\n") == name {
					return true
				}
			}
		}
		time.Sleep(100 * time.Millisecond)
	}
	return false
}

// createTestDevice 造一个「物理键鼠」。
//
// 刻意**不**声明 BTN_LEFT：X 只把带按钮的设备认成指针，不声明它就不会抢走
// 桌面的光标——这样「光标动了」只能来自注入通路。
func createTestDevice(name string) (int, error) {
	fd, err := syscall.Open("/dev/uinput", syscall.O_WRONLY|syscall.O_NONBLOCK, 0)
	if err != nil {
		return -1, err
	}
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), uiSetEvbit, uintptr(evKey)); errno != 0 {
		syscall.Close(fd)
		return -1, errno
	}
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), uiSetEvbit, uintptr(evRel)); errno != 0 {
		syscall.Close(fd)
		return -1, errno
	}
	syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), uiSetKeybit, uintptr(KEY_A))
	syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), uiSetRelbit, uintptr(REL_X))
	syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), uiSetRelbit, uintptr(REL_Y))

	var device uinputUserDev
	copy(device.Name[:], name)
	device.ID = inputID{Bustype: 0x03, Vendor: 0x1209, Product: 0x0002, Version: 1}
	raw := unsafe.Slice((*byte)(unsafe.Pointer(&device)), unsafe.Sizeof(device))
	if _, err := syscall.Write(fd, raw); err != nil {
		syscall.Close(fd)
		return -1, err
	}
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, uintptr(fd), uiDevCreate, 0); errno != 0 {
		syscall.Close(fd)
		return -1, errno
	}
	return fd, nil
}

func emitKey(fd int, code uint16, value int32) {
	event := inputEvent{Type: evKey, Code: code, Value: value}
	syscall.Write(fd, unsafe.Slice((*byte)(unsafe.Pointer(&event)), inputEventSize))
	syn := inputEvent{Type: evSyn}
	syscall.Write(fd, unsafe.Slice((*byte)(unsafe.Pointer(&syn)), inputEventSize))
}

func emitMove(fd int, dx, dy int32) {
	events := []inputEvent{
		{Type: evRel, Code: REL_X, Value: dx},
		{Type: evRel, Code: REL_Y, Value: dy},
		{Type: evSyn},
	}
	syscall.Write(fd, unsafe.Slice((*byte)(unsafe.Pointer(&events[0])), len(events)*inputEventSize))
}
