[CmdletBinding()]
param(
    [string]$Player
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"
$work = Join-Path $repo ".tmp\h263-avi-test"
$source = Join-Path $work "source.mkv"

if (-not (Test-Path -LiteralPath $ffmpeg)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
if (-not $Player) {
    $outputDirectory = Join-Path $repo "build\h263-test"
    & (Join-Path $PSScriptRoot "build_windows_player.ps1") `
        -OutputDirectory $outputDirectory
    $Player = Join-Path $outputDirectory "hlvplay.exe"
}
if (-not (Test-Path -LiteralPath $Player)) {
    throw "Windows player is missing: $Player"
}

New-Item -ItemType Directory -Force -Path $work | Out-Null
& $ffmpeg -y -hide_banner -loglevel error `
    -f lavfi -i "testsrc2=size=320x180:rate=30:duration=1" `
    -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1" `
    -map 0:v:0 -map 1:a:0 -c:v ffv1 -c:a pcm_s16le $source
if ($LASTEXITCODE -ne 0) {
    throw "Could not generate the synthetic H.263 input."
}

foreach ($profile in @("176x144", "352x288")) {
    $avi = Join-Path $work "profile-$profile-pcm.avi"
    & (Join-Path $PSScriptRoot "encode_h263_avi.ps1") `
        -InputFile $source `
        -OutputFile $avi `
        -Profile $profile `
        -FitMode Contain
    if ($LASTEXITCODE -ne 0) {
        throw "The $profile H.263/AVI encoder smoke test failed."
    }

    $frameRate = & $ffprobe -v error -select_streams v:0 `
        -show_entries stream=avg_frame_rate `
        -of default=noprint_wrappers=1:nokey=1 $avi
    if ($LASTEXITCODE -ne 0 -or $frameRate -ne "30/1") {
        throw (
            "The $profile output did not preserve the full 30 fps " +
            "source rate."
        )
    }

    $quotedAvi = '"' + $avi.Replace('"', '\"') + '"'
    $playerProcess = Start-Process -FilePath $Player `
        -ArgumentList "--check $quotedAvi" `
        -Wait -PassThru -WindowStyle Hidden
    if ($playerProcess.ExitCode -ne 0) {
        throw "The project decoder rejected the $profile AVI smoke test."
    }
}

$rejectedCustomSize = $false
try {
    & (Join-Path $PSScriptRoot "encode_h263_avi.ps1") `
        -InputFile $source `
        -OutputFile (Join-Path $work "forbidden-custom.avi") `
        -Profile 320x240 `
        -NoAudio
}
catch {
    $rejectedCustomSize = $true
}
if (-not $rejectedCustomSize) {
    throw "The H.263 encoder accepted a non-QCIF/CIF picture size."
}

$rejected3gp = $false
try {
    & (Join-Path $PSScriptRoot "encode_h263_avi.ps1") `
        -InputFile $source `
        -OutputFile (Join-Path $work "forbidden.3gp") `
        -Profile 176x144 `
        -NoAudio
}
catch {
    $rejected3gp = $true
}
if (-not $rejected3gp) {
    throw "The H.263 encoder accepted a non-AVI container."
}

$source60 = Join-Path $work "source-60fps.mkv"
& $ffmpeg -y -hide_banner -loglevel error `
    -f lavfi -i "testsrc2=size=320x180:rate=60:duration=0.2" `
    -c:v ffv1 $source60
if ($LASTEXITCODE -ne 0) {
    throw "Could not generate the 60 fps policy test input."
}
$rejectedHalfRate = $false
try {
    & (Join-Path $PSScriptRoot "encode_h263_avi.ps1") `
        -InputFile $source60 `
        -OutputFile (Join-Path $work "forbidden-half-rate.avi") `
        -Profile 176x144 `
        -NoAudio
}
catch {
    $rejectedHalfRate = $true
}
if (-not $rejectedHalfRate) {
    throw "The H.263 encoder silently reduced a source above 30 fps."
}

Write-Host (
    "Standard H.263 QCIF/CIF AVI tests passed at the full source rate; " +
    "custom sizes, 3GP, and half-rate fallback were rejected."
)
