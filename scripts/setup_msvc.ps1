[CmdletBinding()]
param([switch]$InstallIfMissing)

$ErrorActionPreference = "Stop"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$component = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"

function Find-MsvcInstallation {
    if (-not (Test-Path -LiteralPath $vswhere)) { return $null }
    $installation = & $vswhere -latest -products * -requires $component `
        -property installationPath
    if ($LASTEXITCODE -ne 0) { return $null }
    return $installation | Select-Object -First 1
}

$installation = Find-MsvcInstallation
if (-not $installation -and $InstallIfMissing) {
    $winget = Get-Command winget.exe -CommandType Application `
        -ErrorAction SilentlyContinue
    if (-not $winget) {
        throw "MSVC is missing and winget.exe is unavailable. Install the Visual Studio C++ workload."
    }

    Write-Host "Installing Visual Studio 2022 Build Tools with the C++ workload..."
    & $winget.Source install --exact `
        --id Microsoft.VisualStudio.2022.BuildTools `
        --source winget `
        --accept-package-agreements `
        --accept-source-agreements `
        --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    if ($LASTEXITCODE -ne 0) {
        throw "Visual Studio Build Tools installation failed."
    }
    $installation = Find-MsvcInstallation
}

if (-not $installation) {
    throw "Visual Studio C/C++ tools are missing. Run .\setup.ps1 or install the Desktop development with C++ workload."
}

$devcmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path -LiteralPath $devcmd)) {
    throw "VsDevCmd.bat is missing from $installation."
}

Write-Host "MSVC C/C++ tools are ready: $installation"
