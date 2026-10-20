using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace FlowGrid.UI.Views;

public partial class FilesView : UserControl
{
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
            Transfers.ItemsSource = transfers.Where(t => !t.Done).Select(t => new ActiveRow(t)).ToList();
            Records.ItemsSource = transfers.Where(t => t.Done).Select(t => new RecordRow(t)).ToList();
        });
    }

    private void OnPickFileClick(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Title = "选择要发送的文件" };
        if (dlg.ShowDialog() != true)
        {
            return;
        }
        SendFile(dlg.FileName);
    }

    private void OnDragOverFile(object sender, DragEventArgs e)
    {
        e.Handled = true;
        e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop)
            ? DragDropEffects.Copy
            : DragDropEffects.None;
    }

    private void OnDropFile(object sender, DragEventArgs e)
    {
        e.Handled = true;
        if (e.Data.GetData(DataFormats.FileDrop) is not string[] paths || paths.Length == 0)
        {
            return;
        }
        SendFile(paths[0]);
    }

    // 选完/拖入后立即向首个在线对端发送（MVP：无多目标选择，由后续迭代增强）。
    private void SendFile(string path)
    {
        var peer = App.LastLinks.FirstOrDefault(l => l.State == "online");
        if (peer == null)
        {
            MessageBox.Show("没有在线对端可发送", "FlowGrid");
            return;
        }
        try
        {
            _ = App.Core.SendFileAsync(peer.Instance, path);
        }
        catch (Exception)
        {
        }
    }

    private static string FormatBytes(int total)
    {
        if (total <= 0)
        {
            return "0 B";
        }
        string[] units = { "B", "KB", "MB", "GB" };
        var value = (double)total;
        var unit = 0;
        while (value >= 1024 && unit < units.Length - 1)
        {
            value /= 1024;
            unit++;
        }
        return $"{value:0.#} {units[unit]}";
    }

    /// <summary>进行中行投影：DTO 只给字节数，大小文案与方向措辞由视图拼装（U3）。</summary>
    private sealed record ActiveRow(TransferInfoDto T)
    {
        public string Name => T.Name;
        public double PercentValue => T.PercentValue;
        public string Percent => T.Percent;
        public bool IsReceive => T.Dir != "send";
        public string SizeText => FormatBytes(T.Total);
        public string PeerText => IsReceive ? "来自 " + T.Peer : T.Peer;
    }

    /// <summary>
    /// 记录行投影：把会话层的错误串映射成徽章语义（U4）。
    /// Err 空 → ok「已完成」；校验/大小不一致、超限 → danger「校验失败/超限」；
    /// 拒绝、超时 → warn「已拒绝/超时」；其余未知错误 → danger「失败」兜底。
    /// 关键词对齐 protocol/filetransfer.go 的错误文案（SHA-256 mismatch / out of range /
    /// not acknowledged / stopped sending / aborted / rejected 等）。
    /// </summary>
    private sealed record RecordRow(TransferInfoDto T)
    {
        private enum Kinds { Ok, Rejected, Timeout, Verified, Oversize, Failed }

        public string Name => T.Name;
        // FL5：记录行第二行 = 方向 · 对端 · 大小 · 结果说明（对齐 files.html:86）。
        public string Display => T.Err != ""
            ? $"{DirWord} · {T.Peer} · 失败：{T.Err}"
            : $"{DirWord} · {T.Peer} · {FormatBytes(T.Total)} · SHA-256 已校验";
        private string DirWord => T.Dir == "send" ? "发送" : "收到";
        public bool IsOk => T.Err == "";
        public bool IsWarn => !IsOk && Kind is Kinds.Rejected or Kinds.Timeout;
        public bool IsDanger => !IsOk && !IsWarn;
        public string StateText => Kind switch
        {
            Kinds.Rejected => "已拒绝",
            Kinds.Timeout => "超时",
            Kinds.Verified => "校验失败",
            Kinds.Oversize => "超限",
            Kinds.Failed => "失败",
            _ => "完成",
        };

        private Kinds Kind => IsOk
            ? Kinds.Ok
            : Has("mismatch", "sha-256", "hash", "differs from offer", "校验") ? Kinds.Verified
            : Has("out of range", "too large", "上限", "超限", "limit") ? Kinds.Oversize
            : Has("timeout", "not acknowledged", "stopped sending", "stopped accepting", "超时", "无进展") ? Kinds.Timeout
            : Has("reject", "abort", "refus", "拒绝") ? Kinds.Rejected
            : Kinds.Failed;

        private bool Has(params string[] needles) =>
            needles.Any(n => T.Err.Contains(n, StringComparison.OrdinalIgnoreCase));
    }
}
