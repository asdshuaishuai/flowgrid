using System.Windows;
using System.Windows.Controls;

namespace FlowGrid.UI.Views;

public partial class KeymapView : UserControl
{
    public KeymapView()
    {
        InitializeComponent();
        Loaded += (_, _) => App.Core.KeymapStatusReceived += OnKeymap;
        Unloaded += (_, _) => App.Core.KeymapStatusReceived -= OnKeymap;
    }

    private void OnKeymap(KeymapStatusDto status)
    {
        Dispatcher.Invoke(() =>
        {
            if (status.Loaded)
            {
                Empty.Visibility = Visibility.Collapsed;
                RulesState.Text = $"已加载（{status.Rules} 条）";
                RulesPath.Text = status.Path;
            }
            else
            {
                Empty.Visibility = Visibility.Visible;
                RulesState.Text = "未加载";
                RulesPath.Text = status.Path;
            }
        });
    }
}
