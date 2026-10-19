# windows-input

## ADDED Requirements

### Requirement: HID 注入

入站 KEY_DOWN/KEY_UP（HID Usage + 修饰键位掩码）、MOUSE_MOVE（相对像素）、MOUSE_BTN（键号+状态）、SCROLL（像素滚轮）帧经 SendInput 注入本机；HID→VK 按 §6.1 映射，右侧修饰键与导航键带扩展键标志。

#### Scenario: 键盘注入

- **WHEN** 收到 KEY_DOWN(HIDKeyA, ModCtrl) 后收到 KEY_UP(HIDKeyA)
- **THEN** 本机产生 Ctrl+A 按下与释放（VK 注入，扩展键标志按映射表）

#### Scenario: 鼠标注入

- **WHEN** 收到 MOUSE_MOVE(dx,dy) / MOUSE_BTN(left,press) / SCROLL(up,0)
- **THEN** 本机产生相对鼠标移动 / 左键按下 / 垂直滚动

### Requirement: 捕获与切换

低级钩子捕获本机键盘鼠标；仅当处于「托管某对端」状态时转发 HID 帧给该对端并吞掉本机事件；切换热键 Ctrl+Alt+0（回本机）/ Ctrl+Alt+1..8（在线对端按实例名序）在任意时刻可用。

#### Scenario: 托管期间转发并吞键

- **WHEN** 托管 Beta 期间本机敲击 A
- **THEN** Beta 收到 KEY_DOWN(HIDKeyA, 当前修饰键) 且本机无按键注入（钩子吞掉）

#### Scenario: 切换回本机

- **WHEN** 托管期间按下 Ctrl+Alt+0
- **THEN** 停止托管，本机输入恢复直通

### Requirement: 键位映射

捕获侧先按 §6.3 RemapModifiers 做跨平台修饰键语义换算，再查 keymap.yaml 规则表（存在则 Lookup 替换键码）；默认（无规则文件）仅修饰键语义。规则文件加载一次，路径 `%APPDATA%\FlowGrid\keymap.yaml`。

#### Scenario: 跨平台修饰键

- **WHEN** Windows 端托管 macOS 端并敲击 Ctrl+C
- **THEN** macOS 端收到 Meta(命令键)+C 语义

### Requirement: 输入状态可见

当前托管状态经 input.status/input.changed 暴露（托管对端实例或 null）。

#### Scenario: 状态推送

- **WHEN** 切换托管目标或回本机
- **THEN** 推送 input.changed（hosting 实例名或空）
