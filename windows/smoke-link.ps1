# Two-node link smoke on one machine (specs/windows-link):
#   Alpha listens on 24801; Beta imports Alpha's certificate out-of-band
#   (PROTOCOL-ISSUES P3 bootstrap), resolves Alpha, connects, exchanges
#   IDENTIFY, runs heartbeats -> both sides report "online", then disconnect.
# Same-host mDNS delivery is unreliable on Windows (unicast replies land on
# the other socket bound to 5353), so the address is injected via devices.add;
# real LAN discovery is covered by protocol mdns tests.
# Prerequisite: go build -o fgcore.exe ./cmd/fgcore (in windows\)
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File windows\smoke-link.ps1
# NOTE: keep this script ASCII-only (PowerShell 5.1 reads BOM-less files as GBK).
$ErrorActionPreference = "Stop"

$root = $PSScriptRoot
$fgcore = Join-Path $root "fgcore.exe"
if (-not (Test-Path $fgcore)) { throw "build first: cd windows && go build -o fgcore.exe ./cmd/fgcore" }

$work = Join-Path $root ".smoke"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Path (Join-Path $work "a") | Out-Null
New-Item -ItemType Directory -Path (Join-Path $work "b") | Out-Null

function StartNode([string]$name, [string]$pipe, [int]$port, [string]$dir,
                   [string]$peerCert, [string]$peerName) {
    # winio wants the full \\.\pipe\ path; the .NET client wants the bare name.
    $nodeArgs = @("-name", $name, "-pipe", ("\\.\pipe\" + $pipe), "-port", "$port",
                  "-data", $dir, "-auto-accept")
    if ($peerCert) { $nodeArgs += @("-peer-cert", $peerCert, "-peer-name", $peerName) }
    return Start-Process -FilePath $fgcore -ArgumentList $nodeArgs -PassThru `
        -WindowStyle Hidden -RedirectStandardError (Join-Path $dir "stderr.log")
}

function Invoke-Ipc([string]$pipe, [string[]]$lines) {
    $stream = [System.IO.Pipes.NamedPipeClientStream]::new(
        ".", $pipe, [System.IO.Pipes.PipeDirection]::InOut)
    $stream.Connect(5000)
    $reader = [System.IO.StreamReader]::new($stream)
    $writer = [System.IO.StreamWriter]::new($stream)
    $writer.AutoFlush = $true
    $replies = @()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $id = "$($i + 1)"
        $writer.WriteLine(('{"v":1,"id":"' + $id + '",' + ($lines[$i] -replace '^\{', '{')))
        while ($true) {
            $resp = $reader.ReadLine()
            if (-not $resp) { throw "no reply for: $($lines[$i])" }
            $env = $resp | ConvertFrom-Json
            # id-less lines are async pushes (device.changed / peers.changed);
            # replies always echo the request id.
            if ($env.id -eq $id) { $replies += $env; break }
        }
    }
    $stream.Dispose()
    return $replies
}

function Get-OnlineLink([string]$pipe, [string]$peerName) {
    $r = Invoke-Ipc $pipe @('"type":"peers.list"}')
    foreach ($l in $r[0].payload.links) {
        if ($l.name -eq $peerName -and $l.state -eq "online") { return $l }
    }
    return $null
}

function Wait-Online([string]$pipe, [string]$peerName, [int]$timeoutSec) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        $l = Get-OnlineLink $pipe $peerName
        if ($null -ne $l) { return $l }
        Start-Sleep -Milliseconds 400
    }
    throw "peer $peerName never came online within ${timeoutSec}s"
}

$a = $null; $b = $null
try {
    # Alpha first; Beta imports Alpha's certificate before dialing.
    $a = StartNode "Alpha" "flowgrid-smoke-a" 24801 (Join-Path $work "a") $null $null
    Start-Sleep -Seconds 1
    Copy-Item (Join-Path $work "a\core.pem") (Join-Path $work "b\alpha.pem")
    $b = StartNode "Beta" "flowgrid-smoke-b" 24802 (Join-Path $work "b") (Join-Path $work "b\alpha.pem") "Alpha"

    Start-Sleep -Seconds 3   # let mDNS announcements settle
    # Beta resolves Alpha: via mDNS when the loopback delivers, else inject.
    $r = Invoke-Ipc "flowgrid-smoke-b" @('"type":"devices.list"}')
    $discovered = $false
    foreach ($p in $r[0].payload.peers) {
        if ($p.name -eq "Alpha") { $discovered = $true }
    }
    if ($discovered) {
        Write-Host "[discovery] Alpha found by Beta via mDNS"
    }
    else {
        Write-Host "[discovery] mDNS loopback unavailable, injecting address manually"
        $r = Invoke-Ipc "flowgrid-smoke-b" @('"type":"devices.add","payload":{"name":"Alpha","addr":"127.0.0.1","port":24801}}')
        if ($r[0].type -ne "devices.ok") { throw "devices.add failed: $($r[0] | ConvertTo-Json -Compress)" }
    }

    $r = Invoke-Ipc "flowgrid-smoke-b" @('"type":"connect.request","payload":{"instance":"Alpha._flowgrid._tcp.local."}}')
    if ($r[0].type -ne "connect.ok") { throw "connect.request failed: $($r[0] | ConvertTo-Json -Compress)" }
    Write-Host "[connect] request accepted"

    $linkB = Wait-Online "flowgrid-smoke-b" "Alpha" 15
    Write-Host ("[online] Beta sees Alpha, latency=" + $linkB.latency_ms + "ms")
    $linkA = Wait-Online "flowgrid-smoke-a" "Beta" 15
    Write-Host ("[online] Alpha sees Beta, latency=" + $linkA.latency_ms + "ms")

    # Clipboard end-to-end: Alpha sets text -> Beta receives over the link and
    # writes it into its own OS clipboard; assert via Beta's clipboard.text.
    $r = Invoke-Ipc "flowgrid-smoke-a" @('"type":"clipboard.set","payload":{"text":"flowgrid-clipboard-e2e"}}')
    if ($r[0].type -ne "clipboard.ok") { throw "clipboard.set failed: $($r[0] | ConvertTo-Json -Compress)" }
    $deadline = (Get-Date).AddSeconds(10)
    $clipOk = $false
    while ((Get-Date) -lt $deadline) {
        $r = Invoke-Ipc "flowgrid-smoke-b" @('"type":"clipboard.text"}')
        if ($r[0].payload.text -eq "flowgrid-clipboard-e2e") { $clipOk = $true; break }
        Start-Sleep -Milliseconds 400
    }
    if (-not $clipOk) { throw "clipboard did not arrive at Beta" }
    Write-Host "[clipboard] end-to-end OK"

    # Clipboard history: both sides recorded the sync; clear empties the store.
    $r = Invoke-Ipc "flowgrid-smoke-a" @('"type":"clipboard.history"}')
    if ($r[0].payload.entries.Count -lt 1) { throw "Alpha clipboard history empty" }
    $r = Invoke-Ipc "flowgrid-smoke-b" @('"type":"clipboard.history"}')
    if ($r[0].payload.entries.Count -lt 1 -or $r[0].payload.entries[0].source -ne "Alpha") {
        throw "Beta clipboard history wrong"
    }
    $r = Invoke-Ipc "flowgrid-smoke-b" @('"type":"clipboard.clear"}')
    $r = Invoke-Ipc "flowgrid-smoke-b" @('"type":"clipboard.history"}')
    if ($r[0].payload.entries.Count -ne 0) { throw "clipboard clear failed" }
    Write-Host "[history] record/read/clear OK"

    # File end-to-end: Alpha sends a known file -> Beta auto-accepts it into
    # its downloads dir; compare byte-for-byte.
    $src = Join-Path $work "send-me.bin"
    $payload = New-Object byte[] 4096
    (New-Object Random 42).NextBytes($payload)
    [IO.File]::WriteAllBytes($src, $payload)
    $jsonPath = $src -replace '\\', '\\'
    $r = Invoke-Ipc "flowgrid-smoke-a" @('"type":"file.send","payload":{"instance":"Beta._flowgrid._tcp.local.","path":"' + $jsonPath + '"}}')
    if ($r[0].type -ne "file.ok") { throw "file.send failed: $($r[0] | ConvertTo-Json -Compress)" }
    $dst = Join-Path $work "b\downloads\send-me.bin"
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline -and -not (Test-Path $dst)) {
        Start-Sleep -Milliseconds 400
    }
    if (-not (Test-Path $dst)) { throw "file never arrived at Beta downloads" }
    $srcHash = (Get-FileHash $src -Algorithm SHA256).Hash
    $dstHash = (Get-FileHash $dst -Algorithm SHA256).Hash
    if ($srcHash -ne $dstHash) { throw "file content mismatch after transfer" }
    Write-Host "[file] end-to-end OK (4096 bytes, sha256 match)"

    # User-initiated disconnect from Beta side: both sides must drop the link.
    $r = Invoke-Ipc "flowgrid-smoke-b" @('"type":"disconnect.request","payload":{"instance":"Alpha._flowgrid._tcp.local."}}')
    if ($r[0].type -ne "disconnect.ok") { throw "disconnect.request failed" }
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        if ($null -eq (Get-OnlineLink "flowgrid-smoke-a" "Beta") -and
            $null -eq (Get-OnlineLink "flowgrid-smoke-b" "Alpha")) { break }
        Start-Sleep -Milliseconds 400
    }
    if ($null -ne (Get-OnlineLink "flowgrid-smoke-a" "Beta")) { throw "link still online on Alpha after disconnect" }
    Write-Host "[disconnect] both sides dropped the link"

    Write-Host "SMOKE-LINK OK"
}
finally {
    if ($a) { Stop-Process -Id $a.Id -Force -ErrorAction SilentlyContinue }
    if ($b) { Stop-Process -Id $b.Id -Force -ErrorAction SilentlyContinue }
    Write-Host "nodes stopped (logs in windows\.smoke\{a,b}\stderr.log)"
}
