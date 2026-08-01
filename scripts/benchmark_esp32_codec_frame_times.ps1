[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600, 1000000)]
    [int]$Baud = 460800,
    [string]$OutputDirectory,
    [switch]$Resume
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$firmware = Join-Path $repo "firmware\esp32_2432s028_hlv_player_idf_c"
$manifestPath = Join-Path $PSScriptRoot "esp32_codec_frame_time_manifest.csv"
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repo "out\benchmarks\esp32-codec-frame-times"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

$tests = Import-Csv -LiteralPath $manifestPath
$selectionBackup = Join-Path $OutputDirectory "play-before-benchmark.txt"
$readScript = Join-Path $firmware "read-file.ps1"
$selectScript = Join-Path $firmware "select-video.ps1"
$captureScript = Join-Path $firmware "capture-player-metrics.ps1"

& $readScript -Port $Port -Name "play.txt" -Output $selectionBackup -Force `
    -DataBaud $Baud
if ($LASTEXITCODE -ne 0) {
    throw "Could not save the existing play.txt selection."
}
$originalSelection = [IO.File]::ReadAllText(
    $selectionBackup, [Text.Encoding]::ASCII
).Trim()
if (-not $originalSelection) {
    throw "The existing play.txt selection is empty."
}

try {
    foreach ($test in $tests) {
        Write-Host "Selecting $($test.filename)"
        & $selectScript -Port $Port -Name $test.filename -DataBaud $Baud
        if ($LASTEXITCODE -ne 0) {
            throw "Could not select $($test.filename)."
        }

        $fps = [double]$test.fps
        $durationSeconds = [double]$test.frames / $fps
        $windowSeconds = [Math]::Min(60.0, $durationSeconds)
        $windowFrames = [int][Math]::Max(
            1, [Math]::Round($fps * $windowSeconds)
        )
        $endStart = [Math]::Max(
            0.0, $durationSeconds - $windowSeconds - 0.5
        )
        $windows = @(
            [pscustomobject]@{ Name = "start"; Seconds = 0.0 },
            [pscustomobject]@{
                Name = "middle"
                Seconds = [Math]::Max(
                    0.0, ($durationSeconds - $windowSeconds) / 2.0
                )
            },
            [pscustomobject]@{ Name = "end"; Seconds = $endStart }
        )
        foreach ($window in $windows) {
            $outputCsv = Join-Path $OutputDirectory (
                "{0}.{1}.csv" -f $test.filename, $window.Name
            )
            if ($Resume -and (Test-Path -LiteralPath $outputCsv)) {
                Write-Host (
                    "Skipping completed {0} {1}" -f `
                    $test.filename, $window.Name
                )
                continue
            }
            $seekMilliseconds = [UInt32][Math]::Floor(
                $window.Seconds * 1000.0
            )
            $timeoutSeconds = [int][Math]::Ceiling(
                [Math]::Min(
                    3600.0,
                    [Math]::Max(180.0, $window.Seconds * 3.0 + 180.0)
                )
            )
            Write-Host (
                "Capturing {0} window at {1:N3} s: {2} frames; timeout {3} s" -f `
                $window.Name, $window.Seconds, $windowFrames, $timeoutSeconds
            )
            & $captureScript -Port $Port -Baud $Baud `
                -SeekMilliseconds $seekMilliseconds `
                -Frames $windowFrames -TimeoutSeconds $timeoutSeconds `
                -OutputCsv $outputCsv -AllowAudioUnderrun
            if ($LASTEXITCODE -ne 0) {
                throw (
                    "Metric capture failed for {0} {1}." -f `
                    $test.filename, $window.Name
                )
            }
        }
    }
} finally {
    Write-Host "Restoring play.txt selection: $originalSelection"
    & $selectScript -Port $Port -Name $originalSelection -DataBaud $Baud
}

$summaryScript = Join-Path $PSScriptRoot `
    "summarize_esp32_codec_frame_times.py"
$python = Get-ChildItem -LiteralPath `
    (Join-Path $firmware ".tools\espressif\python_env") -Recurse `
    -Filter python.exe | Where-Object {
        $_.FullName -like "*\Scripts\python.exe"
    } | Select-Object -First 1 -ExpandProperty FullName
if (-not $python) {
    throw "Project-local ESP-IDF Python environment was not found."
}
& $python $summaryScript --manifest $manifestPath `
    --input-directory $OutputDirectory --window-seconds 60 `
    --output (Join-Path $OutputDirectory "summary.csv")
if ($LASTEXITCODE -ne 0) {
    throw "Frame-window summary failed."
}
