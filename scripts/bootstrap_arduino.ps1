[CmdletBinding()]
param(
    [switch]$ForceCliDownload
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$cliVersion = "1.5.1"
$esp32Version = "3.3.8"
$lovyanGfxVersion = "1.2.21"
$cliArchiveSha256 = `
    "FABE42E0EB04D00E776A66178299FF95A46C623DBC260F997E58FD514853DD40"
$cliDirectory = Join-Path $repo "local_tools\arduino-cli"
$cli = Join-Path $cliDirectory "arduino-cli.exe"
$config = Join-Path $repo "arduino-cli.yaml"
$archive = Join-Path ([IO.Path]::GetTempPath()) `
    "arduino-cli_${cliVersion}_Windows_64bit.zip"
$downloadUrl = "https://github.com/arduino/arduino-cli/releases/download/" +
    "v$cliVersion/arduino-cli_${cliVersion}_Windows_64bit.zip"

function Invoke-ArduinoCli {
    param([Parameter(ValueFromRemainingArguments)][string[]]$Arguments)

    & $cli --config-file $config @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "arduino-cli failed: $($Arguments -join ' ')"
    }
}

if ($ForceCliDownload -or -not (Test-Path -LiteralPath $cli)) {
    New-Item -ItemType Directory -Force -Path $cliDirectory | Out-Null
    try {
        Write-Host "Downloading Arduino CLI $cliVersion..."
        Invoke-WebRequest -UseBasicParsing -Uri $downloadUrl -OutFile $archive
        $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash
        if ($actualHash -ne $cliArchiveSha256) {
            throw "Arduino CLI archive checksum mismatch."
        }
        Expand-Archive -LiteralPath $archive -DestinationPath $cliDirectory -Force
    } finally {
        if (Test-Path -LiteralPath $archive) {
            Remove-Item -LiteralPath $archive -Force
        }
    }
}

New-Item -ItemType Directory -Force `
    -Path (Join-Path $repo "local_tools\arduino") | Out-Null

Push-Location $repo
try {
    Invoke-ArduinoCli version
    Invoke-ArduinoCli core update-index
    Invoke-ArduinoCli core install "esp32:esp32@$esp32Version"
    Invoke-ArduinoCli lib install "LovyanGFX@$lovyanGfxVersion"
} finally {
    Pop-Location
}

Write-Host "Project-local Arduino environment is ready:"
Write-Host "  CLI:       $cli"
Write-Host "  packages:  $(Join-Path $repo 'local_tools\arduino')"
