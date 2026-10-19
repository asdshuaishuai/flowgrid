// fgcore 的管道客户端（openspec/changes/windows-client/specs/windows-ipc）。
// 无状态策略：每次连接 hello → devices.list → status.get 拉全量，此后只消费
// device.changed 推送；断线固定退避重连，重连成功重新拉全量。
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace FlowGrid.UI;

public sealed record PeerInfo(
    string Name, string Instance, string Addr, int Port,
    string Platform, string Version, string Caps, string Encryption)
{
    // 设备卡投影：圆形徽标首字母 + 元信息行（macos · directlink,clipboard）
    public string Initials =>
        Name.Length <= 2 ? Name.ToUpperInvariant() : Name[..2].ToUpperInvariant();

    public string Meta => string.IsNullOrEmpty(Platform) ? Addr : $"{Platform} · {Caps}";
}

public sealed record LinkInfo(
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("instance")] string Instance,
    [property: JsonPropertyName("addr")] string Addr,
    [property: JsonPropertyName("port")] int Port,
    [property: JsonPropertyName("state")] string State,
    [property: JsonPropertyName("latency_ms")] double LatencyMs,
    [property: JsonPropertyName("lat_min_ms")] double LatMinMs,
    [property: JsonPropertyName("lat_max_ms")] double LatMaxMs,
    [property: JsonPropertyName("lat_avg_ms")] double LatAvgMs,
    [property: JsonPropertyName("platform")] string Platform,
    [property: JsonPropertyName("fingerprint")] string Fingerprint,
    [property: JsonPropertyName("trusted")] bool Trusted,
    [property: JsonPropertyName("inbound")] bool Inbound)
{
    public string LatencyText => State == "online" ? $"{LatencyMs:0.#}ms" : "—";
    public string LatencyStats => State == "online"
        ? $"min {LatMinMs:0.#} / 均 {LatAvgMs:0.#} / max {LatMaxMs:0.#} ms"
        : "—";
}

public sealed record InputStatusDto(
    [property: JsonPropertyName("hosting")] string Hosting,
    [property: JsonPropertyName("switch_hint")] string SwitchHint,
    [property: JsonPropertyName("clip_sync")] bool ClipSync,
    [property: JsonPropertyName("auto_accept")] bool AutoAccept);

public sealed record TransferInfoDto(
    [property: JsonPropertyName("dir")] string Dir,
    [property: JsonPropertyName("peer")] string Peer,
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("sent")] int Sent,
    [property: JsonPropertyName("total")] int Total,
    [property: JsonPropertyName("done")] bool Done,
    [property: JsonPropertyName("err")] string Err,
    [property: JsonPropertyName("path")] string Path)
{
    public string DirText => Dir == "send" ? "发送" : "接收";
    public string Display => Done
        ? (Err != "" ? $"失败：{Err}" : Path != "" ? Path : "完成")
        : $"{Sent}/{Total}";
    public double PercentValue => Total > 0 ? (double)Sent / Total * 100 : 0;
    public string Percent => $"{PercentValue:0}%";
}

public sealed record KeymapStatusDto(
    [property: JsonPropertyName("loaded")] bool Loaded,
    [property: JsonPropertyName("path")] string Path,
    [property: JsonPropertyName("rules")] int Rules);

public sealed record ClipEntryDto(
    [property: JsonPropertyName("index")] int Index,
    [property: JsonPropertyName("preview")] string Preview,
    [property: JsonPropertyName("source")] string Source,
    [property: JsonPropertyName("dir")] string Dir,
    [property: JsonPropertyName("size")] int Size,
    [property: JsonPropertyName("time")] string Time)
{
    public string Hint => $"{Source} · {Size} B · text/plain";
}

public sealed record TrustViewDto(
    [property: JsonPropertyName("fingerprint")] string Fingerprint,
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("added")] string Added);

public sealed record PairPendingInfo(
    [property: JsonPropertyName("id")] string Id,
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("fingerprint")] string Fingerprint,
    [property: JsonPropertyName("addr")] string Addr);

public sealed record CoreStatus(
    [property: JsonPropertyName("mdns")] bool Mdns,
    [property: JsonPropertyName("mdns_error")] string MdnsError,
    [property: JsonPropertyName("peers")] int Peers,
    [property: JsonPropertyName("uptime_s")] long UptimeSec,
    [property: JsonPropertyName("version")] string Version);

public sealed class CoreLink : IDisposable
{
    private const string PipeName = "flowgrid-core";
    private static readonly JsonSerializerOptions JsonOpts = new(JsonSerializerDefaults.Web);

    private readonly CancellationTokenSource _cts = new();
    private readonly object _writerLock = new();
    private StreamWriter? _writer;
    private Task? _loop;

