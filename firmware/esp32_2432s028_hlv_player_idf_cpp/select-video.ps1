[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][string]$Name,
    [ValidateSet(460800, 921600, 1000000, 1500000, 2000000, 3000000)]
    [int]$DataBaud = 460800
)

$ErrorActionPreference = "Stop"
if ($Name -notmatch '^[A-Za-z0-9](?:[A-Za-z0-9._ -]{0,109}[A-Za-z0-9._-])?$' -or
    $Name.Contains("..")) {
    throw "Name must be a safe /HLV base filename of at most 111 bytes."
}

$temporary = [IO.Path]::Combine(
    [IO.Path]::GetTempPath(),
    "hlv-play-$([Guid]::NewGuid().ToString('N')).txt"
)
try {
    [IO.File]::WriteAllText(
        $temporary, $Name, [Text.Encoding]::ASCII
    )
    & (Join-Path $PSScriptRoot "upload-video.ps1") `
        -Port $Port -File $temporary -Name "play.txt" `
        -DataBaud $DataBaud
} finally {
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Force
    }
}
