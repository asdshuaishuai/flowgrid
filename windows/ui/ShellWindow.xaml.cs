using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;
using FlowGrid.UI.Views;

namespace FlowGrid.UI;

// 520 面板宿主：无主窗口形态下按需从托盘唤出，常驻隐藏（关闭=收起）。
// 标题栏文案/右侧元素随屏切换（对齐各屏 data-titlebar / data-right）。
public partial class ShellWindow : Window
{
    private static readonly Dictionary<string, (string Title, string Nav)> Screens = new()
    {
        ["connections"] = ("设备与连接", "connections"),
        ["clipboard"] = ("剪贴板", "clipboard"),
        ["files"] = ("文件传输", "files"),
        ["latency"] = ("网络延迟", "latency"),
        ["keymap"] = ("键位映射", "keymap"),
        ["pairing"] = ("添加设备", "pairing"),
        ["settings"] = ("设置", "settings"),
    };

    private readonly ConnectionsView _connectionsView = new();
    private readonly Dictionary<string, Func<UserControl>> _views;
    private DispatcherTimer? _toastTimer;
    private DispatcherTimer? _pillTimer;
    private string _currentKey = "connections";

    public ShellWindow()
    {
        InitializeComponent();

        _views = new Dictionary<string, Func<UserControl>>
        {
            ["connections"] = () => _connectionsView,
            ["clipboard"] = () => new ClipboardView(),
            ["files"] = () => new FilesView(),
            ["latency"] = () => new LatencyView(),
            ["keymap"] = () => new KeymapView(),
            ["pairing"] = () => new PairingView(),
            ["settings"] = () => new SettingsView(),
        };

        // tokens.css §0 --panel-max-h: calc(100vh - 48px)；WPF 无 calc，按工作区换算
        MaxHeight = SystemParameters.WorkArea.Height - 48;

        // 停留在文件/剪贴板屏时标题 pill 也要跟随数据刷新
        _pillTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _pillTimer.Tick += (_, _) => RefreshTitleRight(_currentKey);
        _pillTimer.Start();

        Navigate("connections");
    }

    public void Navigate(string key)
    {
        if (!_views.TryGetValue(key, out var make))
        {
            return;
        }
        _currentKey = key;
        ContentHost.Content = make();
        if (Screens.TryGetValue(key, out var meta))
        {
            ScreenTitle.Text = meta.Title;
            Title = meta.Title;
        }
        RefreshTitleRight(key);
    }

    /// <summary>面板入场动画（panelIn：opacity 0→1 + 内部 Border translateY/scale）。</summary>
    public void PlayEntrance()
    {
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
    }

    // data-right：剪贴板「同步中」、文件「N 项进行中」、延迟「最优延迟」。
    private void RefreshTitleRight(string key)
    {
        TitleRightHost.Content = key switch
        {
            "clipboard" => MakePill("PillOk", "BrushOkSolid", "同步中", "BrushOkText"),
            "files" => MakePill("PillOk", "BrushOkSolid",
                $"{App.LastTransferCount} 项进行中", "BrushOkText"),
            "latency" => MakePill("PillOk", "BrushOkSolid", "采样中", "BrushOkText"),
            _ => null,
        };
    }

    private static Border MakePill(string pillStyle, string dotBrush, string text, string textBrush)
    {
        var border = new Border { Style = (Style)Application.Current.Resources[pillStyle] };
        var stack = new StackPanel { Orientation = Orientation.Horizontal };
        var dot = new System.Windows.Shapes.Ellipse
        {
            Style = (Style)Application.Current.Resources["DotPulse"],
            Margin = new Thickness(0, 0, 6, 0), // ui.css .status-pill gap 6，间距阶梯无 6 档
            VerticalAlignment = VerticalAlignment.Center,
        };
        dot.SetResourceReference(System.Windows.Shapes.Ellipse.FillProperty, dotBrush);
        var label = new TextBlock { Text = text };
        label.SetResourceReference(TextBlock.FontSizeProperty, "Font11");
        label.SetResourceReference(TextBlock.ForegroundProperty, textBrush);
        stack.Children.Add(dot);
        stack.Children.Add(label);
        border.Child = stack;
        return border;
    }

    /// <summary>面板内 Toast（.toast：底部居中，1.8s 自动消失）。</summary>
    public void ShowToast(string message)
    {
        ToastText.Text = message;
        var fadeIn = new System.Windows.Media.Animation.DoubleAnimation(1, TimeSpan.FromMilliseconds(300));
        Toast.BeginAnimation(OpacityProperty, fadeIn);
        _toastTimer ??= new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(1800) };
        _toastTimer.Stop();
        _toastTimer.Tick += (_, _) =>
        {
            _toastTimer.Stop();
            var fadeOut = new System.Windows.Media.Animation.DoubleAnimation(0, TimeSpan.FromMilliseconds(300));
            Toast.BeginAnimation(OpacityProperty, fadeOut);
        };
        _toastTimer.Start();
    }

    private void OnCloseClick(object sender, RoutedEventArgs e)
    {
        // 收进托盘；真正的退出在托盘浮板/菜单。
        Hide();
    }

    protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
    {
        if (!App.IsExiting)
        {
            e.Cancel = true;
            Hide();
        }
        base.OnClosing(e);
    }
}
