## 1. OpenSpec

- [x] 1.1 本变更四件套

## 2. fgcore

- [x] 2.1 hub：剪贴板历史环形存储（容量可调）+ 同步开关 + 回声抑制保留
- [x] 2.2 trust：条目列表 + 撤销；engine：autoaccept 运行时可变
- [x] 2.3 IPC：clipboard.history/entry/clear/cap/enable、config.autoaccept、trust.list/revoke

## 3. UI

- [x] 3.1 剪贴板屏：预览卡 + 过滤搜索 + 历史列表 + 同步设置 + 清空
- [x] 3.2 托盘浮板：300px 浮窗（状态/本机/快速切换/入口/开关/退出），托盘左键唤出
- [x] 3.3 设置屏：连接/输入/剪贴板/安全四组（自启注册表、autoaccept 开关、设备管理）
- [x] 3.4 键位屏：默认映射行 + 规则状态
- [x] 3.5 无主窗口形态：ShutdownMode=OnExplicitShutdown，托盘常驻，面板按需唤出

## 4. 验证

- [x] 4.1 单机双进程冒烟：历史入账/读回/清空/autoaccept 开关生效
- [x] 4.2 全部测试绿 + dotnet build 零警告 + UI 运行时冒烟 + 协议层零改动
