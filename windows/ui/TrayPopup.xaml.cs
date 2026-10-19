using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace FlowGrid.UI;

// 托盘浮板（tray.html）：300px，托盘左键唤出，失焦隐藏。
// 行内容在 Refresh 时按最新快照重建（在线对端/计数/延迟实时）。
public partial class TrayPopup : Window
{
    private static TrayPopup? _instance;

    public static void Toggle()
    {
        if (_instance == null)
        {
            _instance = new TrayPopup();
        }
        if (_instance.IsVisible)
        {
            _instance.Hide();
            return;
        }
        _instance.RefreshAndShow();
    }

    public TrayPopup()
    {
        InitializeComponent();
        Deactivated += (_, _) => Hide();
        Loaded += (_, _) =>
        {
            App.Core.StateChanged += _ => Dispatcher.Invoke(RefreshAll);
            App.Core.LinksReceived += _ => Dispatcher.Invoke(RefreshAll);
            App.Core.InputChanged += _ => Dispatcher.Invoke(RefreshAll);
            App.Core.ClipboardHistory += _ => Dispatcher.Invoke(RefreshAll);
            App.Core.FileProgress += _ => Dispatcher.Invoke(RefreshAll);
        };
    }

    private void RefreshAndShow()
    {
        var area = SystemParameters.WorkArea;
        // 右下角贴托盘（Windows 惯例：Flyout 出现在系统托盘附近）。
        Left = area.Right - Width - 8;
        RefreshAll();
        Show();
        Top = area.Bottom - ActualHeight - 12;
        // trayIn：opacity 淡入（220ms ease-out）。变换动画挂在内层 Border 上。
        Opacity = 0;
        var dur = TimeSpan.FromMilliseconds(220);
        BeginAnimation(OpacityProperty, new DoubleAnimation(1, dur)
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut },
        });
        if (FindName("RootBorder") is Border root)
        {
            var ty = new TranslateTransform(0, -8);
            var sc = new ScaleTransform(0.97, 0.97);
            var group = new TransformGroup();
            group.Children.Add(ty);
            group.Children.Add(sc);
            root.RenderTransform = group;
            var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
            ty.BeginAnimation(TranslateTransform.YProperty,
                new DoubleAnimation(0, dur) { EasingFunction = ease });
            sc.BeginAnimation(ScaleTransform.ScaleXProperty,
                new DoubleAnimation(1, dur) { EasingFunction = ease });
            sc.BeginAnimation(ScaleTransform.ScaleYProperty,
                new DoubleAnimation(1, dur) { EasingFunction = ease });
        }
        Activate();
    }

    private void RefreshAll()
    {
        RefreshHead();
        RefreshSelfCard();
        RefreshSwitchRow();
        RefreshLinkRows();
        RefreshDataRows();
        RefreshClipSync();
    }

    private void RefreshHead()
    {
        var online = App.LastLinks.Where(l => l.State == "online").ToList();
        if (App.CoreLinkState != "online")
        {
            HeadPill.Style = (Style)FindResource("PillIdle");
            HeadPillText.SetResourceReference(TextBlock.ForegroundProperty, "BrushIdleText");
            HeadPillText.Text = "核心未连接";
            return;
        }
        if (online.Count > 0)
        {
            var best = online.Min(l => l.LatencyMs);
            HeadPillText.Text = $"{best:0.#}ms · {online.Count} 台";
        }
        else
        {
            HeadPillText.Text = "已连接 · 无设备";
        }
    }

    private void RefreshSelfCard()
    {
        var hosting = App.LastHosting;
        if (hosting == "")
        {
            SelfName.Text = Environment.MachineName;
            SelfHint.Text = "本机键鼠直通";
        }
        else
        {
            SelfName.Text = hosting;
            SelfHint.Text = "Ctrl+Alt+0 切换回来";
        }
        SelfInitials.Text = InitialsOf(SelfName.Text ?? "");
    }

    private void RefreshSwitchRow()
    {
        SwitchHost.Children.Clear();
        AddSwitchButton("本机", 0, Hosting() == "");
        var index = 1;
        foreach (var name in App.LastLinks.Where(l => l.State == "online")
                     .Select(l => l.Name).OrderBy(n => n))
        {
            AddSwitchButton(name, index, Hosting() == name);
            index++;
        }
    }

    private void AddSwitchButton(string label, int index, bool active)
    {
        var btn = new Button
        {
            Content = active ? $"● {label}" : label,
            Style = (Style)FindResource(active ? "BtnPrimary" : "BtnSecondary"),
            Height = 28,
            Margin = new Thickness(0, 0, 0, 4),
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        btn.Click += async (_, _) =>
        {
            try
            {
                await App.Core.SwitchToAsync(index);
                RefreshAll();
            }
            catch (System.IO.IOException)
            {
            }
        };
        SwitchHost.Children.Add(btn);
    }

    private void RefreshLinkRows()
    {
        LinkRows.Children.Clear();
        AddRow(LinkRows, "设备与连接", $"{OnlineCount()} 台", () => App.OpenPanel("connections"));
        var best = App.LastLinks.Where(l => l.State == "online")
            .Select(l => l.LatencyMs).DefaultIfEmpty(-1).Min();
        AddRow(LinkRows, "网络延迟", best < 0 ? "—" : $"{best:0.#}ms", () => App.OpenPanel("latency"));
        AddRow(LinkRows, "添加设备", "向导", () => App.OpenPanel("pairing"));
    }

    private void RefreshDataRows()
    {
        DataRows.Children.Clear();
        AddRow(DataRows, "剪贴板", $"{App.LastClipboardCount} 项", () => App.OpenPanel("clipboard"));
        AddRow(DataRows, "文件传输", $"{App.LastTransferCount} 项", () => App.OpenPanel("files"));
    }

    private void AddRow(StackPanel host, string label, string trailing, Action open)
    {
        var grid = new Grid { Height = 28 };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

        var btn = new Button
        {
            Style = (Style)FindResource("TrayItem"),
            HorizontalAlignment = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
        };
        var inner = new Grid();
        inner.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        inner.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var text = new TextBlock { Text = label };
        text.SetResourceReference(TextBlock.ForegroundProperty, "BrushText1");
        inner.Children.Add(text);
        var trail = new TextBlock { Text = trailing };
        trail.SetResourceReference(TextBlock.ForegroundProperty, "BrushText3");
        trail.SetValue(Grid.ColumnProperty, 1);
        inner.Children.Add(trail);
        btn.Content = inner;

        btn.Click += (_, _) =>
        {
            HideInstance();
            open();
        };
        grid.Children.Add(btn);
        host.Children.Add(grid);
    }

    private int OnlineCount() => App.LastLinks.Count(l => l.State == "online");

    private string Hosting() => App.LastHosting ?? "";

    private static string InitialsOf(string name)
    {
        if (string.IsNullOrEmpty(name))
        {
            return "FG";
        }
        return name.Length <= 2 ? name.ToUpperInvariant() : name[..2].ToUpperInvariant();
    }

    private async void OnClipSyncToggle(object sender, RoutedEventArgs e)
    {
        if (_suppress)
        {
            return;
        }
        try
        {
            await App.Core.SetClipSyncAsync(ClipSyncToggle.IsChecked == true);
        }
        catch (System.IO.IOException)
        {
        }
    }

    private bool _suppress;

    private void RefreshClipSync()
    {
        _suppress = true;
        ClipSyncToggle.IsChecked = App.LastClipSync;
        _suppress = false;
    }

    private void OnOpenSettingsClick(object sender, RoutedEventArgs e) => OpenPanelAndHide("settings");

    private void OnCloseClick(object sender, RoutedEventArgs e) => Hide();

    private async void OnQuitClick(object sender, RoutedEventArgs e)
    {
        HideInstance();
        App.IsExiting = true;
        try
        {
            await App.Core.DisconnectAllAsync();
        }
        catch (System.IO.IOException)
        {
        }
        Application.Current.Shutdown();
    }

    private static void OpenPanelAndHide(string destination)
    {
        HideInstance();
        App.OpenPanel(destination);
    }

    private static void HideInstance() => _instance?.Hide();
}
