using System.Windows;
using System.Windows.Controls;

namespace FlowGrid.UI.Views;

public partial class PairingView : UserControl
{
    public PairingView()
    {
        InitializeComponent();
        Loaded += (_, _) => App.Core.DevicesReceived += OnDevices;
        Unloaded += (_, _) => App.Core.DevicesReceived -= OnDevices;
    }

    private void OnDevices(IReadOnlyList<PeerInfo> devices)
    {
        Dispatcher.Invoke(() =>
        {
            DeviceList.ItemsSource = devices
                .Select(d => new DiscoveredDevice(
                    Name: d.Name,
                    Instance: d.Instance,
                    Hint: string.IsNullOrEmpty(d.Platform)
                        ? $"{d.Addr}:{d.Port}"
                        : $"{d.Addr}:{d.Port} · {d.Platform}"))
                .ToList();
            NoDevices.Visibility = devices.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        });
    }

    private async void OnPairClick(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: string instance })
        {
            return;
        }
        try
        {
            await App.Core.ConnectPeerAsync(instance);
            App.ShowToast("配对请求已发送");
        }
        catch (Exception)
        {
            App.ShowToast("发送失败：fgcore 未连接");
        }
    }

    private void OnCancelClick(object sender, RoutedEventArgs e) => Window.GetWindow(this)?.Hide();

    private sealed record DiscoveredDevice(string Name, string Instance, string Hint);
}
