# windows-tray

## ADDED Requirements

### Requirement: 托盘浮板

托盘图标左键唤出 300px 浮板（设计 tray.html）：状态头、本机卡片、快速切换（本机 + 在线对端，索引与热键一致）、连接/数据入口（带实时计数）、剪贴板同步开关、退出。浮板失焦自动隐藏。

#### Scenario: 快速切换

- **WHEN** 在浮板点击某在线对端的切换按钮
- **THEN** 托管状态切换（同 Ctrl+Alt+N），激活按钮高亮，主窗口同步收到 input.changed

#### Scenario: 唤出与隐藏

- **WHEN** 托盘图标左键点击
- **THEN** 浮板在任务栏侧显示；点击浮板外任意处后隐藏

### Requirement: 入口与退出

浮板入口打开主窗口并导航到对应屏；「退出 FlowGrid」结束 UI 进程（含托盘），不影响 fgcore。
