[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InputFile,
    [Parameter(Mandatory)][ValidateRange(0, 2147483647)]
    [int]$StartFrame,
    [ValidateRange(1, 120)][int]$Frames = 30,
    [Parameter(Mandatory)][string]$OutputFile
)

$ErrorActionPreference = "Stop"
$InputFile = (Resolve-Path -LiteralPath $InputFile).Path
$OutputFile = [IO.Path]::GetFullPath($OutputFile)
$packetPoolBytes = 9 * 7680

function Read-Exact {
    param(
        [Parameter(Mandatory)][System.IO.Stream]$Stream,
        [Parameter(Mandatory)][byte[]]$Buffer,
        [Parameter(Mandatory)][int]$Count
    )
    $offset = 0
    while ($offset -lt $Count) {
        $read = $Stream.Read($Buffer, $offset, $Count - $offset)
        if ($read -le 0) { throw "Unexpected end of HLV file." }
        $offset += $read
    }
}

$input = [IO.File]::OpenRead($InputFile)
$output = $null
try {
    $header = [byte[]]::new(28)
    Read-Exact -Stream $input -Buffer $header -Count $header.Length
    if ([Text.Encoding]::ASCII.GetString($header, 0, 4) -ne "HLV1") {
        throw "Input is not an HLV1 file."
    }
    $sourceFrames = [BitConverter]::ToUInt32($header, 14)
    if ($StartFrame -ge $sourceFrames) {
        throw "Start frame is outside the stream."
    }

    $packetOffsets = [Collections.Generic.List[long]]::new()
    $packetSizes = [Collections.Generic.List[uint32]]::new()
    $packetTypes = [Collections.Generic.List[byte]]::new()
    $packetHeader = [byte[]]::new(20)
    for ($frame = 0; $frame -lt $sourceFrames; ++$frame) {
        $packetOffsets.Add($input.Position)
        Read-Exact -Stream $input -Buffer $packetHeader `
            -Count $packetHeader.Length
        if ([Text.Encoding]::ASCII.GetString($packetHeader, 0, 4) -ne
            "FRM1") {
            throw "Bad frame marker at packet $frame."
        }
        $payloadBytes = [BitConverter]::ToUInt32($packetHeader, 12)
        if ($payloadBytes -gt $packetPoolBytes) {
            throw "Packet $frame exceeds the ESP32 packet pool."
        }
        $packetSizes.Add($payloadBytes)
        $packetTypes.Add($packetHeader[4])
        $next = $input.Seek($payloadBytes, [IO.SeekOrigin]::Current)
        if ($next -gt $input.Length) {
            throw "Packet $frame extends past the end of the HLV file."
        }
    }
    if ($packetTypes[$StartFrame] -ne 0) {
        throw "Frame $StartFrame is not a keyframe."
    }

    $clipFrames = [Math]::Min($Frames, [int]$sourceFrames - $StartFrame)
    [BitConverter]::GetBytes([uint32]$clipFrames).CopyTo($header, 14)
    $outputParent = Split-Path $OutputFile -Parent
    if ($outputParent) {
        [IO.Directory]::CreateDirectory($outputParent) | Out-Null
    }
    $output = [IO.File]::Create($OutputFile)
    $output.Write($header, 0, $header.Length)
    $buffer = [byte[]]::new(65536)
    for ($index = 0; $index -lt $clipFrames; ++$index) {
        $sourceFrame = $StartFrame + $index
        $input.Position = $packetOffsets[$sourceFrame]
        $remaining = [uint64]$packetSizes[$sourceFrame] + 20U
        while ($remaining -gt 0) {
            $chunk = [int][Math]::Min(
                [uint64]$buffer.Length, $remaining)
            Read-Exact -Stream $input -Buffer $buffer -Count $chunk
            $output.Write($buffer, 0, $chunk)
            $remaining -= $chunk
        }
    }
} finally {
    if ($output) { $output.Dispose() }
    $input.Dispose()
}

$size = (Get-Item -LiteralPath $OutputFile).Length
Write-Host "Prepared source frames $StartFrame-$(
    $StartFrame + $clipFrames - 1): $size bytes"
