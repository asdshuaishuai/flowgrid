## ADDED Requirements

### Requirement: Cross-platform modifier key remapping
The system SHALL automatically remap modifier keys based on the source and target platform types exchanged during IDENTIFY handshake.

#### Scenario: macOS Cmd to Linux/Windows Ctrl
- **WHEN** a macOS Host sends a KEY_DOWN frame with `modifiers=0x08` (LMeta/Cmd) and the Target is Linux or Windows
- **THEN** the Target's KeyMapper SHALL remap the modifier to `0x01` (LCtrl) before injection

#### Scenario: Windows/Linux Ctrl to macOS Cmd
- **WHEN** a Windows or Linux Host sends a KEY_DOWN frame with `modifiers=0x01` (LCtrl) and the Target is macOS
- **THEN** the Target's KeyMapper SHALL remap the modifier to `0x08` (LMeta/Cmd) before injection

#### Scenario: Other modifiers pass through
- **WHEN** a frame contains `modifiers` with bits other than LMeta/LCtrl set (e.g., LShift=0x02, LAlt=0x04)
- **THEN** those bits SHALL pass through unchanged regardless of platform

### Requirement: HAL modifier injection support
The system's PlatformHal trait SHALL accept modifier bits alongside the key code for injection.

#### Scenario: Linux uinput injects modifier + key
- **WHEN** `inject_key_down(0x04, 0x01)` is called (key='A', modifiers=LCtrl)
- **THEN** the Linux HAL SHALL first inject `EV_KEY` events for all active modifier keys (LCtrl), then inject the key code, then emit `EV_SYN`

#### Scenario: Modifier-only change
- **WHEN** a KEY_DOWN frame arrives with `modifiers=0x02` (LShift) but `key_code=0x00` (no key)
- **THEN** the HAL SHALL inject only the modifier key(s) without a main key event

### Requirement: Modifier state tracking
The system SHALL track the current modifier state to prevent duplicate or missing modifier events.

#### Scenario: Consecutive frames with same modifiers
- **WHEN** two consecutive KEY_DOWN frames have identical `modifiers` bits
- **THEN** the system SHALL NOT re-inject already-held modifier keys

#### Scenario: Modifier release on key up
- **WHEN** a KEY_UP frame arrives for a key that was previously injected with modifiers
- **THEN** the system SHALL release the main key but keep held modifiers active (they are released only when their own KEY_UP arrives or when the frame no longer includes them)
