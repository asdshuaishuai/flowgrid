// 设计稿 SVG 图标子集（24 网格 / 1.75 描边）：Geometry 资源在 Icons.xaml，
// 本控件按继承的 Foreground 描边渲染，Size 即显示边长。
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;

namespace FlowGrid.UI.Views;

public class IconGlyph : FrameworkElement
{
    public static readonly DependencyProperty DataProperty = DependencyProperty.Register(
        nameof(Data), typeof(Geometry), typeof(IconGlyph),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty SizeProperty = DependencyProperty.Register(
        nameof(Size), typeof(double), typeof(IconGlyph),
        new FrameworkPropertyMetadata(16.0, FrameworkPropertyMetadataOptions.AffectsMeasure));

    public Geometry Data
    {
        get => (Geometry)GetValue(DataProperty);
        set => SetValue(DataProperty, value);
    }

    public double Size
    {
        get => (double)GetValue(SizeProperty);
        set => SetValue(SizeProperty, value);
    }

    static IconGlyph()
    {
        // Foreground 继承（Control.Foreground 与 TextElement.Foreground 同源）。
    }

    protected override System.Windows.Size MeasureOverride(System.Windows.Size availableSize)
        => new(Size, Size);
    protected override void OnRender(DrawingContext dc)
    {
        if (Data == null)
        {
            return;
        }
        var brush = (Brush)GetValue(TextElement.ForegroundProperty) ?? System.Windows.Media.Brushes.Gray;
        var pen = new System.Windows.Media.Pen(brush, 1.75)
        {
            StartLineCap = PenLineCap.Round,
            EndLineCap = PenLineCap.Round,
            LineJoin = PenLineJoin.Round,
        };
        var scale = Size / 24.0;
        dc.PushTransform(new ScaleTransform(scale, scale));
        dc.DrawGeometry(null, pen, Data);
        dc.Pop();
    }
}
