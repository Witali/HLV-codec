param(
    [switch]$Pristine
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ProjectDir "..\..")).Path
$Workspace = Join-Path $RepoRoot "local_tools\zephyr-workspace"
$ZephyrBase = Join-Path $Workspace "zephyr"
$SdkDir = Join-Path $Workspace "zephyr-sdk-1.0.1\zephyr-sdk-1.0.1"
$Python = Join-Path $Workspace ".venv\Scripts\python.exe"
$PythonBin = Join-Path $Workspace ".venv\Scripts"
$IdfTools = Join-Path $RepoRoot "firmware\esp32_2432s028_hlv_player_idf_c\.tools\espressif\tools"
$CMakeBin = Join-Path $IdfTools "cmake\3.30.2\bin"
$NinjaBin = Join-Path $IdfTools "ninja\1.12.1"
$BuildDir = Join-Path $ProjectDir "build"

foreach ($RequiredPath in @($ZephyrBase, $SdkDir, $Python, $CMakeBin, $NinjaBin)) {
    if (-not (Test-Path -LiteralPath $RequiredPath)) {
        throw "Missing Zephyr prerequisite: $RequiredPath"
    }
}

$env:ZEPHYR_BASE = $ZephyrBase
$env:ZEPHYR_SDK_INSTALL_DIR = $SdkDir
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:Path = "$PythonBin;$CMakeBin;$NinjaBin;$env:Path"

$WestArgs = @(
    "-m", "west", "build", "--sysbuild",
    "--board", "esp32_devkitc/esp32/procpu",
    "--build-dir", $BuildDir
)
if ($Pristine) {
    $WestArgs += @("--pristine", "always")
}
$WestArgs += $ProjectDir

& $Python @WestArgs
if ($LASTEXITCODE -ne 0) {
    throw "Zephyr build failed with exit code $LASTEXITCODE"
}
