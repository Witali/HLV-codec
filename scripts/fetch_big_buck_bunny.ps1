[CmdletBinding()]
param(
    [string]$OutputFile = `
        (Join-Path (Split-Path $PSScriptRoot -Parent) `
            "out\sources\BigBuckBunny_320x180.mp4")
)

$ErrorActionPreference = "Stop"
$url = "https://download.blender.org/peach/bigbuckbunny_movies/BigBuckBunny_320x180.mp4"
$expectedLength = 64657027
$expectedSha256 = `
    "F78F39603E6774907F2FAAFABF26A667F4A6FC31769EC304A8A8F7C62D280508"

$parent = Split-Path $OutputFile -Parent
if ($parent) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
$OutputFile = [IO.Path]::GetFullPath($OutputFile)

if (Test-Path -LiteralPath $OutputFile) {
    $existing = Get-Item -LiteralPath $OutputFile
    $existingHash = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath $OutputFile).Hash
    if ($existing.Length -eq $expectedLength -and
        $existingHash -eq $expectedSha256) {
        Write-Host "Already downloaded and verified: $OutputFile"
        exit 0
    }
}

$temporary = "$OutputFile.download"
try {
    Invoke-WebRequest -Uri $url -OutFile $temporary -UseBasicParsing
    $download = Get-Item -LiteralPath $temporary
    $downloadHash = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath $temporary).Hash
    if ($download.Length -ne $expectedLength -or
        $downloadHash -ne $expectedSha256) {
        throw "Downloaded Big Buck Bunny did not match the expected file."
    }
    Move-Item -LiteralPath $temporary -Destination $OutputFile -Force
    Write-Host "Downloaded and verified: $OutputFile"
} finally {
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Force
    }
}
