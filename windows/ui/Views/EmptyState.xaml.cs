using System.Windows;
using System.Windows.Controls;

namespace FlowGrid.UI.Views;

public partial class EmptyState : UserControl
{
    public static readonly DependencyProperty TitleProperty = DependencyProperty.Register(
        nameof(Title), typeof(string), typeof(EmptyState),
        new PropertyMetadata(string.Empty, (d, e) => ((EmptyState)d).TitleHost.Text = (string)e.NewValue));

    public static readonly DependencyProperty DescriptionProperty = DependencyProperty.Register(
        nameof(Description), typeof(string), typeof(EmptyState),
        new PropertyMetadata(string.Empty, (d, e) => ((EmptyState)d).DescriptionHost.Text = (string)e.NewValue));

    public static readonly DependencyProperty GlyphProperty = DependencyProperty.Register(
        nameof(Glyph), typeof(string), typeof(EmptyState),
        new PropertyMetadata("◎", (d, e) => ((EmptyState)d).GlyphHost.Text = (string)e.NewValue));

    public EmptyState()
    {
        InitializeComponent();
    }

    public string Title
    {
        get => (string)GetValue(TitleProperty);
        set => SetValue(TitleProperty, value);
    }

    public string Description
    {
        get => (string)GetValue(DescriptionProperty);
        set => SetValue(DescriptionProperty, value);
    }

    public string Glyph
    {
        get => (string)GetValue(GlyphProperty);
        set => SetValue(GlyphProperty, value);
    }
}
