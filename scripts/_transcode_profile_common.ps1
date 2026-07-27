#requires -Version 7.4

function Get-TranscodeVideoInfo {
    param(
        [Parameter(Mandatory)]
        [string]$InputFile
    )

    $resolvedInput = (Resolve-Path -LiteralPath $InputFile).Path
    $repo = Split-Path $PSScriptRoot -Parent
    $approvedBunnySource = Join-Path $repo (
        "out\sources\big_buck_bunny_1080p_h264\" +
        "big_buck_bunny_1080p_h264.mov"
    )
    if ($resolvedInput -match "(?i)big[_ .-]*buck|bigbuckbunny" -and
        -not $resolvedInput.Equals(
            [IO.Path]::GetFullPath($approvedBunnySource),
            [StringComparison]::OrdinalIgnoreCase
        )) {
        throw (
            "Big Buck Bunny must use the approved 1080p MOV source: " +
            $approvedBunnySource
        )
    }
    $ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"
    if (-not (Test-Path -LiteralPath $ffprobe)) {
        & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
    }
    if (-not (Test-Path -LiteralPath $ffprobe)) {
        throw "Repository-local FFprobe is unavailable."
    }

    $probeText = & $ffprobe -v error -select_streams v:0 `
        -show_entries stream=width,height,r_frame_rate `
        -of json $resolvedInput
    if ($LASTEXITCODE -ne 0) {
        throw "FFprobe could not inspect: $resolvedInput"
    }
    $streams = @(($probeText | ConvertFrom-Json).streams)
    if ($streams.Count -ne 1) {
        throw "Input must contain one primary video stream."
    }
    $rate = [string]$streams[0].r_frame_rate
    if ($rate -notmatch "^(\d+)/(\d+)$" -or [double]$Matches[2] -eq 0.0) {
        throw "Input has an invalid nominal frame rate: $rate"
    }
    $fps = [double]$Matches[1] / [double]$Matches[2]
    [pscustomobject]@{
        InputFile = $resolvedInput
        BaseName = [IO.Path]::GetFileNameWithoutExtension($resolvedInput)
        Width = [int]$streams[0].width
        Height = [int]$streams[0].height
        Rate = $rate
        Fps = $fps
    }
}

function Format-TranscodeNumber {
    param([Parameter(Mandatory)][double]$Value)

    $culture = [Globalization.CultureInfo]::InvariantCulture
    if ([Math]::Abs($Value - [Math]::Round($Value)) -lt 0.0005) {
        return [Math]::Round($Value).ToString("0", $culture)
    }
    return $Value.ToString("0.###", $culture).Replace(".", "p")
}

function Get-TranscodeOutputFile {
    param(
        [string]$OutputFile,
        [Parameter(Mandatory)][string]$CodecDirectory,
        [Parameter(Mandatory)][string]$FileName
    )

    $repo = Split-Path $PSScriptRoot -Parent
    if (-not $OutputFile) {
        $OutputFile = Join-Path $repo "out\$CodecDirectory\$FileName"
    }
    return [IO.Path]::GetFullPath($OutputFile)
}

function Assert-TranscodeOutput {
    param(
        [Parameter(Mandatory)][string]$OutputFile,
        [switch]$Force
    )

    if ((Test-Path -LiteralPath $OutputFile) -and -not $Force) {
        throw "Output already exists; use -Force to replace it: $OutputFile"
    }
    New-Item -ItemType Directory -Force -Path (
        Split-Path $OutputFile -Parent
    ) | Out-Null
}

function Get-Esp32PreparationFilter {
    param(
        [Parameter(Mandatory)][int]$Width,
        [Parameter(Mandatory)][int]$Height,
        [ValidateSet("Stretch", "Crop")][string]$ResizeMode,
        [double]$Fps = 0
    )

    $parts = [Collections.Generic.List[string]]::new()
    if ($Fps -gt 0) {
        $fpsText = $Fps.ToString(
            "0.########",
            [Globalization.CultureInfo]::InvariantCulture
        )
        $parts.Add("fps=${fpsText}:start_time=0")
    }
    $parts.Add("gblur=sigma=1:steps=2")
    if ($ResizeMode -eq "Crop") {
        $parts.Add(
            "crop='trunc(min(iw\,ih*4/3)/2)*2':" +
            "'trunc(min(ih\,iw*3/4)/2)*2':(iw-ow)/2:(ih-oh)/2"
        )
    }
    $parts.Add(
        "scale=${Width}:${Height}:" +
        "flags=area+accurate_rnd+full_chroma_int"
    )
    $parts.Add("setsar=1")
    $parts.Add("format=yuv420p")
    return $parts -join ","
}
