using System.IO;
using System.Windows;
using System.Windows.Controls;

namespace FlowGrid.UI.Views;

// 历史行视图模型（过滤在 UI 侧完成；kind 启发式：http 前缀=链接，其余=文本）。
public sealed class ClipRow
{
    public int Index { get; init; }
    public string Preview { get; init; } = "";
    public string Hint { get; init; } = "";
    public string Time { get; init; } = "";
    public string Kind { get; init; } = "text";
}

public partial class ClipboardView : UserControl
{
    private List<ClipEntryDto> _all = new();
    private bool _ready;

    public ClipboardView()
    {
        InitializeComponent();
        Loaded += (_, _) =>
        {
            App.Core.ClipboardHistory += OnHistory;
            _ready = true;
            _ = App.Core.RefreshClipboardHistoryAsync();
        };
        Unloaded += (_, _) => App.Core.ClipboardHistory -= OnHistory;
    }

    private void OnHistory(IReadOnlyList<ClipEntryDto> entries)
    {
        Dispatcher.Invoke(() =>
        {
            _all = entries.ToList();
            ApplyFilter();
            if (_all.Count > 0)
            {
                PreviewText.Text = _all[0].Preview;
            }
        });
    }

    private void ApplyFilter()
    {
        var wantLink = FilterLink.IsChecked == true;
        var q = (SearchBox.Text ?? "").Trim();
        var rows = _all
            .Select(e => new ClipRow
            {
                Index = e.Index,
                Preview = e.Preview,
                Hint = $"{e.Source} · {e.Size} B · text/plain",
                Time = e.Time,
                Kind = e.Preview.StartsWith("http://", StringComparison.OrdinalIgnoreCase)
                    || e.Preview.StartsWith("https://", StringComparison.OrdinalIgnoreCase)
                    ? "link" : "text",
            })
            .Where(r => !wantLink || r.Kind == "link")
            .Where(r => q == "" || r.Preview.Contains(q, StringComparison.OrdinalIgnoreCase))
            .ToList();
        History.ItemsSource = rows;
        NoHistory.Visibility = rows.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
    }

    private void OnFilterChanged(object sender, RoutedEventArgs e)
    {
        if (_ready)
        {
            ApplyFilter();
        }
    }

    private void OnSearchChanged(object sender, TextChangedEventArgs e)
    {
        if (_ready)
        {
            ApplyFilter();
        }
    }

    private async void OnWriteClick(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { Tag: int index })
        {
            try
            {
                await App.Core.WriteClipboardEntryAsync(index);
            }
            catch (Exception)
            {
            }
        }
    }

    private async void OnPreviewPushClick(object sender, RoutedEventArgs e)
    {
        var text = PreviewText.Text;
        if (string.IsNullOrEmpty(text) || text == "（空）")
        {
            return;
        }
        try
        {
            await App.Core.SendClipboardTextAsync(text);
        }
        catch (Exception)
        {
        }
    }

    private void OnPreviewWriteClick(object sender, RoutedEventArgs e)
    {
        // 读当前 OS 剪贴板再写回=空操作；此处语义为「把预览内容固定写入本地」，
        // 数据源即本机剪贴板，直接刷新预览即可。
        _ = App.Core.RefreshClipboardHistoryAsync();
    }

    private async void OnClearClick(object sender, RoutedEventArgs e)
    {
        try
        {
            await App.Core.ClearClipboardHistoryAsync();
            await App.Core.RefreshClipboardHistoryAsync();
        }
        catch (Exception)
        {
        }
    }

    private async void OnSyncToggle(object sender, RoutedEventArgs e)
    {
        if (!_ready)
        {
            return;
        }
        try
        {
            await App.Core.SetClipSyncAsync(SyncToggle.IsChecked == true);
        }
        catch (Exception)
        {
        }
    }
}
