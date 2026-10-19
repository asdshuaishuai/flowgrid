using System.Windows;
using System.Windows.Controls;

namespace FlowGrid.UI.Views;

/// <summary>骨架屏通用形态：标题 + 空态 + 启用说明。文本全部经构造注入。</summary>
public partial class PlaceholderView : UserControl
{
    public PlaceholderView(string title, string emptyTitle, string emptyDescription, string glyph = "◎")
    {
        InitializeComponent();
        TitleHost.Text = title;
        Empty.Title = emptyTitle;
        Empty.Description = emptyDescription;
        Empty.Glyph = glyph;
    }
}
