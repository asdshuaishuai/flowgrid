using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using FlowGrid.UI.Views;

namespace FlowGrid.UI;

public partial class App : Application
{
    public static CoreLink Core { get; } = new();

    /// <summary>托盘「退出」置位后窗口 Closing 不再拦截（否则隐藏会阻止关停）。</summary>
    public static bool IsExiting { get; set; }

    // 托盘浮板等轻消费者读取的最新状态快照（事件里同步更新）。
    public static string? CoreLinkState { get; private set; }
    public static string? LastHosting { get; private set; }
    public static bool LastClipSync { get; private set; } = true;
    public static bool LastAutoAccept { get; private set; }
    public static IReadOnlyList<LinkInfo> LastLinks { get; private set; } = Array.Empty<LinkInfo>();
    public static int LastClipboardCount { get; private set; }
    public static int LastTransferCount { get; private set; }

    private TrayIcon? _tray;
    private Mutex? _mutex;

    protected override void OnStartup(StartupEventArgs e)
    {
        // 全局异常兜底：不让未处理异常弹 JIT 调试对话框直接打死进程。
        DispatcherUnhandledException += (_, args) =>
        {
            MessageBox.Show($"发生未处理的异常：\n{args.Exception.Message}\n\n{args.Exception.StackTrace}",
                "FlowGrid 错误", MessageBoxButton.OK, MessageBoxImage.Warning);
            args.Handled = true;
        };

        _mutex = new Mutex(true, "FlowGridUI-SingleInstance", out var createdNew);
        if (!createdNew)
        {
            MessageBox.Show("FlowGrid 已在运行。", "FlowGrid",
                MessageBoxButton.OK, MessageBoxImage.Information);
            Shutdown();
            return;
        }

        base.OnStartup(e);
        ThemeManager.Initialize();
        Core.StateChanged += s => CoreLinkState = s;
        Core.InputChanged += st =>
        {
            LastHosting = st.Hosting;
            LastClipSync = st.ClipSync;
            LastAutoAccept = st.AutoAccept;
        };
        Core.LinksReceived += links => LastLinks = links;
        Core.ClipboardHistory += entries => LastClipboardCount = entries.Count;
        Core.FileProgress += transfers => LastTransferCount = transfers.Count(t => !t.Done);
        Core.Start();
        _tray = new TrayIcon();
    }

    /// <summary>打开（或唤起）520 面板并导航到指定目的地。</summary>
    public static void OpenPanel(string destination)
    {
        if (Current.MainWindow is not ShellWindow shell)
        {
            shell = new ShellWindow();
            Current.MainWindow = shell;
        }
        shell.Show();
        shell.Navigate(destination);
        shell.PlayEntrance();
        shell.Activate();
    }

    /// <summary>面板内 Toast（供 View 通过 GetWindow 调用）。</summary>
    public static void ShowToast(string message)
    {
        if (Current.MainWindow is ShellWindow shell)
        {
            shell.ShowToast(message);
        }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _tray?.Dispose();
        Core.Dispose();
        _mutex?.Dispose();
        base.OnExit(e);
    }
}
