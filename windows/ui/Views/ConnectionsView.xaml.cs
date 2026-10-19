using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace FlowGrid.UI.Views;

public partial class ConnectionsView : UserControl
{
    public ConnectionsView()
    {
        InitializeComponent();
        Loaded += (_, _) =>
        {
            App.Core.StateChanged += OnStateChanged;
            App.Core.StatusReceived += OnStatusReceived;
            App.Core.DevicesReceived += OnDevices;
            App.Core.LinksReceived += OnLinks;
        };
        Unloaded += (_, _) =>
        {
            App.Core.StateChanged -= OnStateChanged;
            App.Core.StatusReceived -= OnStatusReceived;
            App.Core.DevicesReceived -= OnDevices;
            App.Core.LinksReceived -= OnLinks;
        };
    }

    private void OnStateChanged(string state)
    {
        var (pill, dot, text, label) = state switch
        {
            "online" => ("PillOk", "BrushOkSolid", "BrushOkText", "已连接 fgcore"),
            "connecting" => ("PillWarn", "BrushWarnSolid", "BrushWarnText", "连接核心中…"),
            _ => ("PillIdle", "BrushIdleSolid", "BrushIdleText", "核心未运行"),
        };
        StatusPill.Style = (Style)FindResource(pill);
        StatusDot.Fill = (Brush)FindResource(dot);
        StatusText.SetResourceReference(TextBlock.ForegroundProperty, text);
        StatusText.Text = label;
    }

    private void OnStatusReceived(CoreStatus s)
    {
        if (s.Mdns)
        {
            StatusText.Text = $"已连接 fgcore {s.Version} · mDNS 运行中 · {s.Peers} 台设备";
        }
        else
        {
            StatusText.Text = $"已连接 fgcore {s.Version} · mDNS 不可用";
        }
    }

    private void OnDevices(IReadOnlyList<PeerInfo> peers)
    {
        Devices.ItemsSource = peers;
        NoDevices.Visibility = peers.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
    }

    private readonly double?[] _latSamples = new double?[12];

    private void OnLinks(IReadOnlyList<LinkInfo> links)
    {
        Links.ItemsSource = links;
        // 用最新在线对端的延迟推进滚动窗口（12 柱图数据源）。
        var online = links.FirstOrDefault(l => l.State == "online" && l.LatencyMs > 0);
        if (online != null)
        {
            Array.Copy(_latSamples, 1, _latSamples, 0, 11);
            _latSamples[11] = online.LatencyMs;
        }
        Chart.SetSamples(_latSamples);
        var best = links.Where(l => l.State == "online" && l.LatencyMs > 0)
            .Select(l => l.LatencyMs).DefaultIfEmpty(0).Min();
        LatBadge.Text = best > 0 ? $"均值 {best:0.#}ms" : "均值 —";
    }

    private async void OnConnectClick(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { Tag: string instance })
        {
            try
            {
                await App.Core.ConnectPeerAsync(instance);
            }
            catch (Exception)
            {
                // 核心未连接：状态 pill 已如实反映，无需弹错。
            }
        }
    }

    private async void OnDisconnectClick(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { Tag: string instance })
        {
            try
            {
                await App.Core.DisconnectPeerAsync(instance);
            }
            catch (Exception)
            {
            }
        }
    }

    private void OnAddDeviceClick(object sender, RoutedEventArgs e)
    {
        (Window.GetWindow(this) as ShellWindow)?.Navigate("pairing");
    }

    private void OnEditKeymapClick(object sender, RoutedEventArgs e)
    {
        (Window.GetWindow(this) as ShellWindow)?.Navigate("keymap");
    }
}
