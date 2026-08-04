#requires -Version 7.4

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputFile,

    [Parameter(Mandatory)]
    [string]$AudioSource,

    [string]$OutputFile,

    [string]$ReportFile,

    [switch]$Replace,

    [switch]$Force
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "_audio_normalization.ps1")

function Resolve-ExistingFile {
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "File does not exist: $Path"
    }
    (Resolve-Path -LiteralPath $Path).Path
}

function Get-VideoPayloadHash {
    param(
        [Parameter(Mandatory)][string]$Ffmpeg,
        [Parameter(Mandatory)][string]$Path
    )

    $hashOutput = & $Ffmpeg -hide_banner -loglevel error -i $Path `
        -map 0:v:0 -c:v copy -f hash -hash sha256 - 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Could not hash the compressed video payload: $Path"
    }
    $match = [regex]::Match(($hashOutput -join "`n"), "SHA256=([0-9a-fA-F]+)")
    if (-not $match.Success) {
        throw "FFmpeg did not return a video payload hash for: $Path"
    }
    $match.Groups[1].Value.ToUpperInvariant()
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
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
    throw "Repository-local FFmpeg tools are unavailable. Run setup.ps1."
}

$inputPath = Resolve-ExistingFile $InputFile
$audioPath = Resolve-ExistingFile $AudioSource
if ($Replace -and $OutputFile) {
    throw "Use either -Replace or -OutputFile, not both."
}
if ($Replace) {
    $finalPath = $inputPath
} elseif ($OutputFile) {
    $finalPath = [IO.Path]::GetFullPath($OutputFile)
} else {
    $directory = Split-Path -Parent $inputPath
    $stem = [IO.Path]::GetFileNameWithoutExtension($inputPath)
    $finalPath = Join-Path $directory "${stem}_IMAADPCM.avi"
}
if ([IO.Path]::GetExtension($finalPath) -ne ".avi") {
    throw "The output must use the .avi extension: $finalPath"
}
if (-not $Replace -and
    (Test-Path -LiteralPath $finalPath -PathType Leaf) -and
    -not $Force) {
    throw "Output already exists; pass -Force to overwrite it: $finalPath"
}

