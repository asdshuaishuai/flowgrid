## 1. OpenSpec

- [x] 1.1 本变更四件套

## 2. 令牌与主题

- [x] 2.1 DesignTokens.xaml：§0 窗口几何 / §2 间距 / §3 圆角 / §4 排版 / §5 组件尺寸
- [x] 2.2 ThemeLight/ThemeDark.xaml：L2 语义色全量 + L3 Windows 强调色
- [x] 2.3 ThemeManager：跟随系统（注册表）/ 浅 / 深，运行时切换，%APPDATA% 持久化

## 3. 壳

- [x] 3.1 ShellWindow：520 面板 + 34 自绘标题栏（拖拽/关闭）+ 44 导航轨 + 内容切换
- [x] 3.2 Controls.xaml：status-pill / btn / badge / device 卡 / section 折叠 / empty / 滚动条
- [x] 3.3 TrayIcon：NotifyIcon + 打开/退出菜单

## 4. 屏

- [x] 4.1 连接屏：核心状态 pill、设备卡（live）、空态、键位/延迟折叠区骨架
- [x] 4.2 其余 6 屏空态骨架（剪贴板/文件/延迟/键位/配对/设置）
- [x] 4.3 设置屏：主题三档切换并持久化

## 5. 验证与整备

- [x] 5.1 dotnet build 零警告；协议目录 git diff 为空；go test ./... 仍绿
- [x] 5.2 windows/README 与 windows-ui-shell 关联更新
