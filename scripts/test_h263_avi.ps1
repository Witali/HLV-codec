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

function Invoke-PlayerCheck {
    param(
        [Parameter(Mandatory)][string]$Executable,
        [Parameter(Mandatory)][string]$MediaFile
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $quotedMediaFile = '"' + $MediaFile.Replace('"', '\"') + '"'
    $startInfo.Arguments = "--check $quotedMediaFile"
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    [pscustomobject]@{
        ExitCode = $process.ExitCode
        Output = ($stdout + $stderr).Trim()
    }
}

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

    $check = Invoke-PlayerCheck -Executable $Player -MediaFile $avi
    if ($check.ExitCode -ne 0) {
        throw (
            "The project decoder rejected the $profile AVI smoke test. " +
            $check.Output
        )
    }
    if ($profile -eq "352x288" -and
        $check.Output -notmatch "checksum 4e31541b0204ec7e") {
        throw (
            "The CIF pixel checksum changed while decoding packets larger " +
            "than the streaming input buffer. " + $check.Output
        )
    }
}

# The ESP32 presents the center 320x240 pixels of CIF. Verify that the
# transcoding profile reserves exactly the surrounding 16/24-pixel guard area.
$centeredCif = Join-Path $work "centered-cif.avi"
& (Join-Path $PSScriptRoot "encode_h263_avi.ps1") `
    -InputFile $source `
    -OutputFile $centeredCif `
    -Profile 352x288 `
    -FitMode Crop `
    -NoAudio `
    -MaxFrames 1
if ($LASTEXITCODE -ne 0) {
    throw "The centered CIF profile encoding failed."
}
$borderStats = (
    & $ffmpeg -hide_banner -loglevel info -i $centeredCif `
        -vf "crop=16:288:0:0,signalstats,metadata=print" `
        -frames:v 1 -f null NUL 2>&1 | Out-String
)
if ($borderStats -notmatch "lavfi\.signalstats\.YAVG=(1[456]|1[456]\.\d+)") {
    throw "The CIF profile no longer has a black 16-pixel left guard area."
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
    "CIF is centered for a 320x240 display; custom sizes, 3GP, and " +
    "half-rate fallback were rejected."
)
