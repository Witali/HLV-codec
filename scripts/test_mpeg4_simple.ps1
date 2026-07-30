#requires -Version 7.4

[CmdletBinding()]
param(
    [string]$OutputDirectory = (
        Join-Path (Split-Path $PSScriptRoot -Parent) "build\mpeg4-simple-test"
    )
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$source = Join-Path $OutputDirectory "source.avi"
$encoded = Join-Path $OutputDirectory "mpeg4-simple.avi"

& $ffmpeg -y -v error `
    -f lavfi -i "testsrc2=size=640x360:rate=30:duration=2" `
    -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=2" `
    -c:v ffv1 -c:a pcm_s16le -shortest $source
if ($LASTEXITCODE -ne 0) {
    throw "Could not create the MPEG-4 SP test source."
}

& (Join-Path $PSScriptRoot "encode_mpeg4_simple_avi.ps1") `
    -InputFile $source `
    -OutputFile $encoded `
    -VideoQuality 3 `
    -MaxFrames 60

$headerLength = [Math]::Min(
    [int64]65536,
    (Get-Item -LiteralPath $encoded).Length
)
$headerBytes = [byte[]]::new([int]$headerLength)
$headerStream = [IO.File]::OpenRead($encoded)
try {
    $bytesRead = $headerStream.Read($headerBytes, 0, $headerBytes.Length)
}
finally {
    $headerStream.Dispose()
}
$videoStrfSize = $null
for ($i = 0; $i -le $bytesRead - 8; ++$i) {
    if ($headerBytes[$i] -eq 0x73 -and
        $headerBytes[$i + 1] -eq 0x74 -and
        $headerBytes[$i + 2] -eq 0x72 -and
        $headerBytes[$i + 3] -eq 0x66) {
        $videoStrfSize = [BitConverter]::ToUInt32($headerBytes, $i + 4)
        break
    }
}
if ($null -eq $videoStrfSize -or $videoStrfSize -le 40) {
    throw "MPEG-4 VOL configuration is missing from the AVI video strf."
}

$packetsText = & $ffprobe -v error -select_streams v:0 `
    -show_entries packet=size -show_packets -of json $encoded
if ($LASTEXITCODE -ne 0) {
    throw "Could not inspect MPEG-4 packet sizes."
}
$packetSizes = @(($packetsText | ConvertFrom-Json).packets |
    ForEach-Object { [int]$_.size })
if (-not ($packetSizes | Where-Object { $_ -gt 4096 })) {
    throw "Test clip has no valid video packet larger than the 4 KiB refill."
}

$streamBuild = Join-Path $OutputDirectory "stream-build"
$contiguousBuild = Join-Path $OutputDirectory "contiguous-build"
& (Join-Path $PSScriptRoot "build_windows_player.ps1") `
    -OutputDirectory $streamBuild
& (Join-Path $PSScriptRoot "build_windows_player.ps1") `
    -OutputDirectory $contiguousBuild `
    -H263PacketBufferBytes 262144

$streamResult = & cmd.exe /d /c (
    '"{0}" --check "{1}"' -f
    (Join-Path $streamBuild "hlvplay.exe"),
    $encoded
)
if ($LASTEXITCODE -ne 0) {
    throw "The 4 KiB streaming decoder rejected the MPEG-4 SP clip."
}
$contiguousResult = & cmd.exe /d /c (
    '"{0}" --check "{1}"' -f
    (Join-Path $contiguousBuild "hlvplay.exe"),
    $encoded
)
if ($LASTEXITCODE -ne 0) {
    throw "The contiguous-input decoder rejected the MPEG-4 SP clip."
}
$checksumPattern = "checksum\s+([0-9a-fA-F]+)"
$streamText = $streamResult -join "`n"
$contiguousText = $contiguousResult -join "`n"
if ($streamText -notmatch $checksumPattern) {
    throw "Streaming decoder output did not contain a checksum."
}
$streamChecksum = $Matches[1]
if ($streamText -notmatch "MPEG-4 SP/AVI check OK") {
    throw "Streaming decoder did not identify the clip as MPEG-4 SP/AVI."
}
if ($contiguousText -notmatch $checksumPattern) {
    throw "Contiguous decoder output did not contain a checksum."
}
$contiguousChecksum = $Matches[1]
if ($streamChecksum -ne $contiguousChecksum) {
    throw (
        "Streaming checksum $streamChecksum differs from contiguous " +
        "checksum $contiguousChecksum."
    )
}

Write-Host $streamResult
Write-Host (
    (
        "MPEG-4 SP streaming test passed: max packet {0:N0} bytes, " +
        "matching checksum {1}."
    ) -f
    ($packetSizes | Measure-Object -Maximum).Maximum,
    $streamChecksum
)
