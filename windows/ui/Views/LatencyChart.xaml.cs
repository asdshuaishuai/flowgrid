using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Animation;

namespace FlowGrid.UI.Views;

// .chart：12 柱延迟图（70px 高，顶部药丸圆角柱 + 最近一柱 accent，500ms 高度过渡）。
public partial class LatencyChart : UserControl
{
    private const int BarCount = 12;

    // 固定刻度：0..50ms 线性映射 6..66px。app.js 的柱高是随机演示数据（20..46px），
    // 固定域替代旧的 max 归一化，避免单次毛刺把其余柱压扁。
    private const double MinBarHeight = 6;
    private const double MaxBarHeight = 66;
    private const double ScaleMaxMs = 50;

    private static readonly Duration BarDuration = new(TimeSpan.FromMilliseconds(500));

    private readonly Border[] _bars = new Border[BarCount];

    public LatencyChart()
    {
        var grid = new Grid
        {
            Height = (double)Application.Current.Resources["ChartHeight"],
            VerticalAlignment = VerticalAlignment.Bottom,
        };
        for (var i = 0; i < BarCount; i++)
        {
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            var bar = new Border
            {
                // ui.css .chart__bar：r-pill r-pill 0 0 —— 只圆顶部
                CornerRadius = new CornerRadius(3, 3, 0, 0),
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(2, 0, 2, 0),
                Height = MinBarHeight,
            };
            // 资源引用而非缓存画刷实例，主题切换自动跟随
            bar.SetResourceReference(Border.BackgroundProperty,
                i == BarCount - 1 ? "BrushAccent" : "BrushAccentSoft");
            Grid.SetColumn(bar, i);
            grid.Children.Add(bar);
            _bars[i] = bar;
        }

        Content = grid;
    }

    // SetSamples 按延迟毫秒值刷新柱高（固定刻度线性映射，500ms 过渡）。
    public void SetSamples(double?[] values)
    {
        for (var i = 0; i < BarCount; i++)
        {
            var ms = i < values.Length && values[i].HasValue
                ? Math.Clamp(values[i].GetValueOrDefault(), 0, ScaleMaxMs)
                : 0;
            var height = MinBarHeight + ms / ScaleMaxMs * (MaxBarHeight - MinBarHeight);
            _bars[i].BeginAnimation(FrameworkElement.HeightProperty, new DoubleAnimation(height, BarDuration)
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut },
            });
        }
    }
}
