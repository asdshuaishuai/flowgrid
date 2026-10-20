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
            App.Core.KeymapStatusReceived += OnKeymapStatus;
        };
        Unloaded += (_, _) =>
        {
            App.Core.StateChanged -= OnStateChanged;
            App.Core.StatusReceived -= OnStatusReceived;
            App.Core.DevicesReceived -= OnDevices;
            App.Core.LinksReceived -= OnLinks;
            App.Core.KeymapStatusReceived -= OnKeymapStatus;
        };
    }

    // CoreLink 事件在后台线程触发；所有 UI 改动经 Dispatcher 回 UI 线程。
    private void OnStateChanged(string state) => Dispatcher.BeginInvoke(() => ApplyCoreState(state));
    private void OnStatusReceived(CoreStatus s) => Dispatcher.BeginInvoke(() => ApplyCoreStatus(s));
    private void OnDevices(IReadOnlyList<PeerInfo> peers) => Dispatcher.BeginInvoke(() => ApplyDevices(peers));
    private void OnLinks(IReadOnlyList<LinkInfo> links) => Dispatcher.BeginInvoke(() => ApplyLinks(links));
    private void OnKeymapStatus(KeymapStatusDto km) => Dispatcher.BeginInvoke(() => ApplyKeymapStatus(km));

    private void ApplyCoreState(string state)
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

    private void ApplyCoreStatus(CoreStatus s)
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

    private void ApplyDevices(IReadOnlyList<PeerInfo> peers)
    {
        Devices.ItemsSource = peers;
        NoDevices.Visibility = peers.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
    }

    private readonly double?[] _latSamples = new double?[12];

    private void ApplyLinks(IReadOnlyList<LinkInfo> links)
    {
        // CN1/CN2：原始 LinkInfo → 界面投影（中文状态 / 四档芯片 / 行内按钮可见性）
        Links.ItemsSource = links.Select(l => new LinkRow(l)).ToList();
        // 用最新在线对端的延迟推进滚动窗口（12 柱图数据源）。
        var online = links.FirstOrDefault(l => l.State == "online" && l.LatencyMs > 0);
        if (online != null)
        {
            Array.Copy(_latSamples, 1, _latSamples, 0, 11);
            _latSamples[11] = online.LatencyMs;
        }
        Chart.SetSamples(_latSamples);
        // U6：badge 是「均值」语义，取在线链路各自的平均延迟再平均，不是最小值。
        var means = links.Where(l => l.State == "online" && l.LatAvgMs > 0)
            .Select(l => l.LatAvgMs).ToList();
        var avg = means.Count > 0 ? means.Average() : 0;
        LatBadge.Text = avg > 0 ? $"均值 {avg:0.#}ms" : "均值 —";
    }

    // CN13：「N 条生效」改绑 keymap.status 实时数据，替换硬编码演示值。
    private void ApplyKeymapStatus(KeymapStatusDto km)
    {
        KeymapBadge.Text = $"{km.Rules} 条生效";
    }

    private async void OnConnectClick(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { Tag: PeerInfo peer })
        {
            await ConnectCoreAsync(peer.Instance, $"正在连接 {peer.Name}");
        }
        else if (sender is FrameworkElement { Tag: LinkRow row })
        {
            // 文案对齐 connections.html:73 重连钮 data-msg「正在重连 …」
            await ConnectCoreAsync(row.Instance, $"正在重连 {row.Name}");
        }
    }

    private async Task ConnectCoreAsync(string instance, string toast)
    {
        try
        {
            await App.Core.ConnectPeerAsync(instance);
            (Window.GetWindow(this) as ShellWindow)?.ShowToast(toast);
        }
        catch (Exception)
        {
            // 核心未连接：状态 pill 已如实反映，无需弹错。
        }
    }

    private async void OnDisconnectClick(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { Tag: LinkRow row })
        {
            try
            {
                await App.Core.DisconnectPeerAsync(row.Instance);
                (Window.GetWindow(this) as ShellWindow)?.ShowToast($"已断开 {row.Name}");
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

    // CN7：延迟折叠区页脚「详情」→ 延迟屏。
    private void OnLatencyDetailClick(object sender, RoutedEventArgs e)
    {
        (Window.GetWindow(this) as ShellWindow)?.Navigate("latency");
    }

    /// <summary>
    /// 链接行投影（CN1/CN2/CN5）：把 LinkInfo 原始字段翻成绑定语义。
    /// 档位阈值与 app.js:290 一致：&lt;8ms good、&lt;20ms warn、其余 bad；非在线一律 idle「—」。
    /// </summary>
    private sealed class LinkRow(LinkInfo link)
    {
        public string Instance { get; } = link.Instance;
        public string Name { get; } = link.Name;
        public bool IsOnline { get; } = link.State == "online";
        public bool IsOffline { get; } = link.State == "offline";
        // windows-link spec：连接中/在线/重连/离线
        public string StateText { get; } = link.State switch
        {
            "online" => "在线",
            "reconnecting" => "重连",
            "connecting" => "连接中",
            _ => "离线",
        };
        public string LatencyText { get; } = link.State == "online" ? $"{link.LatencyMs:0.#}ms" : "—";
        public string LatTier { get; } = link.State != "online" ? "idle"
            : link.LatencyMs < 8 ? "good" : link.LatencyMs < 20 ? "warn" : "bad";
    }
}
