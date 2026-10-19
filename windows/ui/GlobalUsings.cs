// UseWindowsForms（托盘 NotifyIcon 互操作）开启后，SDK 会注入 WinForms 的隐式
// using，与 WPF 同名类型冲突；用全局别名把常用名钉在 WPF 侧。
global using Application = System.Windows.Application;
global using UserControl = System.Windows.Controls.UserControl;
global using Panel = System.Windows.Controls.Panel;
global using RadioButton = System.Windows.Controls.RadioButton;
global using Button = System.Windows.Controls.Button;
global using Orientation = System.Windows.Controls.Orientation;
global using Rectangle = System.Windows.Shapes.Rectangle;
global using Brush = System.Windows.Media.Brush;
global using HorizontalAlignment = System.Windows.HorizontalAlignment;
global using OpenFileDialog = Microsoft.Win32.OpenFileDialog;
global using MessageBox = System.Windows.MessageBox;
