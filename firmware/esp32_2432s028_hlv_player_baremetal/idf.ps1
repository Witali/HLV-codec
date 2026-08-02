[CmdletBinding(DefaultParameterSetName = "Idf")]
param(
    [Parameter(ParameterSetName = "Idf", ValueFromRemainingArguments = $true)]
    [string[]]$IdfArguments,
    [Parameter(Mandatory, ParameterSetName = "Esptool")]
    [string[]]$EsptoolArguments,
    [Parameter(ParameterSetName = "Esptool")]
    [string]$EsptoolWorkingDirectory
)

$ErrorActionPreference = "Stop"
$baseScript = Join-Path $PSScriptRoot `
    "..\esp32_2432s028_hlv_player_idf_c\idf.ps1"
if ($PSCmdlet.ParameterSetName -eq "Esptool") {
    & $baseScript -EsptoolWorkingDirectory $EsptoolWorkingDirectory `
        -EsptoolArguments $EsptoolArguments
} else {
    $projectArguments = @("-C", $PSScriptRoot) + $IdfArguments
    & $baseScript -IdfArguments $projectArguments
}
