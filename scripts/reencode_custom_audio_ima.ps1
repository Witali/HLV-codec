#requires -Version 7.4

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string[]]$InputFile,

    [Parameter(Mandatory)]
    [string]$AudioSource,

    [ValidateRange(8000, 48000)]
    [int]$AudioRate = 32000,

    [switch]$Replace,

    [switch]$Force
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot "_audio_normalization.ps1")

function Resolve-ExistingFile {
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "File does not exist: $Path"
    }
    (Resolve-Path -LiteralPath $Path).Path
}

function Set-ReportProperty {
    param(
        [Parameter(Mandatory)][psobject]$Object,
        [Parameter(Mandatory)][string]$Name,
        $Value
    )

    $Object | Add-Member -NotePropertyName $Name `
        -NotePropertyValue $Value -Force
}

$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$tool = Join-Path $repo "build\msvc\custom_audio_ima.exe"
$toolSource = Join-Path $repo "tools\custom_audio_ima.c"
$buildScript = Join-Path $PSScriptRoot "build_custom_audio_ima.ps1"
if (-not (Test-Path -LiteralPath $ffmpeg)) {
    throw "Repository-local FFmpeg is unavailable. Run setup.ps1."
}
if (-not (Test-Path -LiteralPath $tool) -or
    (Get-Item -LiteralPath $tool).LastWriteTimeUtc -lt
        (Get-Item -LiteralPath $toolSource).LastWriteTimeUtc) {
    & $buildScript
}
if (-not (Test-Path -LiteralPath $tool)) {
    throw "The custom audio rewriter is unavailable: $tool"
}

$audioPath = Resolve-ExistingFile $AudioSource
$inputs = @($InputFile | ForEach-Object { Resolve-ExistingFile $_ })
if (-not $inputs.Count) {
    throw "At least one HLV or BPV input is required."
}
if ($inputs.Count -ne @($inputs | Select-Object -Unique).Count) {
    throw "The input list contains duplicate paths."
}
foreach ($inputPath in $inputs) {
    if ([IO.Path]::GetExtension($inputPath) -notin ".hlv", ".bpv1") {
        throw "Only HLV and BPV1 inputs are supported: $inputPath"
    }
}

$temporaryDirectory = Join-Path $repo ".tmp\reencode-custom-audio-ima"
New-Item -ItemType Directory -Force -Path $temporaryDirectory | Out-Null
$identifier = [Guid]::NewGuid().ToString("N")
$temporaryAudio = Join-Path $temporaryDirectory "$identifier.s16le"
$normalization = Get-PeakSafeAudioFilter -Ffmpeg $ffmpeg `
    -InputFile $audioPath -Rate $AudioRate

try {
    Write-AudioNormalizationStatus -Normalization $normalization
    Write-Host "Preparing normalized PCM16 mono $AudioRate Hz once..."
    & $ffmpeg -y -hide_banner -loglevel error -i $audioPath `
        -map 0:a:0 -vn -af $normalization.Filter `
        -ac 1 -ar $AudioRate -f s16le $temporaryAudio
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg audio preparation failed with exit code $LASTEXITCODE."
    }

    foreach ($inputPath in $inputs) {
        $extension = [IO.Path]::GetExtension($inputPath)
        $finalPath = if ($Replace) {
            $inputPath
        } else {
            Join-Path (Split-Path -Parent $inputPath) (
                [IO.Path]::GetFileNameWithoutExtension($inputPath) +
                "_IMAADPCM" + $extension
            )
        }
        if (-not $Replace -and
            (Test-Path -LiteralPath $finalPath -PathType Leaf) -and
            -not $Force) {
            throw "Output already exists; pass -Force: $finalPath"
        }
        $stagePath = Join-Path $temporaryDirectory (
            [IO.Path]::GetFileNameWithoutExtension($inputPath) +
            ".$identifier" + $extension
        )
        try {
            Write-Host "Rewriting audio only: $inputPath"
            $beforeHash = (& $tool --video-sha256 $inputPath).Trim()
            if ($LASTEXITCODE -ne 0 -or
                $beforeHash -notmatch "^[0-9a-f]{64}$") {
                throw "Could not validate the original video stream."
            }
            $rewriteStatus = & $tool $inputPath $stagePath `
                $temporaryAudio $AudioRate
            if ($LASTEXITCODE -ne 0) {
                throw "HLV/BPV audio replacement failed."
            }
            $afterHash = (& $tool --video-sha256 $stagePath).Trim()
            if ($LASTEXITCODE -ne 0 -or $afterHash -ne $beforeHash) {
                throw "Compressed video changed while replacing audio."
            }
            if ($rewriteStatus -notmatch "padded_samples=(\d+)") {
                throw "The audio rewriter returned an invalid status line."
            }
            $paddedSamples = [uint64]$Matches[1]
            Move-Item -LiteralPath $stagePath -Destination $finalPath -Force

            $reportPath = [IO.Path]::ChangeExtension($finalPath, ".json")
            $report = if (Test-Path -LiteralPath $reportPath -PathType Leaf) {
                Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
            } else {
                [pscustomobject]@{}
            }
            if ($report.PSObject.Properties.Name -contains "settings") {
                Set-ReportProperty $report "settings" (
                    ([string]$report.settings) -replace
                        "PCM_U8 mono 16 kHz", "IMA_ADPCM mono 32 kHz"
                )
            }
            Set-ReportProperty $report "output" $finalPath
            Set-ReportProperty $report "audio" `
                "IMA_ADPCM mono $AudioRate Hz"
            Set-ReportProperty $report "audioSource" $audioPath
            Set-ReportProperty $report "audioNormalization" ([ordered]@{
                method = "primaryCompressorPeak"
                curvePeakDb = $normalization.CurvePeakDb
                outputGainDb = $normalization.OutputGainDb
                targetPeakDb = $normalization.TargetPeakDb
            })
            Set-ReportProperty $report "audioPaddedSamples" $paddedSamples
            Set-ReportProperty $report "videoPayloadSha256" $afterHash
            Set-ReportProperty $report "bytes" `
                ((Get-Item -LiteralPath $finalPath).Length)
            Set-ReportProperty $report "generatedUtc" `
                ([DateTime]::UtcNow.ToString("o"))
            $report | ConvertTo-Json -Depth 32 | Set-Content `
                -LiteralPath $reportPath -Encoding utf8
            Write-Host (
                "Ready: $finalPath (video $afterHash, " +
                "padded samples $paddedSamples)"
            )
        } finally {
            if (Test-Path -LiteralPath $stagePath -PathType Leaf) {
                Remove-Item -LiteralPath $stagePath -Force
            }
        }
    }
} finally {
    if (Test-Path -LiteralPath $temporaryAudio -PathType Leaf) {
        Remove-Item -LiteralPath $temporaryAudio -Force
    }
}
