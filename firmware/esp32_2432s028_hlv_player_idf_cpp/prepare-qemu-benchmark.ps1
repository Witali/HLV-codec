[CmdletBinding()]
param(
    [string]$InputFile = "",
    [ValidateRange(1, 10000)]
    [int]$Frames = 120,
    [ValidateRange(1, 32)]
    [int]$Windows = 4
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $project "..\..")).Path
$decoderRefillBytes = 7680
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
    $packetOffsets = [Collections.Generic.List[long]]::new()
    $packetSizes = [Collections.Generic.List[uint32]]::new()
    $packetTypes = [Collections.Generic.List[byte]]::new()
    $packetHeader = New-Object byte[] 20
    [uint32]$maximumPacketBytes = 0
    for ($frame = 0; $frame -lt $sourceFrames; ++$frame) {
        $packetOffsets.Add($input.Position)
        Read-Exact -Stream $input -Buffer $packetHeader -Count $packetHeader.Length
        if ([Text.Encoding]::ASCII.GetString($packetHeader, 0, 4) -ne "FRM1") {
            throw "Bad frame marker at packet $frame."
        }
        $payloadBytes = [BitConverter]::ToUInt32($packetHeader, 12)
        if ($payloadBytes -gt $maximumPacketBytes) {
            $maximumPacketBytes = $payloadBytes
        }
        $packetSizes.Add($payloadBytes)
        $packetTypes.Add($packetHeader[4])
        $next = $input.Seek($payloadBytes, [IO.SeekOrigin]::Current)
        if ($next -gt $input.Length) {
            throw "Packet $frame extends past the end of the HLV file."
        }
    }

    $selected = [Collections.Generic.List[int]]::new()
    $ranges = [Collections.Generic.List[string]]::new()
    $windowCount = [Math]::Min([int]$clipFrames, $Windows)
    $baseLength = [Math]::Floor([int]$clipFrames / $windowCount)
    $extraFrames = [int]$clipFrames % $windowCount
    for ($window = 0; $window -lt $windowCount; ++$window) {
        $windowLength = $baseLength + $(if ($window -lt $extraFrames) { 1 } else { 0 })
        $target = [Math]::Floor($window * [double]$sourceFrames / $windowCount)
        $start = [int]$target
        while ($start -lt $sourceFrames -and $packetTypes[$start] -ne 0) {
            ++$start
        }
        if ($start + $windowLength -gt $sourceFrames) {
            $start = [Math]::Max(0, [int]$sourceFrames - $windowLength)
            while ($start -gt 0 -and $packetTypes[$start] -ne 0) {
                --$start
            }
        }
        if ($packetTypes[$start] -ne 0) {
            throw "Cannot find a keyframe for benchmark window $window."
        }
        for ($frame = 0; $frame -lt $windowLength; ++$frame) {
            $selected.Add($start + $frame)
        }
        $ranges.Add("$start-$($start + $windowLength - 1)")
    }

    [BitConverter]::GetBytes([uint32]$selected.Count).CopyTo($header, 14)
    $output = [System.IO.File]::Create($outputFile)
    $output.Write($header, 0, $header.Length)
    $copyBuffer = New-Object byte[] 65536
    foreach ($frame in $selected) {
        $input.Position = $packetOffsets[$frame]
        $remaining = [uint64]$packetSizes[$frame] + 20U
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
Write-Host "Prepared QEMU benchmark clip: $($selected.Count) frames, $size bytes"
Write-Host "Source frame windows: $($ranges -join ', ')"
Write-Host "Largest source packet: $maximumPacketBytes bytes (decoder refill: $decoderRefillBytes bytes)"
