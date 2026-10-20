using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;

namespace FlowGrid.UI.Views;

// 历史行视图模型（过滤在 UI 侧完成）。
// kind 判定：ClipEntryDto 无类型字段，采用保守启发——带 scheme 前缀（xxx://）即链接，
// 其余一律按文本；「代码/图片」段按 D4 置灰展示，不参与判定。
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
    private static readonly Regex SchemePrefix = new(@"^[a-zA-Z][a-zA-Z0-9+.\-]*://", RegexOptions.Compiled);

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
            // 预览卡固定展示最新一条（与设计稿 preview 语义一致）；
            // 分段信息 DTO 无对应字段，按「单帧发送 · 未分段」口径呈现。
            if (_all.Count > 0)
            {
                var e = _all[0];
                PreviewText.Text = string.IsNullOrEmpty(e.Preview) ? "（空）" : e.Preview;
                PreviewSource.Text = $"来自 {e.Source}";
                PreviewMeta.Text = $"{e.Size} B · 单帧发送 · 未分段";
            }
            else
            {
                PreviewText.Text = "（空）";
                PreviewSource.Text = "—";
                PreviewMeta.Text = "—";
            }
        });
    }

    private void ApplyFilter()
    {
        // U2：「文本/链接」按 kind 真过滤（此前文本档等于不过滤）
        var kind = FilterLink.IsChecked == true ? "link"
            : FilterAll.IsChecked == true ? "all"
            : "text";
        var q = (SearchBox.Text ?? "").Trim();
        var rows = _all
            .Select(e => new ClipRow
            {
                Index = e.Index,
                Preview = e.Preview,
                // CB12：方向段按 Dir 组装（hub 侧 send=本机产生 / recv=对端→本机）
                Hint = e.Dir == "recv"
                    ? $"{e.Source} → 本机 · {e.Size} B · text/plain"
                    : $"本机 · {e.Size} B · text/plain",
                Time = e.Time,
                Kind = SchemePrefix.IsMatch(e.Preview) ? "link" : "text",
            })
            .Where(r => kind == "all" || r.Kind == kind)
            // CB15：命中范围扩到 Preview + Hint（Source/方向/大小都在 Hint 内）
            .Where(r => q == ""
                || r.Preview.Contains(q, StringComparison.OrdinalIgnoreCase)
                || r.Hint.Contains(q, StringComparison.OrdinalIgnoreCase))
            .ToList();
        History.ItemsSource = rows;
        HistoryCard.Visibility = rows.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
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
        SearchHint.Visibility = string.IsNullOrEmpty(SearchBox.Text)
            ? Visibility.Visible
            : Visibility.Collapsed;
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
                Toast("已写入本地剪贴板");
            }
            catch (Exception)
            {
            }
        }
    }

    // CB8：预览卡「写入本地」复用行内写入逻辑（fgcore 写 OS 剪贴板 + 回声抑制）。
    private async void OnPreviewWriteClick(object sender, RoutedEventArgs e)
    {
        if (_all.Count == 0)
        {
            return;
        }
        try
        {
            await App.Core.WriteClipboardEntryAsync(_all[0].Index);
            Toast("已写入本地剪贴板");
        }
        catch (Exception)
        {
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
            // 设计稿文案为「已推送到 {设备名}」；发送是广播语义、UI 层拿不到目标名单，
            // 退化为不含设备名的口径。
            Toast("已推送到在线设备");
        }
        catch (Exception)
        {
        }
    }

    private async void OnClearClick(object sender, RoutedEventArgs e)
    {
        try
        {
            await App.Core.ClearClipboardHistoryAsync();
            await App.Core.RefreshClipboardHistoryAsync();
            Toast("已清空同步历史");
        }
        catch (Exception)
        {
        }
    }

    private void Toast(string message) => (Window.GetWindow(this) as ShellWindow)?.ShowToast(message);

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
