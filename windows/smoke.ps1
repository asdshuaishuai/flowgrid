# IPC smoke against fgcore: hello -> devices.list -> status.get (specs/windows-ipc).
# Prerequisite: fgcore is running with the default pipe name. Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File windows\smoke.ps1
# NOTE: keep this script ASCII-only; PowerShell 5.1 reads BOM-less files as GBK.
$ErrorActionPreference = "Stop"

$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
    ".", "flowgrid-core", [System.IO.Pipes.PipeDirection]::InOut)
$pipe.Connect(5000)

$reader = [System.IO.StreamReader]::new($pipe)
$writer = [System.IO.StreamWriter]::new($pipe)
$writer.AutoFlush = $true

function Recv([string]$label, [string]$wantType, [string]$wantId) {
    $line = $reader.ReadLine()
    if (-not $line) { throw "[$label] no reply" }
    $env = $line | ConvertFrom-Json
    if ($env.type -ne $wantType) { throw "[$label] got type=$($env.type), want $wantType" }
    if ($env.id -ne $wantId) { throw "[$label] got id=$($env.id), want $wantId" }
    Write-Host "[$label] $line"
}

$writer.WriteLine('{"v":1,"id":"1","type":"hello"}');        Recv "hello"   "hello.ok" "1"
$writer.WriteLine('{"v":1,"id":"2","type":"devices.list"}'); Recv "devices" "devices"  "2"
$writer.WriteLine('{"v":1,"id":"3","type":"status.get"}');   Recv "status"  "status"   "3"

# reader/writer own the pipe: disposing either closes it, so dispose the pipe only.
$pipe.Dispose()
Write-Host "SMOKE OK"
