#requires -Version 7.4

<#
.SYNOPSIS
Encodes BPV1 v6 profiles declared in a repository YAML file.

.DESCRIPTION
Reads the deliberately small, dependency-free YAML subset used by
out/source/bpv-transcode.yaml. Every video entry is passed to
encode_bpv_target_quality.ps1, so each profile receives an independent lambda
search against its requested RGB PSNR.
#>

[CmdletBinding()]
param(
    [string]$ConfigFile,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [switch]$ValidateOnly,

    [switch]$NoAudio,

    [switch]$Force
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
if (-not $ConfigFile) {
    $ConfigFile = Join-Path $repo "out\source\bpv-transcode.yaml"
}
$ConfigFile = [IO.Path]::GetFullPath($ConfigFile)
if (-not (Test-Path -LiteralPath $ConfigFile)) {
    throw "BPV YAML configuration is missing: $ConfigFile"
}
$configDirectory = Split-Path $ConfigFile -Parent
$culture = [Globalization.CultureInfo]::InvariantCulture

function ConvertFrom-BpvYamlScalar {
    param(
        [Parameter(Mandatory)]
        [string]$Text,

        [int]$LineNumber
    )

    $value = $Text.Trim()
    if (-not $value.Length) {
        return $null
    }
    if ($value.StartsWith('"')) {
        try {
            return $value | ConvertFrom-Json
        }
        catch {
            throw "Invalid quoted YAML value at line $LineNumber."
        }
    }
    if ($value.StartsWith("'")) {
        if ($value.Length -lt 2 -or -not $value.EndsWith("'")) {
            throw "Invalid single-quoted YAML value at line $LineNumber."
        }
        return $value.Substring(1, $value.Length - 2).Replace("''", "'")
    }
    switch -Regex ($value) {
        "^(?i:true)$" { return $true }
        "^(?i:false)$" { return $false }
        "^(?i:null|~)$" { return $null }
        "^[+-]?\d+$" {
            $integer = 0L
            if ([long]::TryParse(
                $value,
                [Globalization.NumberStyles]::Integer,
                $culture,
                [ref]$integer
            )) {
                return $integer
            }
        }
        "^[+-]?(?:\d+\.\d*|\d*\.\d+)(?:[eE][+-]?\d+)?$" {
            $number = 0.0
            if ([double]::TryParse(
                $value,
                [Globalization.NumberStyles]::Float,
                $culture,
                [ref]$number
            )) {
                return $number
            }
        }
    }
    return $value
}

function Read-BpvYamlConfig {
    param([Parameter(Mandatory)][string]$Path)

    $root = [ordered]@{}
    $videos = [Collections.Generic.List[object]]::new()
    $currentVideo = $null
    $insideVideos = $false
    $lineNumber = 0

    foreach ($rawLine in Get-Content -LiteralPath $Path) {
        $lineNumber++
        if ($rawLine.Contains("`t")) {
            throw "YAML tabs are not supported at line $lineNumber."
        }
        $line = $rawLine -replace "\s+#.*$", ""
        $line = $line.TrimEnd()
        if (-not $line.Trim().Length -or
            $line.TrimStart().StartsWith("#")) {
            continue
        }

        if (-not $insideVideos) {
            if ($line -match "^videos:\s*$") {
                $insideVideos = $true
                continue
            }
            if ($line -notmatch "^([A-Za-z][A-Za-z0-9]*):\s*(.+)$") {
                throw "Invalid top-level YAML at line $lineNumber."
            }
            $root[$Matches[1]] = ConvertFrom-BpvYamlScalar `
                -Text $Matches[2] -LineNumber $lineNumber
            continue
        }

        if ($line -match "^  -\s+([A-Za-z][A-Za-z0-9]*):\s*(.+)$") {
            $currentVideo = [ordered]@{}
            [void]$videos.Add($currentVideo)
            $currentVideo[$Matches[1]] = ConvertFrom-BpvYamlScalar `
                -Text $Matches[2] -LineNumber $lineNumber
            continue
        }
        if ($line -match "^    ([A-Za-z][A-Za-z0-9]*):\s*(.+)$") {
            if ($null -eq $currentVideo) {
                throw "Video property before first list item at line $lineNumber."
            }
            $currentVideo[$Matches[1]] = ConvertFrom-BpvYamlScalar `
                -Text $Matches[2] -LineNumber $lineNumber
            continue
        }
        throw "Unsupported YAML structure at line $lineNumber."
    }

    if (-not $insideVideos -or -not $videos.Count) {
        throw "YAML must contain a non-empty videos list."
    }
    $root["videos"] = @($videos)
    return $root
}

function Get-RequiredSetting {
    param(
        [Collections.Specialized.OrderedDictionary]$Map,
        [string]$Name,
        [int]$Profile
    )
    if (-not $Map.Contains($Name) -or $null -eq $Map[$Name] -or
        ($Map[$Name] -is [string] -and
         [string]::IsNullOrWhiteSpace($Map[$Name]))) {
        throw "YAML video $Profile requires '$Name'."
    }
    return $Map[$Name]
}

function Get-OptionalSetting {
    param(
        [Collections.Specialized.OrderedDictionary]$Map,
        [string]$Name,
        $Default
    )
    if ($Map.Contains($Name)) {
        return $Map[$Name]
    }
    return $Default
}

function Resolve-ConfigPath {
    param(
        [string]$Value,
        [string]$BaseDirectory
    )
    if ([IO.Path]::IsPathRooted($Value)) {
        return [IO.Path]::GetFullPath($Value)
    }
    return [IO.Path]::GetFullPath(
        (Join-Path $BaseDirectory $Value)
    )
}

$config = Read-BpvYamlConfig -Path $ConfigFile
$allowedRootKeys = @("version", "outputDirectory", "summaryFile", "videos")
foreach ($key in $config.Keys) {
    if ($key -notin $allowedRootKeys) {
        throw "Unknown top-level YAML setting: $key"
    }
}
if (-not $config.Contains("version") -or
    [int]$config["version"] -ne 1) {
    throw "Unsupported BPV YAML version; expected version: 1."
}

$configuredOutput = if ($config.Contains("outputDirectory")) {
    [string]$config["outputDirectory"]
}
else {
    ".."
}
$outputDirectory = Resolve-ConfigPath `
    -Value $configuredOutput `
    -BaseDirectory $configDirectory
if (-not $ValidateOnly) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}

$summaryFile = if ($config.Contains("summaryFile")) {
    Resolve-ConfigPath `
        -Value ([string]$config["summaryFile"]) `
        -BaseDirectory $configDirectory
}
else {
    Join-Path $outputDirectory "BPVv6_yaml_summary.json"
}
if (-not $ValidateOnly -and
    (Test-Path -LiteralPath $summaryFile) -and -not $Force) {
    throw "YAML summary already exists; use -Force: $summaryFile"
}
$summaryParent = Split-Path $summaryFile -Parent
if (-not $ValidateOnly) {
    New-Item -ItemType Directory -Force -Path $summaryParent | Out-Null
}

$allowedVideoKeys = @(
    "name", "input", "width", "height", "fps", "format", "codec",
    "targetPsnrDb", "toleranceDb", "resizeMode", "audio", "threads",
    "gop", "minGop", "sceneThreshold", "candidatePalettes",
    "sampleBlocks", "samplesPerFrame",
    "blockIterations", "colorIterations", "colorsPerCluster",
    "activePalettes", "initialLambda", "maximumLambda",
    "searchIterations"
)
$targetScript = Join-Path $PSScriptRoot "encode_bpv_target_quality.ps1"
$results = [Collections.Generic.List[object]]::new()
$profileNumber = 0

foreach ($video in $config["videos"]) {
    $profileNumber++
    foreach ($key in $video.Keys) {
        if ($key -notin $allowedVideoKeys) {
            throw "Unknown setting '$key' in YAML video $profileNumber."
        }
    }

    $name = [string](Get-RequiredSetting `
        -Map $video -Name "name" -Profile $profileNumber)
    $inputValue = [string](Get-RequiredSetting `
        -Map $video -Name "input" -Profile $profileNumber)
    $format = [string](Get-RequiredSetting `
        -Map $video -Name "format" -Profile $profileNumber)
    $codec = [string](Get-OptionalSetting `
        -Map $video -Name "codec" -Default "BPVv6")
    if (-not $format.Equals(
        "bpv1",
        [StringComparison]::OrdinalIgnoreCase
    )) {
        throw "YAML video $profileNumber has unsupported format '$format'."
    }
    if (-not $codec.Equals(
        "BPVv6",
        [StringComparison]::OrdinalIgnoreCase
    )) {
        throw "YAML video $profileNumber has unsupported codec '$codec'."
    }

    $fpsSetting = Get-RequiredSetting `
        -Map $video -Name "fps" -Profile $profileNumber
    $fps = if ($fpsSetting -is [string] -and
        $fpsSetting.Equals(
            "source",
            [StringComparison]::OrdinalIgnoreCase
        )) {
        0.0
    }
    else {
        [double]$fpsSetting
    }
    $resolvedInput = Resolve-ConfigPath `
        -Value $inputValue `
        -BaseDirectory $configDirectory
    if (-not (Test-Path -LiteralPath $resolvedInput)) {
        throw "YAML video $profileNumber input is missing: $resolvedInput"
    }

    $arguments = @{
        InputFile = @($resolvedInput)
        OutputName = @($name)
        OutputDirectory = $outputDirectory
        TargetPsnrDb = [double](Get-RequiredSetting `
            -Map $video -Name "targetPsnrDb" -Profile $profileNumber)
        Width = [int](Get-RequiredSetting `
            -Map $video -Name "width" -Profile $profileNumber)
        Height = [int](Get-RequiredSetting `
            -Map $video -Name "height" -Profile $profileNumber)
        Fps = $fps
        ResizeMode = [string](Get-OptionalSetting `
            -Map $video -Name "resizeMode" -Default "Stretch")
        Threads = [int](Get-OptionalSetting `
            -Map $video -Name "threads" -Default 8)
        Gop = [int](Get-OptionalSetting `
            -Map $video -Name "gop" -Default 48)
        MinGop = [int](Get-OptionalSetting `
            -Map $video -Name "minGop" -Default 12)
        SceneThreshold = [double](Get-OptionalSetting `
            -Map $video -Name "sceneThreshold" -Default 0.35)
        CandidatePalettes = [int](Get-OptionalSetting `
            -Map $video -Name "candidatePalettes" -Default 8)
        SampleBlocks = [int](Get-OptionalSetting `
            -Map $video -Name "sampleBlocks" -Default 32768)
        SamplesPerFrame = [int](Get-OptionalSetting `
            -Map $video -Name "samplesPerFrame" -Default 256)
        BlockIterations = [int](Get-OptionalSetting `
            -Map $video -Name "blockIterations" -Default 10)
        ColorIterations = [int](Get-OptionalSetting `
            -Map $video -Name "colorIterations" -Default 10)
        ColorsPerCluster = [int](Get-OptionalSetting `
            -Map $video -Name "colorsPerCluster" -Default 8192)
        ActivePalettes = [bool](Get-OptionalSetting `
            -Map $video -Name "activePalettes" -Default $true)
        ToleranceDb = [double](Get-OptionalSetting `
            -Map $video -Name "toleranceDb" -Default 0.10)
        InitialLambda = [double](Get-OptionalSetting `
            -Map $video -Name "initialLambda" -Default 64)
        MaximumLambda = [double](Get-OptionalSetting `
            -Map $video -Name "maximumLambda" -Default 65536)
        SearchIterations = [int](Get-OptionalSetting `
            -Map $video -Name "searchIterations" -Default 10)
        MaxFrames = $MaxFrames
        NoSummary = $true
    }
    $audioEnabled = [bool](Get-OptionalSetting `
        -Map $video -Name "audio" -Default $true)
    if ($NoAudio -or -not $audioEnabled) {
        $arguments["NoAudio"] = $true
    }
    if ($Force) {
        $arguments["Force"] = $true
    }

    Write-Host ""
    Write-Host (
        (
            "YAML profile {0}/{1}: {2}, {3}x{4}, fps={5}, " +
            "{6}, target={7} dB"
        ) -f
        $profileNumber,
        $config["videos"].Count,
        $name,
        $arguments.Width,
        $arguments.Height,
        $(if ($fps) { $fps } else { "source" }),
        $codec,
        $arguments.TargetPsnrDb
    )
    if ($ValidateOnly) {
        continue
    }
    $profileResults = @(& $targetScript @arguments)
    foreach ($result in $profileResults) {
        $result |
            Add-Member -NotePropertyName yamlProfile `
                -NotePropertyValue $profileNumber -Force
        [void]$results.Add($result)
    }
}

if ($ValidateOnly) {
    Write-Host ""
    Write-Host (
        "YAML configuration is valid: {0} profile(s)." -f
        $profileNumber
    )
    return
}

$summary = [pscustomobject]@{
    configVersion = 1
    configFile = $ConfigFile
    generatedAt = [DateTime]::UtcNow.ToString("o", $culture)
    profiles = @($results)
}
$summary |
    ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $summaryFile -Encoding utf8NoBOM
Write-Host ""
Write-Host "YAML batch summary: $summaryFile"
$results
