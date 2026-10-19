# FlowGrid Windows 客户端

目标三层形态与决策见 `openspec/changes/windows-client/design.md`：
SYSTEM 服务 fgcored → 会话核心 fgcore → WPF UI。
当前：fgcore（mDNS 发现/自通告 + DirectLink TCP/TLS 连接引擎 + 命名管道 IPC）
+ FlowGrid.UI（设计令牌壳 + 7 屏，连接屏/配对屏接线 live）。

## 结构

```
windows/
├── cmd/fgcore/      会话核心进程入口
├── internal/core/   设备表 + mDNS 桥接 + IPC（契约：specs/windows-ipc）
├── internal/link/   连接引擎：TLS 身份/信任库/IDENTIFY/心跳/重连（specs/windows-link）
├── ui/
│   ├── Themes/      DesignTokens（形态令牌）+ ThemeLight/Dark（颜色层）
│   ├── Views/       连接屏（live）+ 配对/设置 + 占位屏
│   ├── ShellWindow  520 面板 + 34 标题栏 + 44 导航轨
│   ├── ThemeManager 跟随系统/浅/深，%APPDATA%\FlowGrid\ui.json 持久化
│   └── TrayIcon     托盘常驻（打开/退出；窗口关闭=收进托盘）
├── smoke.ps1        IPC 端到端冒烟（ASCII-only，见下「已知坑」）
└── smoke-link.ps1   双进程连接冒烟（证书导入→连接→在线→断开）
```

## 运行

```bash
cd windows
go build -o fgcore.exe ./cmd/fgcore
fgcore.exe                      # -name -port -data -auto-accept -peer-cert -peer-name
dotnet run --project ui
```

**无主窗口**：UI 启动即托盘常驻——托盘左键唤出 300px 浮板（快速切换/入口/
开关/退出），双击或菜单打开 520 主面板（按需从浮板跳转各屏）。连接屏显示
mDNS 状态与发现的设备；对已配对设备点「连接」即发起 DirectLink TCP/TLS
连接（IDENTIFY → 心跳 → 在线，延迟实时显示）。入站未信任对端在配对屏出
裁决卡。

## 两台真机配对

首次配对的证书分发尚无规范层方案（PROTOCOL-ISSUES P3），当前带外引导：
把 A 的 `core.pem` 拷给 B，B 启动加
`-peer-cert a.pem -peer-name <A 的设备名>`；反向同理（或由已配对一侧
入站 + 用户确认完成登记）。

## 验证

```bash
go test ./...
dotnet build ui
powershell -NoProfile -ExecutionPolicy Bypass -File smoke.ps1        # 先起 fgcore
powershell -NoProfile -ExecutionPolicy Bypass -File smoke-link.ps1   # 双进程连接链路
```

## 已知坑

- **winio 零配额管道**：`winio.ListenPipe(path, nil)` 建出的管道配额为 0，
  对端没有 pending read 时写入会永久阻塞。必须走 `core.ListenPipe`
  （显式 64KB 缓冲），且出站写一律经队列，不许同步写。
- **PowerShell 5.1 编码**：无 BOM 的 .ps1 按 GBK 解析，中文会炸——脚本保持
  ASCII（smoke*.ps1 已如此）。
- **同机双进程 mDNS 环回不可靠**：advertiser 按查询源单播应答，同端口另一
  套接字可能截走应答；同机联调用 `devices.add` IPC 注入地址，真双机无此问题。
- **首次配对引导**：拨号方需信任库里有对端证书（固定根钉扎，零校验旁路），
  带外分发见上；规范层解法待协议修订（P3）。
- **心跳不回帧**：双侧各自 2s 周期发送、收帧只喂狗——「收到即回」会产生
  1:1 回显风暴（P4）。
- **环回延迟可为 0µs**：判断「有无延迟数据」要看快照 samples 计数，不能看
  数值 > 0。
- **go vet**：windows 模块用 `go vet -unsafeptr=false ./...`——LL 钩子
  lparam 与 GlobalLock 句柄必须做 uintptr→Pointer 转换（Windows 平台面
  无法避免；结构体/内存生命周期由系统保证），已在代码内 SAFETY 注释。
- go-winio 钉在 v0.6.2：v0.6.4 要求 go ≥ 1.26，仓库工具链是 1.25。
