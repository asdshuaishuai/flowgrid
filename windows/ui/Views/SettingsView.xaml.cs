using System.IO;
using System.Windows;
using System.Windows.Controls;

namespace FlowGrid.UI.Views;

public partial class SettingsView : UserControl
{
    private const string UiVersion = "0.1.0";
    private bool _ready;

    public SettingsView()
    {
        InitializeComponent();
        Loaded += (_, _) =>
        {
            App.Core.StatusReceived += OnStatusReceived;
            App.Core.TrustList += OnTrustList;
            SyncToggles();
            _ready = true;
            _ = App.Core.RefreshTrustListAsync();
        };
        // 退订全部核心事件：壳层导航往返会反复实例化本视图，漏退订即泄漏（SE11）。
        Unloaded += (_, _) =>
        {
            App.Core.StatusReceived -= OnStatusReceived;
            App.Core.TrustList -= OnTrustList;
        };
    }

    private void SyncToggles()
    {
        // _ready 置位前不触发各 Checked 处理器，避免初始化期间反向写偏好。
        _ready = false;

        (ThemeManager.Preference switch
        {
            ThemePreference.Light => ThemeLight,
            ThemePreference.Dark => ThemeDark,
            _ => ThemeAuto,
        }).IsChecked = true;

        AutostartToggle.IsChecked = Autostart.IsEnabled();
        ClipSyncToggle.IsChecked = App.LastClipSync;
        RequireConfirmToggle.IsChecked = !App.LastAutoAccept;

        _ready = true;
    }

    private void OnThemeChecked(object sender, RoutedEventArgs e)
    {
        if (!_ready)
        {
            return;
        }
        var preference =
            ReferenceEquals(sender, ThemeLight) ? ThemePreference.Light :
            ReferenceEquals(sender, ThemeDark) ? ThemePreference.Dark :
            ThemePreference.Auto;
        ThemeManager.SetTheme(preference);
    }

    private void OnAutostartChecked(object sender, RoutedEventArgs e)
    {
        if (!_ready)
        {
            return;
        }
        var on = AutostartToggle.IsChecked == true;
        try
        {
            Autostart.SetEnabled(on);
            Toast(on, "开机自启");
        }
        catch (Exception)
        {
        }
    }

    private async void OnClipSyncChecked(object sender, RoutedEventArgs e)
    {
        if (!_ready)
        {
            return;
        }
        var on = ClipSyncToggle.IsChecked == true;
        try
        {
            await App.Core.SetClipSyncAsync(on);
            Toast(on, "剪贴板同步");
        }
        catch (Exception)
        {
        }
    }

    private async void OnRequireConfirmChecked(object sender, RoutedEventArgs e)
    {
        if (!_ready)
        {
            return;
        }
        var on = RequireConfirmToggle.IsChecked == true;
        try
        {
            await App.Core.SetRequireConfirmAsync(on);
            Toast(on, "新设备确认");
        }
        catch (Exception)
        {
        }
    }

    // 「XX已开启/已关闭」——文案格式对齐 design/unified/app.js:232-235 的开关 toast（SE13）。
    private void Toast(bool on, string label) =>
        (Window.GetWindow(this) as ShellWindow)?.ShowToast(label + (on ? "已开启" : "已关闭"));

    private async void OnCapChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_ready || CapBox.SelectedItem is not ComboBoxItem item)
        {
            return;
        }
        if (int.TryParse(item.Tag as string, out var cap))
        {
            try
            {
                await App.Core.SetClipboardCapAsync(cap);
            }
            catch (Exception)
            {
            }
        }
    }

    private void OnTrustList(IReadOnlyList<TrustViewDto> entries)
    {
        Dispatcher.Invoke(() =>
        {
            TrustList.Children.Clear();
            if (entries.Count == 0)
            {
                TrustList.Children.Add(new TextBlock
                {
                    Text = "暂无已授权设备",
                    Margin = new Thickness(12, 10, 12, 10),
                });
                return;
            }
            foreach (var entry in entries)
            {
                var row = new Border { Style = (Style)FindResource("ListRow") };
                var grid = new Grid();
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                var info = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
                info.Children.Add(new TextBlock { Text = entry.Name, FontSize = 12 });
                info.Children.Add(new TextBlock { Text = entry.Fingerprint, FontSize = 11 });
                grid.Children.Add(info);
                var btn = new Button
                {
                    Content = "撤销",
                    Style = (Style)FindResource("BtnSmSecondary"),
                    Tag = entry.Fingerprint,
                    VerticalAlignment = VerticalAlignment.Center,
                    Margin = new Thickness(0, 0, 8, 0),
                };
                btn.Click += async (s, args) =>
                {
                    try
                    {
                        await App.Core.TrustRevokeAsync(entry.Fingerprint);
                        // 撤销后主动拉列表，等 trust.list.ok 推送回来重绘（SE10）。
                        await App.Core.RefreshTrustListAsync();
                    }
                    catch (Exception) { }
                };
                Grid.SetColumn(btn, 1);
                grid.Children.Add(btn);
                row.Child = grid;
                TrustList.Children.Add(row);
            }
        });
    }

    private void OnStatusReceived(CoreStatus s)
    {
        Dispatcher.Invoke(() =>
        {
            CoreVersion.Text = s.Version;
            MdnsState.Text = s.Mdns ? "运行中" : "不可用" + (string.IsNullOrEmpty(s.MdnsError) ? "" : $"（{s.MdnsError}）");
        });
    }

    private void OnOpenKeymapClick(object sender, RoutedEventArgs e)
    {
        (Window.GetWindow(this) as ShellWindow)?.Navigate("keymap");
    }
}
