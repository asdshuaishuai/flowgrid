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
            // 头部/全局徽章消费同一条实时数据；规则文件路径收进全局折叠区底部。
            RulesBadge.Text = status.Loaded ? $"{status.Rules} 条生效" : "未加载";
            GlobalBadge.Text = status.Loaded ? $"{status.Rules} 条" : "—";
            RulesPath.Text = status.Loaded ? $"规则文件 {status.Path}" : "规则文件未加载";
            Empty.Visibility = status.Loaded ? Visibility.Collapsed : Visibility.Visible;
        });
    }
}
