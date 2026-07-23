[CmdletBinding()]
param(
    [Parameter(Position = 0, ValueFromRemainingArguments)]
    [string[]]$ArduinoArguments
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$cli = Join-Path $repo "local_tools\arduino-cli\arduino-cli.exe"
$config = Join-Path $repo "arduino-cli.yaml"

if (-not (Test-Path -LiteralPath $cli)) {
    throw "Local Arduino CLI is missing. Run .\scripts\bootstrap_arduino.ps1 first."
}

Push-Location $repo
try {
    & $cli --config-file $config @ArduinoArguments
    if ($LASTEXITCODE -ne 0) {
        throw "arduino-cli failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}
