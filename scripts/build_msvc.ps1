[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) "build\msvc")
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer (vswhere.exe) was not found."
}

$vs = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vs) {
    throw "Visual Studio C/C++ tools are not installed."
}

$devcmd = Join-Path $vs "Common7\Tools\VsDevCmd.bat"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

$librarySources = @(
    (Join-Path $repo "src\hlv1_common.c"),
    (Join-Path $repo "src\hlv1_y4m.c"),
    (Join-Path $repo "src\hlv1_encode.c"),
    (Join-Path $repo "src\hlv1_decode.c")
)

function Invoke-CBuild {
    param([string]$Name, [string]$Source)

    $sources = @($Source) + $librarySources
    $quotedSources = ($sources | ForEach-Object { '"' + $_ + '"' }) -join " "
    $output = Join-Path $OutputDirectory "$Name.exe"
    $commandTemplate = 'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
        'cl /nologo /O2 /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS ' +
        '/I"{2}" {3} /Fe:"{4}"'
    $command = $commandTemplate -f $devcmd, $OutputDirectory, `
        (Join-Path $repo "include"), $quotedSources, $output

    Write-Host "Building $Name..."
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "MSVC failed while building $Name."
    }
}

foreach ($tool in @(
    "hlvenc", "hlvdec", "hlvinfo", "hlvbenchdec", "hlvpeakdec"
)) {
    Invoke-CBuild $tool (Join-Path $repo "tools\$tool.c")
}
Invoke-CBuild "test_roundtrip" (Join-Path $repo "tests\test_roundtrip.c")
Invoke-CBuild "test_errors" (Join-Path $repo "tests\test_errors.c")

& (Join-Path $OutputDirectory "test_roundtrip.exe")
if ($LASTEXITCODE -ne 0) { throw "test_roundtrip failed." }
& (Join-Path $OutputDirectory "test_errors.exe")
if ($LASTEXITCODE -ne 0) { throw "test_errors failed." }

& (Join-Path $PSScriptRoot "build_windows_player.ps1") `
    -OutputDirectory $OutputDirectory -SkipCompilerCheck

Write-Host "MSVC tools are ready in $OutputDirectory"