$finalDirectory = Split-Path -Parent $finalPath
if (-not (Test-Path -LiteralPath $finalDirectory -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $finalDirectory | Out-Null
}
$stageDirectory = Join-Path $repo ".tmp\reencode-avi-audio-ima"
New-Item -ItemType Directory -Force -Path $stageDirectory | Out-Null
$stagePath = Join-Path $stageDirectory (
    ([IO.Path]::GetFileNameWithoutExtension($inputPath)) +
    "." + [Guid]::NewGuid().ToString("N") + ".avi"
)

$audioRate = Get-PrimaryAudioSampleRate -Ffprobe $ffprobe `
    -InputFile $audioPath
$normalization = Get-PeakSafeAudioFilter -Ffmpeg $ffmpeg `
    -InputFile $audioPath -Rate $audioRate
Write-AudioNormalizationStatus -Normalization $normalization
$beforeHash = Get-VideoPayloadHash -Ffmpeg $ffmpeg -Path $inputPath

try {
    Write-Host "Copying video and encoding normalized source audio as WAV IMA ADPCM..."
    & $ffmpeg -hide_banner -loglevel warning -y `
        -i $inputPath -i $audioPath `
        -map 0:v:0 -map 1:a:0 -map_metadata 0 `
        -c:v copy `
        -af $normalization.Filter -c:a adpcm_ima_wav -ac 1 -ar $audioRate `
        -f avi $stagePath
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg AVI audio re-encode failed with exit code $LASTEXITCODE."
    }

    $probeText = & $ffprobe -v error -show_streams -show_format `
        -of json $stagePath
    if ($LASTEXITCODE -ne 0) {
        throw "FFprobe rejected the staged AVI."
    }
    $probe = $probeText | ConvertFrom-Json
    $video = @($probe.streams | Where-Object codec_type -eq "video")
    $audio = @($probe.streams | Where-Object codec_type -eq "audio")
    if ($video.Count -ne 1 -or $audio.Count -ne 1) {
        throw "The staged AVI must contain exactly one video and one audio stream."
    }
    if ($audio[0].codec_name -ne "adpcm_ima_wav" -or
        [int]$audio[0].sample_rate -ne $audioRate -or
        [int]$audio[0].channels -ne 1 -or
        [int]$audio[0].bits_per_sample -ne 4) {
        throw "The staged AVI does not preserve the source WAV IMA sample rate."
    }
    # FFmpeg's AVI muxer writes one complete WAVE IMA block per audio packet.
    # Inspect every packet so the report and player limit are based on the
    # actual encoded block size rather than an unavailable ffprobe field.
    $audioPacketLines = & $ffprobe -v error -select_streams a:0 `
        -show_entries packet=size -of csv=p=0 $stagePath
    if ($LASTEXITCODE -ne 0) {
        throw "Could not inspect staged WAV IMA packet sizes."
    }
    $audioPacketSizes = @(
        $audioPacketLines |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object { [int]$_.Trim() }
    )
    if (-not $audioPacketSizes.Count) {
        throw "The staged AVI contains no WAV IMA blocks."
    }
    $audioBlockAlign = $audioPacketSizes[0]
    if ($audioBlockAlign -lt 4 -or $audioBlockAlign -gt 2048 -or
        @($audioPacketSizes | Where-Object { $_ -ne $audioBlockAlign }).Count) {
        throw "The staged WAV IMA packets do not match one bounded block size."
    }

    $afterHash = Get-VideoPayloadHash -Ffmpeg $ffmpeg -Path $stagePath
    if ($afterHash -ne $beforeHash) {
        throw "Compressed video changed while replacing the audio stream."
    }

    & $ffmpeg -hide_banner -loglevel error -i $stagePath `
        -map 0:v:0 -map 0:a:0 -f null NUL
    if ($LASTEXITCODE -ne 0) {
        throw "Complete staged AVI decode failed."
    }

    Move-Item -LiteralPath $stagePath -Destination $finalPath -Force

    if (-not $ReportFile) {
        $ReportFile = [IO.Path]::ChangeExtension($finalPath, ".json")
    }
    if (Test-Path -LiteralPath $ReportFile -PathType Leaf) {
        $report = Get-Content -LiteralPath $ReportFile -Raw | ConvertFrom-Json
    } else {
        $report = [pscustomobject]@{}
    }
    if ($report.PSObject.Properties.Name -contains "settings") {
        $settings = [string]$report.settings
        $settings = $settings -replace "PCM_U8 mono 16 kHz", (
            "WAV IMA ADPCM mono $audioRate Hz"
        )
        Set-ReportProperty $report "settings" $settings
    }
    Set-ReportProperty $report "output" $finalPath
    Set-ReportProperty $report "audio" "IMA_ADPCM_WAV mono $audioRate Hz"
    Set-ReportProperty $report "audioSource" $audioPath
    Set-ReportProperty $report "audioNormalization" ([ordered]@{
        method = "primaryCompressorPeak"
        curvePeakDb = $normalization.CurvePeakDb
        outputGainDb = $normalization.OutputGainDb
        targetPeakDb = $normalization.TargetPeakDb
    })
    Set-ReportProperty $report "audioBlockAlignBytes" $audioBlockAlign
    Set-ReportProperty $report "videoPayloadSha256" $afterHash
    Set-ReportProperty $report "bytes" ((Get-Item -LiteralPath $finalPath).Length)
    Set-ReportProperty $report "generatedUtc" ([DateTime]::UtcNow.ToString("o"))
    $report | ConvertTo-Json -Depth 32 | Set-Content `
        -LiteralPath $ReportFile -Encoding utf8

    Write-Host "Ready: $finalPath"
    Write-Host "Video payload preserved: $afterHash"
    Write-Host "Report: $ReportFile"
} finally {
    if (Test-Path -LiteralPath $stagePath -PathType Leaf) {
        Remove-Item -LiteralPath $stagePath -Force
    }
}
