using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace FlowGrid.UI.Views;

public partial class FilesView : UserControl
{
    private string? _pickedPath;

    public FilesView()
    {
        InitializeComponent();
        Loaded += (_, _) => App.Core.FileProgress += OnProgress;
        Unloaded += (_, _) => App.Core.FileProgress -= OnProgress;
    }

    private void OnProgress(IReadOnlyList<TransferInfoDto> transfers)
    {
        Dispatcher.Invoke(() =>
        {
            Transfers.ItemsSource = transfers.Where(t => !t.Done).ToList();
            Records.ItemsSource = transfers.Where(t => t.Done).ToList();
        });
    }

    private void OnPickFileClick(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Title = "选择要发送的文件" };
        if (dlg.ShowDialog() != true)
        {
            return;
        }
        _pickedPath = dlg.FileName;
        // 选完文件立即向首个在线对端发送（MVP：无多目标选择，由后续迭代增强）。
        var peer = App.LastLinks.FirstOrDefault(l => l.State == "online");
        if (peer == null)
        {
            MessageBox.Show("没有在线对端可发送", "FlowGrid");
            return;
        }
        try
        {
            _ = App.Core.SendFileAsync(peer.Instance, _pickedPath);
        }
        catch (Exception)
        {
        }
    }
}
