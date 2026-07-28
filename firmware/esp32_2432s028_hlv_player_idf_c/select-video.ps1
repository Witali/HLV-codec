[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][string]$Name,
    [ValidateSet(460800, 921600, 1500000, 2000000, 3000000)]
    [int]$DataBaud = 2000000
)

$ErrorActionPreference = "Stop"
if ($Name -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,47}$') {
    throw "Name must be a safe /HLV base filename of at most 48 bytes."
}

$temporary = [IO.Path]::Combine(
    [IO.Path]::GetTempPath(),
    "hlv-play-$([Guid]::NewGuid().ToString('N')).txt"
)
try {
    [IO.File]::WriteAllText(
        $temporary, $Name, [Text.ASCIIEncoding]::new($false)
    )
    & (Join-Path $PSScriptRoot "upload-video.ps1") `
        -Port $Port -File $temporary -Name "play.txt" `
        -DataBaud $DataBaud
} finally {
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Force
    }
}
