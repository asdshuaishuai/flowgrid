package protocol

import "fmt"

// InputEvent is one recognized input event decoded from a KEY_*/MOUSE_*/SCROLL
// frame and remapped for the target platform (§5.3 / §6). Clients inject it via
// their platform HAL; they do not re-implement frame decoding or cross-platform
// remapping — that capability lives here, in the protocol layer.
//
// Type is one of FrameKeyDown, FrameKeyUp, FrameMouseMove, FrameMouseButton,
// FrameScroll. Which fields carry meaning depends on Type:
//   - key events (FrameKeyDown/FrameKeyUp): KeyCode + Modifiers, already
//     remapped to ToPlatform;
//   - mouse move / scroll (FrameMouseMove/FrameScroll): DeltaX, DeltaY;
//   - mouse button (FrameMouseButton): Button + Pressed.
type InputEvent struct {
	Type uint8

	// Key events.
	KeyCode   uint16
	Modifiers uint8

	// Mouse move / scroll.
	DeltaX int16
	DeltaY int16

	// Mouse button.
	Button  uint8
	Pressed bool

	// Provenance for logs / telemetry.
	FromPlatform  Platform
	ToPlatform    Platform
	OrigKeyCode   uint16 // pre-remap HID Usage ID (key events)
	OrigModifiers uint8  // pre-remap modifier mask (key events)
	Remapped      bool   // a rule or the platform default changed key/mods
}

// DecodeInputFrame recognizes one input frame and remaps it from → to.
//
// Key events honor an explicit KeyMapTable rule first (context-scoped, §11.2),
// then fall back to the platform-default Ctrl↔Meta conversion (RemapModifierKey
// / RemapModifiers, §6.3). table may be nil to use the platform default only.
// Mouse and scroll frames carry no key semantics and pass through unchanged.
//
// It returns an ErrInvalidPayload error for a malformed payload and for a frame
// type that is not an input frame.
func DecodeInputFrame(frameType uint8, payload []byte, from, to Platform, table *KeyMapTable, context KeyMapContext) (InputEvent, error) {
	if context == "" {
		context = ContextGlobal
	}
	ev := InputEvent{Type: frameType, FromPlatform: from, ToPlatform: to}
	switch frameType {
	case FrameKeyDown, FrameKeyUp:
		var code uint16
		var mods uint8
		if frameType == FrameKeyDown {
			kd, err := UnmarshalKeyDown(payload)
			if err != nil {
				return InputEvent{}, err
			}
			code, mods = kd.KeyCode, kd.Modifiers
		} else {
			ku, err := UnmarshalKeyUp(payload)
			if err != nil {
				return InputEvent{}, err
			}
			code = ku.KeyCode // KEY_UP carries no modifier mask (§5.3)
		}
		ev.OrigKeyCode, ev.OrigModifiers = code, mods
		rk, rm := remapKey(code, mods, from, to, table, context)
		ev.KeyCode, ev.Modifiers = rk, rm
		ev.Remapped = rk != code || rm != mods
	case FrameMouseMove:
		mp, err := UnmarshalMouseMove(payload)
		if err != nil {
			return InputEvent{}, err
		}
		ev.DeltaX, ev.DeltaY = mp.DeltaX, mp.DeltaY
	case FrameMouseButton:
		bp, err := UnmarshalMouseButton(payload)
		if err != nil {
			return InputEvent{}, err
		}
		ev.Button, ev.Pressed = bp.ButtonID, bp.State == 0x01
	case FrameScroll:
		sp, err := UnmarshalScroll(payload)
		if err != nil {
			return InputEvent{}, err
		}
		ev.DeltaX, ev.DeltaY = sp.DeltaX, sp.DeltaY
	default:
		return InputEvent{}, NewError(ErrInvalidPayload, fmt.Sprintf("frame 0x%02x is not an input frame", frameType))
	}
	return ev, nil
}

// remapKey applies an explicit keymap rule (if any) then the platform-default
// modifier conversion, keeping the key code and modifier mask consistent
// (§6.3 / §11). from == to is a no-op.
func remapKey(code uint16, mods uint8, from, to Platform, table *KeyMapTable, context KeyMapContext) (uint16, uint8) {
	if from == to {
		return code, mods
	}
	if table != nil {
		if rk, rm, ok := table.Lookup(from, to, code, mods, context); ok {
			return rk, rm
		}
	}
	return RemapModifierKey(code, from, to), RemapModifiers(mods, from, to)
}

// Describe renders a short human-readable summary for logs / UI.
func (e InputEvent) Describe() string {
	switch e.Type {
	case FrameKeyDown, FrameKeyUp:
		verb := "keyup"
		if e.Type == FrameKeyDown {
			verb = "keydown"
		}
		s := fmt.Sprintf("%s 键码 0x%04x", verb, e.KeyCode)
		if e.Modifiers != 0 {
			s += fmt.Sprintf(" 修饰 0x%02x", e.Modifiers)
		}
		if e.Remapped {
			s += fmt.Sprintf("（%s→%s：0x%04x/0x%02x ⇒ 0x%04x/0x%02x）",
				e.FromPlatform, e.ToPlatform, e.OrigKeyCode, e.OrigModifiers, e.KeyCode, e.Modifiers)
		}
		return s
	case FrameMouseMove:
		return fmt.Sprintf("mousemove ΔX=%d ΔY=%d", e.DeltaX, e.DeltaY)
	case FrameMouseButton:
		state := "释放"
		if e.Pressed {
			state = "按下"
		}
		return fmt.Sprintf("mousebutton 按键 %d %s", e.Button, state)
	case FrameScroll:
		return fmt.Sprintf("scroll ΔY=%d ΔX=%d", e.DeltaY, e.DeltaX)
	default:
		return fmt.Sprintf("未知输入帧 0x%02x", e.Type)
	}
}
