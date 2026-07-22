[CmdletBinding()]
param([switch]$ForceInstall)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$repo = Split-Path $PSScriptRoot -Parent
$version = "3.11.9"
$pythonSha256 = "009D6BF7E3B2DDCA3D784FA09F90FE54336D5B60F0E0F305C37F400BF83CFD3B"
$pipVersion = "26.1.2"
$pipSha256 = "382FF9F685EE3BC25864F820AA50505825F10F5458FFFF07E30A6D96E5715CAB"
$localTools = Join-Path $repo "local_tools"
$cache = Join-Path $localTools "cache"
$pythonDirectory = Join-Path $localTools "python"
$python = Join-Path $pythonDirectory "python.exe"
$pythonArchive = Join-Path $cache "python-$version-embed-amd64.zip"
$pythonUrl = "https://www.python.org/ftp/python/$version/python-$version-embed-amd64.zip"
$pipWheel = Join-Path $cache "pip-$pipVersion-py3-none-any.whl"
$pipUrl = "https://files.pythonhosted.org/packages/5d/95/6b5cb3461ea5673ba0995989746db58eb18b91b54dbf331e72f569540946/pip-$pipVersion-py3-none-any.whl"
$sitePackages = Join-Path $pythonDirectory "Lib\site-packages"
$requirements = Join-Path $repo "requirements-tools.txt"
$marker = Join-Path $pythonDirectory "hlv-tools-ready.txt"

function Get-VerifiedDownload {
    param(
        [Parameter(Mandatory)][string]$Url,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][string]$ExpectedSha256
    )

    if (Test-Path -LiteralPath $Destination) {
        $cachedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash
        if ($cachedHash -ne $ExpectedSha256) {
            Write-Warning "Removing an incomplete or invalid cached download: $Destination"
            Remove-Item -LiteralPath $Destination -Force
        }
    }
    if (-not (Test-Path -LiteralPath $Destination)) {
        Write-Host "Downloading $Url"
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Destination
    }

    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash
    if ($actualHash -ne $ExpectedSha256) {
        Remove-Item -LiteralPath $Destination -Force
        throw "Download checksum mismatch. Expected $ExpectedSha256, got $actualHash."
    }
}

New-Item -ItemType Directory -Force -Path $cache | Out-Null
if (-not (Test-Path -LiteralPath $python)) {
    Get-VerifiedDownload -Url $pythonUrl -Destination $pythonArchive `
        -ExpectedSha256 $pythonSha256

    Write-Host "Extracting portable Python $version into $pythonDirectory"
    New-Item -ItemType Directory -Force -Path $pythonDirectory | Out-Null
    Expand-Archive -LiteralPath $pythonArchive -DestinationPath $pythonDirectory -Force
}

# The embeddable distribution is isolated by default. Enable site initialization
# and explicitly add the repository-local package directory.
$pth = Join-Path $pythonDirectory "python311._pth"
if (-not (Test-Path -LiteralPath $pth)) {
    throw "Portable Python path configuration is missing: $pth"
}
$pthLines = @(Get-Content -LiteralPath $pth) |
    ForEach-Object { if ($_ -eq "#import site") { "import site" } else { $_ } }
if ($pthLines -notcontains "Lib\site-packages") {
    $pthLines += "Lib\site-packages"
}
Set-Content -LiteralPath $pth -Value $pthLines -Encoding ascii
New-Item -ItemType Directory -Force -Path $sitePackages | Out-Null

$requirementsHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $requirements).Hash
$setupKey = "$requirementsHash pip=$pipVersion"
$installedHash = if (Test-Path -LiteralPath $marker) {
    (Get-Content -LiteralPath $marker -ErrorAction SilentlyContinue |
        Select-Object -First 1)
} else { $null }

if ($ForceInstall -or $installedHash -ne $setupKey) {
    Get-VerifiedDownload -Url $pipUrl -Destination $pipWheel `
        -ExpectedSha256 $pipSha256
    Write-Host "Installing project-local Python tool packages..."
    $pipRunner = "import runpy,sys;sys.path.insert(0,sys.argv.pop(1));runpy.run_module('pip',run_name='__main__')"
    & $python -c $pipRunner $pipWheel install `
        --disable-pip-version-check --no-warn-script-location `
        --only-binary=:all: --upgrade --target $sitePackages `
        --requirement $requirements
    if ($LASTEXITCODE -ne 0) {
        throw "Python package installation failed."
    }
    Set-Content -LiteralPath $marker -Value $setupKey -Encoding ascii
}

& $python -c "import numpy, PIL, skimage; print('Python tools:', numpy.__version__, PIL.__version__, skimage.__version__)"
if ($LASTEXITCODE -ne 0) {
    throw "Project-local Python package verification failed."
}
Write-Host "Project-local Python is ready: $python"
