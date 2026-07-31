#requires -Version 7.4

<#
.SYNOPSIS
Reports decoder-relevant structure for an MPEG-4 Simple Profile AVI.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$InputFile,

    [string]$ReportFile,

    [string]$Player
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"
$InputFile = [IO.Path]::GetFullPath($InputFile)
if (-not (Test-Path -LiteralPath $InputFile)) {
    throw "Input video is missing: $InputFile"
}
if (-not (Test-Path -LiteralPath $ffprobe)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
if (-not $Player) {
    $analysisBuild = Join-Path $repo "build\mpeg4-analysis-player"
    & (Join-Path $PSScriptRoot "build_windows_player.ps1") `
        -OutputDirectory $analysisBuild `
        -H263StageProfile
    $Player = Join-Path $analysisBuild "hlvplay.exe"
}
$Player = [IO.Path]::GetFullPath($Player)
if (-not (Test-Path -LiteralPath $Player)) {
    throw "Analysis player is missing: $Player"
}
if (-not $ReportFile) {
    $ReportFile = [IO.Path]::ChangeExtension(
        $InputFile, ".mpeg4-analysis.json")
}
$ReportFile = [IO.Path]::GetFullPath($ReportFile)

$quotedPlayer = '"' + $Player.Replace('"', '""') + '"'
$quotedInput = '"' + $InputFile.Replace('"', '""') + '"'
$decoderText = & cmd.exe /d /c (
    "$quotedPlayer --analyze-mpeg4 $quotedInput"
)
if ($LASTEXITCODE -ne 0) {
    throw "The complete decoder analysis failed."
}
$decoder = ($decoderText -join "`n") | ConvertFrom-Json

$frameText = & $ffprobe -v error -select_streams v:0 `
    -show_entries frame=pict_type,pkt_size -show_frames -of json $InputFile
if ($LASTEXITCODE -ne 0) {
    throw "FFprobe could not inspect MPEG-4 picture sizes."
}
$pictures = @(($frameText | ConvertFrom-Json).frames)
$iPictures = @($pictures | Where-Object { $_.pict_type -eq "I" })
$pPictures = @($pictures | Where-Object { $_.pict_type -eq "P" })
if ($pictures.Count -ne $decoder.frames -or
    $iPictures.Count -ne $decoder.i_frames -or
    $pPictures.Count -ne $decoder.p_frames) {
    throw "Decoder and FFprobe picture counts do not match."
}

function Get-Percent([double]$Part, [double]$Whole) {
    if ($Whole -eq 0.0) { return 0.0 }
    return 100.0 * $Part / $Whole
}

function Get-AveragePacketBytes($Frames) {
    if (-not $Frames.Count) { return 0.0 }
    return [double](($Frames |
        Measure-Object -Property pkt_size -Average).Average)
}

$classifiedBlocks = [uint64]$decoder.dc_only_blocks +
    [uint64]$decoder.sparse_blocks +
    [uint64]$decoder.dense_blocks
$predictions = [uint64]$decoder.integer_predictions +
    [uint64]$decoder.horizontal_predictions +
    [uint64]$decoder.vertical_predictions +
    [uint64]$decoder.diagonal_predictions
$report = [ordered]@{
    input = $InputFile
    frames = [uint64]$decoder.frames
    pictures = [ordered]@{
        i_count = [uint64]$decoder.i_frames
        p_count = [uint64]$decoder.p_frames
        i_average_packet_bytes = Get-AveragePacketBytes $iPictures
        p_average_packet_bytes = Get-AveragePacketBytes $pPictures
    }
    macroblocks = [ordered]@{
        total = [uint64]$decoder.macroblocks
        skipped = [uint64]$decoder.skipped_macroblocks
        skipped_percent = Get-Percent `
            $decoder.skipped_macroblocks $decoder.macroblocks
        intra = [uint64]$decoder.intra_macroblocks
        inter = [uint64]$decoder.inter_macroblocks
        cbp_zero = [uint64]$decoder.cbp_zero_macroblocks
        cbp_zero_percent_of_inter = Get-Percent `
            $decoder.cbp_zero_macroblocks $decoder.inter_macroblocks
    }
    inter_residual_blocks = [ordered]@{
        classified = $classifiedBlocks
        dc_only = [uint64]$decoder.dc_only_blocks
        dc_only_percent = Get-Percent `
            $decoder.dc_only_blocks $classifiedBlocks
        sparse_2_to_10_coefficients = [uint64]$decoder.sparse_blocks
        sparse_percent = Get-Percent `
            $decoder.sparse_blocks $classifiedBlocks
        dense_over_10_coefficients = [uint64]$decoder.dense_blocks
        dense_percent = Get-Percent `
            $decoder.dense_blocks $classifiedBlocks
        one_row = [uint64]$decoder.one_row_blocks
        one_column = [uint64]$decoder.one_column_blocks
        two_columns = [uint64]$decoder.two_column_blocks
    }
    motion_prediction = [ordered]@{
        total = $predictions
        integer = [uint64]$decoder.integer_predictions
        integer_percent = Get-Percent `
            $decoder.integer_predictions $predictions
        horizontal_half_pel = [uint64]$decoder.horizontal_predictions
        vertical_half_pel = [uint64]$decoder.vertical_predictions
        diagonal_half_pel = [uint64]$decoder.diagonal_predictions
        edge_clamped = [uint64]$decoder.edge_predictions
    }
}

$directory = Split-Path $ReportFile -Parent
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$json = $report | ConvertTo-Json -Depth 5
$json | Set-Content -LiteralPath $ReportFile -Encoding utf8
$json
Write-Host "Report: $ReportFile"
