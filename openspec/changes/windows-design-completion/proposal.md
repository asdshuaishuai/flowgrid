# 设计稿收口：剪贴板历史 / 托盘浮板 / 设置与键位对齐

## Why

客户端功能链路已全通，但对照 design/unified 还有四块缺口：剪贴板屏仍是占位（设计稿有预览卡/历史/过滤/同步设置）、托盘浮板（tray.html 300px）未实现、设置屏只有骨架（设计稿有连接/输入/剪贴板/安全四组）、键位屏缺映射行展示。本变更把这些按设计稿补齐，单机（Windows 一台）双进程可验收。

## What Changes

- **fgcore 剪贴板历史**：内存环形存储（默认 50 条，可调 50/100/200），记录文本、来源对端、方向、大小、时间；IPC `clipboard.history / clipboard.entry / clipboard.clear / clipboard.cap`；`clipboard.enable`（跨端同步开关）；`config.autoaccept`（新设备需确认开关，运行时可变）；`trust.list / trust.revoke`（已授权设备管理）
- **UI 剪贴板屏**：当前剪贴板预览卡（内容/大小/来源/写入本地/推送）、过滤+搜索（全部/文本/链接，图片与代码无同步源置灰语义）、历史列表（预览行/来源/大小/时间/写入本地）、同步设置折叠区（跨端同步/接收自动写入可用，其余置灰并说明）、页脚清空历史
- **UI 托盘浮板**：300px 无边框窗，托盘左键唤出、失焦隐藏；头部状态、本机卡片、快速切换（0..n 按钮 = SwitchTo）、连接/数据与同步入口（带计数）、剪贴板同步开关、退出
- **UI 设置屏**：连接（开机自启 = HKCU Run 注册表，真实；优先链路/自动路由置灰注明）、输入（切换热键展示、键位映射跳转）、剪贴板（跨端同步开关、历史条数）、安全（端到端加密徽章、新设备需确认开关 = autoaccept、已授权设备管理 = 列表+撤销）
- **UI 键位屏**：默认跨平台映射行（§6.3 Windows→macOS/Linux 语义）+ 规则表状态
- **Non-Goals**：图片/富文本剪贴板、IntelligentLink 链路、鼠标平滑/边缘切换、密码管理器排除列表、YAML 热重载

## Capabilities

### New Capabilities
- `windows-clipboard-history`: 历史存储、过滤语义、清空与写回
- `windows-tray`: 托盘浮板形态、唤出/隐藏、快速切换语义

## Impact

- fgcore：hub 剪贴板历史/开关、trust 列表/撤销、engine autoaccept 运行时可变、IPC 扩展
- ui：ClipboardView/TrayPopup/SettingsView/KeymapView 重写或扩充
- 验证：单机双进程冒烟扩展（历史/清空/autoaccept 开关）+ 全部测试绿 + 协议层零改动
