using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Shapes;

namespace FlowGrid.UI.Views;

// .chart：12 柱延迟图（70px 高，accent-soft 柱 + 最近一柱 accent，500ms 高度过渡）。
public partial class LatencyChart : UserControl
{
    private static readonly int BarCount = 12;
    private readonly Rectangle[] _bars = new Rectangle[12];

    public LatencyChart()
    {
        var grid = new Grid { Height = 70, VerticalAlignment = VerticalAlignment.Bottom };
        for (var i = 0; i < BarCount; i++)
        {
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            var bar = new Rectangle
            {
                RadiusX = 10,
                RadiusY = 10,
                Fill = FindBrush(i == BarCount - 1 ? "BrushAccent" : "BrushAccentSoft"),
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(2, 0, 2, 0),
            };
            Grid.SetColumn(bar, i);
            grid.Children.Add(bar);
            _bars[i] = bar;
        }

        Content = grid;
    }

    // SetSamples 按延迟毫秒值刷新柱高（值域归一化到 4..66px）。
    public void SetSamples(double?[] values)
    {
        var max = 1.0;
        for (var i = 0; i < BarCount; i++)
        {
            if (i < values.Length && values[i].HasValue && values[i].GetValueOrDefault() > max)
            {
                max = values[i].GetValueOrDefault();
            }
        }
        for (var i = 0; i < BarCount; i++)
        {
            if (i < values.Length && values[i].HasValue)
            {
                _bars[i].Height = 4.0 + (values[i].GetValueOrDefault() / max) * 62.0;
            }
            else
            {
                _bars[i].Height = 4;
            }
        }
    }

    private static Brush FindBrush(string key) =>
        Application.Current.TryFindResource(key) as Brush ?? System.Windows.Media.Brushes.Transparent;
}
