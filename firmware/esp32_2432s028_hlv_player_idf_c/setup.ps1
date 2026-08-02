[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$project = $PSScriptRoot
$tools = Join-Path $project ".tools"
$cache = Join-Path $tools "cache"
$idfVersion = "5.5.5"
$pythonVersion = "3.12.10"
$idf = Join-Path $tools "esp-idf-v$idfVersion"
$idfTools = Join-Path $tools "espressif"
$python = Join-Path $tools "python"
$marker = Join-Path $tools "ready-v$idfVersion"
$projectPath = [IO.Path]::GetFullPath($project).TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar
)
$projectMarker = "Project=$projectPath"

# SHA-256 values are for the official release files. Keeping them in source
# makes repeated setup deterministic and catches interrupted downloads.
$idfSha256 = "48FFFE90304573EF366BDA06E38C92FD65C6F3636D785CE103FF82E4647964C8"
$pythonSha256 = "67B5635E80EA51072B87941312D00EC8927C4DB9BA18938F7AD2D27B328B95FB"
$idfArchive = Join-Path $cache "esp-idf-v$idfVersion.zip"
$pythonInstaller = Join-Path $cache "python-$pythonVersion-amd64.exe"
$idfUrl = "https://dl.espressif.com/github_assets/espressif/esp-idf/releases/download/v$idfVersion/esp-idf-v$idfVersion.zip"
$pythonUrl = "https://www.python.org/ftp/python/$pythonVersion/python-$pythonVersion-amd64.exe"

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
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash
    if ($actual -ne $ExpectedSha256) {
        Remove-Item -LiteralPath $Destination -Force
        throw "SHA-256 mismatch for $Destination. Expected $ExpectedSha256, got $actual."
    }
    Write-Host "Verified $([IO.Path]::GetFileName($Destination)): $actual"
}

if (Test-Path -LiteralPath $marker) {
    $markerLines = @(Get-Content -LiteralPath $marker)
    $pythonMarker = "Python=$pythonVersion"
    if (($markerLines -contains $projectMarker) -and
        ($markerLines -contains $pythonMarker)) {
        Write-Host "Project-local ESP-IDF v$idfVersion is ready."
        return
    }

    Write-Host "The firmware path or Python version changed; rebuilding the ESP-IDF Python environment."
    $pythonEnvironment = Join-Path $idfTools "python_env"
    if (Test-Path -LiteralPath $pythonEnvironment) {
        Remove-Item -LiteralPath $pythonEnvironment -Recurse -Force
    }
    Remove-Item -LiteralPath $marker -Force
}

New-Item -ItemType Directory -Force -Path $cache | Out-Null

$pythonExecutable = Join-Path $python "python.exe"
$installedPythonVersion = if (Test-Path -LiteralPath $pythonExecutable) {
    (& $pythonExecutable -c "import platform; print(platform.python_version())" |
        Select-Object -First 1)
} else { $null }
if ($installedPythonVersion -ne $pythonVersion) {
    Get-VerifiedDownload -Url $pythonUrl -Destination $pythonInstaller `
        -ExpectedSha256 $pythonSha256
    if (Test-Path -LiteralPath $python) {
        Write-Host "Replacing project-local Python $installedPythonVersion with $pythonVersion"
        Remove-Item -LiteralPath $python -Recurse -Force
    }
    Write-Host "Installing project-local Python $pythonVersion"
    $arguments = @(
        "/quiet", "InstallAllUsers=0", "TargetDir=$python",
        "AssociateFiles=0", "Shortcuts=0", "PrependPath=0",
        "Include_doc=0", "Include_debug=0", "Include_dev=0",
        "Include_launcher=0", "Include_pip=1", "Include_tcltk=0",
        "Include_test=0"
    )
    $process = Start-Process -FilePath $pythonInstaller `
        -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "Project-local Python installation failed: $($process.ExitCode)"
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $idf "tools\idf.py"))) {
    Get-VerifiedDownload -Url $idfUrl -Destination $idfArchive `
        -ExpectedSha256 $idfSha256
    Write-Host "Extracting ESP-IDF v$idfVersion (including submodules)"
    Expand-Archive -LiteralPath $idfArchive -DestinationPath $tools -Force
}

$env:IDF_TOOLS_PATH = $idfTools
$env:PYTHONNOUSERSITE = "1"
$env:Path = "$python;$python\Scripts;$env:Path"
Write-Host "Installing the ESP32-only IDF toolchain into $idfTools"
& (Join-Path $idf "install.ps1") esp32
if ($LASTEXITCODE -ne 0) {
    throw "ESP-IDF tool installation failed."
}

Set-Content -LiteralPath $marker -Value @(
    "ESP-IDF=$idfVersion"
    "Python=$pythonVersion"
    $projectMarker
    "Installed=$([DateTime]::UtcNow.ToString('o'))"
) -Encoding ascii
Write-Host "Project-local ESP-IDF environment is ready."
