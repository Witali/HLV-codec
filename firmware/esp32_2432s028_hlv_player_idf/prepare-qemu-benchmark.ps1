[CmdletBinding()]
param(
    [string]$InputFile = "",
    [ValidateRange(1, 10000)]
    [int]$Frames = 120
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $project "..\..")).Path
if (-not $InputFile) {
    $InputFile = Join-Path $repo "out\video.hlv"
}
$InputFile = (Resolve-Path -LiteralPath $InputFile).Path
$outputFile = Join-Path $project "main\qemu_benchmark.hlv"

function Read-Exact {
    param(
        [Parameter(Mandatory)][System.IO.Stream]$Stream,
        [Parameter(Mandatory)][byte[]]$Buffer,
        [Parameter(Mandatory)][int]$Count
    )
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($Buffer, $offset, $Count - $offset)
        if ($read -le 0) {
            throw "Unexpected end of HLV file."
        }
        $offset += $read
    }
}

$input = [System.IO.File]::OpenRead($InputFile)
$output = $null
try {
    $header = New-Object byte[] 28
    Read-Exact -Stream $input -Buffer $header -Count $header.Length
    if ([Text.Encoding]::ASCII.GetString($header, 0, 4) -ne "HLV1") {
        throw "Input is not an HLV1 file."
    }
    $sourceFrames = [BitConverter]::ToUInt32($header, 14)
    $clipFrames = [Math]::Min([uint32]$Frames, $sourceFrames)
    [BitConverter]::GetBytes([uint32]$clipFrames).CopyTo($header, 14)

    $output = [System.IO.File]::Create($outputFile)
    $output.Write($header, 0, $header.Length)
    $packetHeader = New-Object byte[] 20
    $copyBuffer = New-Object byte[] 65536
    for ($frame = 0; $frame -lt $clipFrames; ++$frame) {
        Read-Exact -Stream $input -Buffer $packetHeader -Count $packetHeader.Length
        if ([Text.Encoding]::ASCII.GetString($packetHeader, 0, 4) -ne "FRM1") {
            throw "Bad frame marker at packet $frame."
        }
        $payloadBytes = [BitConverter]::ToUInt32($packetHeader, 12)
        if ($payloadBytes -gt 61440) {
            throw "Packet $frame exceeds the ESP32 61440-byte pool."
        }
        $output.Write($packetHeader, 0, $packetHeader.Length)
        $remaining = [uint64]$payloadBytes
        while ($remaining -gt 0) {
            $chunk = [int][Math]::Min([uint64]$copyBuffer.Length, $remaining)
            Read-Exact -Stream $input -Buffer $copyBuffer -Count $chunk
            $output.Write($copyBuffer, 0, $chunk)
            $remaining -= $chunk
        }
    }
} finally {
    if ($output) { $output.Dispose() }
    $input.Dispose()
}

$size = (Get-Item -LiteralPath $outputFile).Length
Write-Host "Prepared QEMU benchmark clip: $clipFrames frames, $size bytes"
