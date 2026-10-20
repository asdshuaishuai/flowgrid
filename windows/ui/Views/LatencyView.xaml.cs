using System.Windows;
using System.Windows.Controls;

namespace FlowGrid.UI.Views;

public partial class LatencyView : UserControl
{
    private readonly double?[] _samples = new double?[12];

    public LatencyView()
    {
        InitializeComponent();
        Loaded += (_, _) => App.Core.LinksReceived += OnLinks;
        Unloaded += (_, _) => App.Core.LinksReceived -= OnLinks;
    }

    private void OnLinks(IReadOnlyList<LinkInfo> links)
    {
        Dispatcher.Invoke(() =>
        {
            var online = links.Where(l => l.State == "online" && l.LatencyMs > 0)
                              .OrderBy(l => l.LatencyMs)
                              .ToList();

            // 推进滚动窗口（取最优链路）
            if (online.Count > 0)
            {
                Array.Copy(_samples, 1, _samples, 0, 11);
                _samples[11] = online[0].LatencyMs;
            }
            Chart.SetSamples(_samples);

            // 大号数字：无数据/离线用 idle 色，不再初始绿/兜底红；
            // 三档阈值对齐 app.js:290（<8 good / <20 warn / 否则 bad）。
            var latest = online.Count > 0 ? online[0].LatencyMs : (double?)null;
            BigLatency.Text = latest.HasValue ? $"{latest:0.#}ms" : "—";
            BigLatency.SetResourceReference(TextBlock.ForegroundProperty,
                !latest.HasValue ? "BrushIdleText"
                : latest < 8 ? "BrushOkText"
                : latest < 20 ? "BrushWarnText"
                : "BrushDangerText");

            ChartBadge.Text = online.Count > 0 ? $"均值 {online.Average(l => l.LatencyMs):0.#}ms" : "均值 —";

            // 分设备行：离线设备也显示（idle 行），不再按 online 过滤。
            // hint 只用 LinkInfo 实际有的字段（addr / min-均-max 统计；无丢包字段）。
            DeviceList.ItemsSource = links
                .OrderByDescending(l => l.State == "online")
                .Select(l => new LatencyRow(
                    Name: l.Name,
                    Hint: l.State == "online"
                        ? $"{l.Addr} · min {l.LatMinMs:0.#} / 均 {l.LatAvgMs:0.#} / max {l.LatMaxMs:0.#} ms"
                        : "离线",
                    Tier: l.State == "online"
                        ? (l.LatencyMs < 8 ? "good" : l.LatencyMs < 20 ? "warn" : "bad")
                        : "idle",
                    Text: l.LatencyText))
                .ToList();
            NoLinks.Visibility = links.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        });
    }

    private void OnCloseClick(object sender, RoutedEventArgs e) => Window.GetWindow(this)?.Hide();

    private sealed record LatencyRow(string Name, string Hint, string Tier, string Text);
}
