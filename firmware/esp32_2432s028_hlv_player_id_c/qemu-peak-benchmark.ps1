[CmdletBinding()]
param(
    [string]$InputFile = "",
    [int]$StartFrame = -1,
    [ValidateRange(1, 120)][int]$Frames = 30,
    [ValidateSet(32, 64)][int]$BitReaderBits = 32,
    [ValidateSet("O2", "O3", "Os")][string]$Optimization = "O3"
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $project "..\..")).Path
if (-not $InputFile) {
    $InputFile = Join-Path $repo "out\video.hlv"
}
$InputFile = (Resolve-Path -LiteralPath $InputFile).Path
if ($StartFrame -lt -1) {
    throw "StartFrame must be -1 for automatic selection or a keyframe index."
}

if ($StartFrame -eq -1) {
    $peakTool = Join-Path $repo "build\msvc\hlvpeakdec.exe"
    if (-not (Test-Path -LiteralPath $peakTool)) {
        & (Join-Path $repo "scripts\build_msvc.ps1")
    }
    $peakOutput = @(& $peakTool $InputFile)
    if ($LASTEXITCODE -ne 0) {
        throw "hlvpeakdec failed with exit code $LASTEXITCODE."
    }
    $peakRow = $peakOutput |
        Where-Object { $_ -match '^\d+,\d+,\d+,[KP],\d+,\d+$' } |
        Select-Object -First 1
    if (-not $peakRow) {
        throw "hlvpeakdec did not return a peak frame."
    }
    $columns = $peakRow.Split(",")
    $StartFrame = [int]$columns[2]
    Write-Host (
        "Peak candidate frame {0}; GOP starts at {1} (host score {2})." -f
        $columns[1], $StartFrame, $columns[5])
}

$temporaryDirectory = Join-Path $repo ".tmp"
[IO.Directory]::CreateDirectory($temporaryDirectory) | Out-Null
$temporaryClip = Join-Path $temporaryDirectory (
    "qemu-peak-{0}.hlv" -f [guid]::NewGuid().ToString("N"))
try {
    & (Join-Path $project "prepare-qemu-peak-clip.ps1") `
        -InputFile $InputFile -StartFrame $StartFrame -Frames $Frames `
        -OutputFile $temporaryClip
    & (Join-Path $project "qemu-benchmark.ps1") `
        -InputFile $temporaryClip -Frames $Frames -Windows 1 `
        -BitReaderBits $BitReaderBits -Optimization $Optimization
} finally {
    if (Test-Path -LiteralPath $temporaryClip) {
        Remove-Item -LiteralPath $temporaryClip -Force
    }
}
