using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

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
            var online = links.Where(l => l.State == "online" && l.LatencyMs > 0).ToList();

            // 推进滚动窗口
            if (online.Count > 0)
            {
                Array.Copy(_samples, 1, _samples, 0, 11);
                _samples[11] = online[0].LatencyMs;
            }
            Chart.SetSamples(_samples);

            // 大号数字 + 质量颜色
            var latest = online.Count > 0 ? online[0].LatencyMs : 0;
            BigLatency.Text = latest > 0 ? $"{latest:0.#}ms" : "—";
            BigLatency.SetResourceReference(TextBlock.ForegroundProperty,
                latest < 2 ? "BrushOkText" : latest < 4 ? "BrushWarnText" : "BrushDangerText");

            // 均值徽章
            if (online.Count > 0)
            {
                ChartBadge.Text = $"均值 {online.Average(l => l.LatencyMs):0.#}ms";
            }

            DeviceList.ItemsSource = online;
        });
    }
}
