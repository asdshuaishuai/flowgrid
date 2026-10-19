// 托盘常驻：图标 + 原生菜单（打开 / 退出）。窗口关闭 = 收进托盘，
// 退出只结束 UI 进程，fgcore 不受影响（specs/windows-ui-shell）。
using System.Drawing;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using WinForms = System.Windows.Forms;

namespace FlowGrid.UI;

public sealed class TrayIcon : IDisposable
{
    private readonly WinForms.NotifyIcon _icon;

    public TrayIcon()
    {
        _icon = new WinForms.NotifyIcon
        {
            Text = "FlowGrid",
            Icon = MakeIcon(),
            Visible = true,
        };
        var menu = new WinForms.ContextMenuStrip();
        menu.Items.Add("打开主面板", null, (_, _) => App.OpenPanel("connections"));
        menu.Items.Add(new WinForms.ToolStripSeparator());
        menu.Items.Add("退出", null, (_, _) =>
        {
            App.IsExiting = true;
            Application.Current.Shutdown();
        });
        _icon.ContextMenuStrip = menu;
        _icon.MouseClick += (_, e) =>
        {
            if (e.Button == WinForms.MouseButtons.Left)
            {
                TrayPopup.Toggle(); // 左键唤出/收起浮板（tray.html）
            }
        };
        _icon.DoubleClick += (_, _) => App.OpenPanel("connections");
    }

    // 运行时画一枚 32px 图标：强调色圆角底 + 2×2 白点（跨机流动的网格隐喻），
    // 免维护 .ico 资产；像素级图标待设计图标体系落地后替换。
    private static Icon MakeIcon()
    {
        const int size = 32;
        var visual = new DrawingVisual();
        using (var dc = visual.RenderOpen())
        {
            var accent = FindBrush("BrushAccent");
            var onAccent = FindBrush("BrushOnAccent");
            dc.DrawRoundedRectangle(accent, null, new Rect(0, 0, size, size), 7, 7);
            foreach (var p in new[] { (8, 8), (20, 8), (8, 20), (20, 20) })
            {
                dc.DrawEllipse(onAccent, null, new System.Windows.Point(p.Item1 + 2, p.Item2 + 2), 3, 3);
            }
        }
        var bmp = new RenderTargetBitmap(size, size, 96, 96, PixelFormats.Pbgra32);
        bmp.Render(visual);
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bmp));
        using var ms = new MemoryStream();
        encoder.Save(ms);
        using var tmp = new Bitmap(ms);
        return Icon.FromHandle(tmp.GetHicon());
    }

    private static System.Windows.Media.Brush FindBrush(string key) =>
        Application.Current.TryFindResource(key) as System.Windows.Media.Brush
        ?? System.Windows.Media.Brushes.DodgerBlue;

    public void Dispose()
    {
        _icon.Visible = false;
        _icon.Dispose();
    }
}
