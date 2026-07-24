[CmdletBinding()]
param(
    [switch]$ForceDownload
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$version = "8.1.1"
$archiveSha256 = `
    "49B28C5F16ADDD40239A66949973458769B7056FB7752C30AC0D53389D09A552"
$toolDirectory = Join-Path $repo "local_tools\ffmpeg"
$ffmpegDirectory = Join-Path $toolDirectory "bin"
$ffmpeg = Join-Path $ffmpegDirectory "ffmpeg.exe"
$ffprobe = Join-Path $ffmpegDirectory "ffprobe.exe"
$archive = Join-Path ([IO.Path]::GetTempPath()) "ffmpeg-$version-full_build.zip"
$unpackDirectory = Join-Path ([IO.Path]::GetTempPath()) `
    ("hlv1-ffmpeg-{0}" -f [guid]::NewGuid().ToString("N"))
$downloadUrl = "https://github.com/GyanD/codexffmpeg/releases/download/" +
    "$version/ffmpeg-$version-full_build.zip"

if ($ForceDownload -or -not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
    New-Item -ItemType Directory -Force -Path $ffmpegDirectory | Out-Null
    try {
        Write-Host "Downloading FFmpeg $version..."
        Invoke-WebRequest -UseBasicParsing -Uri $downloadUrl -OutFile $archive
        $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash
        if ($actualHash -ne $archiveSha256) {
            throw "FFmpeg archive checksum mismatch."
        }

        Expand-Archive -LiteralPath $archive -DestinationPath $unpackDirectory
        $downloadedFfmpeg = Get-ChildItem -LiteralPath $unpackDirectory `
            -Recurse -Filter ffmpeg.exe | Select-Object -First 1
        $downloadedFfprobe = Get-ChildItem -LiteralPath $unpackDirectory `
            -Recurse -Filter ffprobe.exe | Select-Object -First 1
        if (-not $downloadedFfmpeg -or -not $downloadedFfprobe) {
            throw "ffmpeg.exe or ffprobe.exe is missing from the archive."
        }
        Copy-Item -LiteralPath $downloadedFfmpeg.FullName `
            -Destination $ffmpeg -Force
        Copy-Item -LiteralPath $downloadedFfprobe.FullName `
            -Destination $ffprobe -Force
    } finally {
        if (Test-Path -LiteralPath $archive) {
            Remove-Item -LiteralPath $archive -Force
        }
        if (Test-Path -LiteralPath $unpackDirectory) {
            Remove-Item -LiteralPath $unpackDirectory -Recurse -Force
        }
    }
}

$versionOutput = & $ffmpeg -version
$probeVersionOutput = & $ffprobe -version
if ($LASTEXITCODE -ne 0 -or -not $versionOutput -or
    -not $probeVersionOutput) {
    throw "The project-local FFmpeg tools cannot be started."
}
$versionOutput | Select-Object -First 1
Write-Host "Project-local FFmpeg is ready: $ffmpeg"
Write-Host "Project-local FFprobe is ready: $ffprobe"
