#requires -Version 7.4

[CmdletBinding()]
param(
    [string]$OutputRoot,

    [switch]$ListOnly
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repo "out"
}
$outputPath = [IO.Path]::GetFullPath($OutputRoot)
$converter = Join-Path $PSScriptRoot "reencode_avi_audio_ima.ps1"
if (-not (Test-Path -LiteralPath $converter -PathType Leaf)) {
    throw "The single-file AVI audio converter is missing: $converter"
}

$sourceByPrefix = [ordered]@{
    "BigBuckBunny_" = Join-Path $outputPath (
        "sources\big_buck_bunny_1080p_h264\" +
        "big_buck_bunny_1080p_h264.mov"
    )
    "Danila_" = Join-Path $outputPath "sources\VID_20260522_181611.mp4"
    "VideoFormatRegression_" = Join-Path $outputPath (
        "sources\VideoFormatRegression.mkv"
    )
}

$jobs = @()
foreach ($format in @("H263", "MPEG4SP")) {
    $directory = Join-Path $outputPath $format
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        throw "Production AVI directory is missing: $directory"
    }
    foreach ($file in Get-ChildItem -LiteralPath $directory -Filter "*.avi" -File) {
        $audioSource = $null
        foreach ($entry in $sourceByPrefix.GetEnumerator()) {
            if ($file.Name.StartsWith(
                    $entry.Key, [StringComparison]::OrdinalIgnoreCase)) {
                $audioSource = $entry.Value
                break
            }
        }
        if (-not $audioSource) {
            throw "No source-audio mapping exists for: $($file.FullName)"
        }
        if (-not (Test-Path -LiteralPath $audioSource -PathType Leaf)) {
            throw "Mapped source audio is missing: $audioSource"
        }
        $jobs += [pscustomobject]@{
            Format = $format
            Avi = $file.FullName
            AudioSource = $audioSource
        }
    }
}
$jobs = @($jobs | Sort-Object Format, Avi)
if (-not $jobs.Count) {
    throw "No production H.263 or MPEG-4 SP AVI files were found."
}

if ($ListOnly) {
    $jobs | Format-Table Format, Avi, AudioSource -AutoSize
    Write-Host "Mapped $($jobs.Count) production AVI files. No files changed."
    return
}

$completed = 0
foreach ($job in $jobs) {
    Write-Host "=== $($job.Avi) ==="
    & $converter -InputFile $job.Avi `
        -AudioSource $job.AudioSource -Replace
    ++$completed
}
Write-Host "Re-encoded normalized IMA ADPCM audio in $completed AVI files."