    /// <summary>"connecting" | "online" | "offline"，后台线程触发。</summary>
    public event Action<string>? StateChanged;
    public event Action<CoreStatus>? StatusReceived;
    public event Action<IReadOnlyList<PeerInfo>>? DevicesReceived;
    public event Action<IReadOnlyList<LinkInfo>>? LinksReceived;
    public event Action<IReadOnlyList<PairPendingInfo>>? PendingReceived;
    public event Action<InputStatusDto>? InputChanged;
    public event Action<IReadOnlyList<TransferInfoDto>>? FileProgress;
    public event Action<KeymapStatusDto>? KeymapStatusReceived;
    public event Action<IReadOnlyList<ClipEntryDto>>? ClipboardHistory;
    public event Action<IReadOnlyList<TrustViewDto>>? TrustList;

    private IReadOnlyList<LinkInfo> _lastLinks = Array.Empty<LinkInfo>();

    public CoreLink()
    {
        // 自留一份最新链接快照，供 DisconnectAllAsync 等内部逻辑使用。
        LinksReceived += links => _lastLinks = links;
    }

    public void Start()
    {
        _loop ??= Task.Run(() => RunAsync(_cts.Token));
    }

    private async Task RunAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            StateChanged?.Invoke("connecting");
            try
            {
                using var pipe = new NamedPipeClientStream(
                    ".", PipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
                using var dialTimeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
                dialTimeout.CancelAfter(TimeSpan.FromSeconds(3));
                await pipe.ConnectAsync(dialTimeout.Token);

                _writer = new StreamWriter(pipe, new UTF8Encoding(false)) { AutoFlush = true };
                StateChanged?.Invoke("online");

                await SendAsync("hello");
                await SendAsync("devices.list");
                await SendAsync("status.get");
                await SendAsync("peers.list");
                await SendAsync("input.status");
                await SendAsync("file.transfers");
                await SendAsync("keymap.status");
                await SendAsync("clipboard.history");
                await SendAsync("trust.list");
                await ReadLoopAsync(pipe, ct); // 返回即连接中断，落入下方重连
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested)
            {
                return;
            }
            catch (Exception)
            {
                // 核心未起 / 连接中断：退避后重试，界面由 offline 状态如实反映。
            }
            lock (_writerLock)
            {
                _writer?.Dispose();
                _writer = null;
            }
            StateChanged?.Invoke("offline");
            try
            {
                await Task.Delay(TimeSpan.FromSeconds(2), ct);
            }
            catch (OperationCanceledException)
            {
                return;
            }
        }
    }

    private async Task ReadLoopAsync(PipeStream pipe, CancellationToken ct)
    {
        using var reader = new StreamReader(pipe, Encoding.UTF8,
            detectEncodingFromByteOrderMarks: false, bufferSize: 4096, leaveOpen: true);
        while (!ct.IsCancellationRequested)
        {
            var line = await reader.ReadLineAsync(ct);
            if (line == null)
            {
                return; // 对端关闭
            }
            HandleLine(line);
        }
    }

    private void HandleLine(string line)
    {
        try
        {
            var env = JsonSerializer.Deserialize<Envelope>(line, JsonOpts);
            if (env == null || env.Payload.ValueKind != JsonValueKind.Object)
            {
                return;
            }
            switch (env.Type)
            {
                case "status":
                    var st = JsonSerializer.Deserialize<CoreStatus>(env.Payload, JsonOpts);
                    if (st != null)
                    {
                        StatusReceived?.Invoke(st);
                    }
                    break;
                case "devices":
                case "device.changed":
                    var d = JsonSerializer.Deserialize<DevicesDto>(env.Payload, JsonOpts);
                    if (d?.Peers != null)
                    {
                        DevicesReceived?.Invoke(d.Peers);
                    }
                    break;
                case "peers":
                case "peers.changed":
                    var p = JsonSerializer.Deserialize<LinksDto>(env.Payload, JsonOpts);
                    if (p?.Links != null)
                    {
                        LinksReceived?.Invoke(p.Links);
                    }
                    if (p?.Pending != null)
                    {
                        PendingReceived?.Invoke(p.Pending);
                    }
                    break;
                case "pair.pending":
                    var pp = JsonSerializer.Deserialize<PendingDto>(env.Payload, JsonOpts);
                    if (pp?.Pending != null)
                    {
                        PendingReceived?.Invoke(pp.Pending);
                    }
                    break;
                case "input.status.ok":
                case "input.changed":
                    var ist = JsonSerializer.Deserialize<InputStatusDto>(env.Payload, JsonOpts);
                    if (ist != null)
                    {
                        InputChanged?.Invoke(ist);
                    }
                    break;
                case "file.progress":
                case "file.transfers.ok":
                    var fp = JsonSerializer.Deserialize<TransfersDto>(env.Payload, JsonOpts);
                    if (fp?.Transfers != null)
                    {
                        FileProgress?.Invoke(fp.Transfers);
                    }
                    break;
                case "keymap.status.ok":
                    var km = JsonSerializer.Deserialize<KeymapStatusDto>(env.Payload, JsonOpts);
                    if (km != null)
                    {
                        KeymapStatusReceived?.Invoke(km);
                    }
                    break;
                case "clipboard.history.ok":
                    var ch = JsonSerializer.Deserialize<HistoryDto>(env.Payload, JsonOpts);
                    if (ch?.Entries != null)
                    {
                        ClipboardHistory?.Invoke(ch.Entries);
                    }
                    break;
                case "trust.list.ok":
                    var tl = JsonSerializer.Deserialize<TrustListDto>(env.Payload, JsonOpts);
                    if (tl?.Entries != null)
                    {
                        TrustList?.Invoke(tl.Entries);
                    }
                    break;
                case "error":
                    // 单条坏请求的应答，骨架阶段仅吞掉；连接不受影响。
                    break;
            }
        }
        catch (JsonException)
        {
            // 单条坏消息不断链。
        }
    }

    private void Send(string type, object? payload = null)
    {
        var line = payload == null
            ? JsonSerializer.Serialize(new { v = 1, type }, JsonOpts)
            : JsonSerializer.Serialize(new { v = 1, type, payload }, JsonOpts);
        lock (_writerLock)
        {
            if (_writer == null)
            {
                throw new IOException("core link closed");
            }
            _writer.WriteLine(line);
        }
    }

    private async Task SendAsync(string type, object? payload = null)
    {
        Send(type, payload);
        await Task.CompletedTask;
    }

    /// <summary>请求连接已配对的发现设备（windows-link）。</summary>
    public Task ConnectPeerAsync(string instance) => SendAsync("connect.request", new { instance });

    /// <summary>断开在线链接（用户语义：不再自动重连）。</summary>
    public Task DisconnectPeerAsync(string instance) => SendAsync("disconnect.request", new { instance });

    /// <summary>对 pair.pending 的用户裁决。</summary>
    public Task ResolvePairAsync(string id, bool accept) =>
        SendAsync(accept ? "pair.accept" : "pair.reject", new { id });

    /// <summary>切换托管（0=本机，1..8=在线对端）。</summary>
    public Task SwitchToAsync(int index) => SendAsync("switch.request", new { index });

    /// <summary>发送文本剪贴板到在线对端。</summary>
    public Task SendClipboardTextAsync(string text) => SendAsync("clipboard.set", new { text });

    /// <summary>向在线对端发送文件。</summary>
    public Task SendFileAsync(string instance, string path) => SendAsync("file.send", new { instance, path });

    /// <summary>退出前逐一对在线对端走用户断开语义（DISCONNECT 0x00）。</summary>
    public async Task DisconnectAllAsync()
    {
        foreach (var link in _lastLinks.Where(l => l.State == "online").ToList())
        {
            try
            {
                await DisconnectPeerAsync(link.Instance);
            }
            catch (IOException)
            {
            }
        }
    }

    /// <summary>拉取剪贴板历史。</summary>
    public Task RefreshClipboardHistoryAsync() => SendAsync("clipboard.history");

    /// <summary>把某条历史写入本地 OS 剪贴板（fgcore 侧写入 + 回声抑制）。</summary>
    public Task WriteClipboardEntryAsync(int index) => SendAsync("clipboard.write", new { index });

    /// <summary>清空剪贴板历史。</summary>
    public Task ClearClipboardHistoryAsync() => SendAsync("clipboard.clear");

    /// <summary>设置历史容量（条数）。</summary>
    public Task SetClipboardCapAsync(int cap) => SendAsync("clipboard.cap", new { cap });

    /// <summary>跨端同步开关。</summary>
    public Task SetClipSyncAsync(bool enabled) => SendAsync("clipboard.enable", new { enabled });

    /// <summary>「新设备需确认」开关（= autoaccept 的反面）。</summary>
    public Task SetRequireConfirmAsync(bool require) => SendAsync("config.autoaccept", new { enabled = !require });

    /// <summary>撤销一条已授权设备。</summary>
    public Task TrustRevokeAsync(string fingerprint) => SendAsync("trust.revoke", new { fingerprint });

    /// <summary>拉取已授权设备列表。</summary>
    public Task RefreshTrustListAsync() => SendAsync("trust.list");

    public void Dispose()
    {
        _cts.Cancel();
        try
        {
            _loop?.Wait(1500);
        }
        catch (AggregateException)
        {
            // 后台循环的收尾异常在断开场景下无所谓。
        }
    }

    private sealed record Envelope(
        [property: JsonPropertyName("v")] int V,
        [property: JsonPropertyName("type")] string Type,
        [property: JsonPropertyName("id")] string? Id,
        [property: JsonPropertyName("payload")] JsonElement Payload);

    private sealed record DevicesDto(
        [property: JsonPropertyName("peers")] List<PeerInfo> Peers);

    private sealed record LinksDto(
        [property: JsonPropertyName("links")] List<LinkInfo> Links,
        [property: JsonPropertyName("pending")] List<PairPendingInfo> Pending);

    private sealed record PendingDto(
        [property: JsonPropertyName("pending")] List<PairPendingInfo> Pending);

    private sealed record TransfersDto(
        [property: JsonPropertyName("transfers")] List<TransferInfoDto> Transfers);

    private sealed record HistoryDto(
        [property: JsonPropertyName("entries")] List<ClipEntryDto> Entries);

    private sealed record TrustListDto(
        [property: JsonPropertyName("entries")] List<TrustViewDto> Entries);
}
