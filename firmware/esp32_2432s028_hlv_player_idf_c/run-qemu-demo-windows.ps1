#requires -Version 7.4

[CmdletBinding()]
param(
    [switch]$Rebuild,
    [switch]$Headless,
    [ValidateRange(0, 100)]
    [int]$Volume = 70
)

$ErrorActionPreference = "Stop"

$project = $PSScriptRoot
$build = Join-Path $project "build-qemu-demo"
$flash = Join-Path $build "qemu_demo_flash_4mb.bin"
$idf = Join-Path $project "idf.ps1"
$cache = Join-Path $build "CMakeCache.txt"

# CMake stores absolute source and toolchain paths. A firmware directory
# rename can otherwise leave a seemingly valid demo flash that was built from
# the old project location. Recreate only this generated demo build directory
# when its cache no longer belongs to the current checkout.
$staleReason = $null
if (Test-Path -LiteralPath $cache -PathType Leaf) {
    $cacheLines = Get-Content -LiteralPath $cache
    $homeEntry = $cacheLines |
        Where-Object { $_ -like "CMAKE_HOME_DIRECTORY:INTERNAL=*" } |
        Select-Object -First 1
    $pythonEntry = $cacheLines |
        Where-Object { $_ -like "PYTHON:UNINITIALIZED=*" } |
        Select-Object -First 1
    if ($homeEntry) {
        $cachedProject = [IO.Path]::GetFullPath(
            $homeEntry.Substring($homeEntry.IndexOf("=") + 1)
        )
        $expectedProject = [IO.Path]::GetFullPath($project)
        if (-not $cachedProject.Equals(
                $expectedProject,
                [StringComparison]::OrdinalIgnoreCase)) {
            $staleReason = "project $cachedProject"
        }
    } else {
        $staleReason = "missing CMAKE_HOME_DIRECTORY"
    }
    if (-not $staleReason -and $pythonEntry) {
        $cachedPython = [IO.Path]::GetFullPath(
            $pythonEntry.Substring($pythonEntry.IndexOf("=") + 1)
        )
        $expectedToolRoot =
            [IO.Path]::GetFullPath($project).TrimEnd("\", "/") +
            [IO.Path]::DirectorySeparatorChar
        if (-not $cachedPython.StartsWith(
                $expectedToolRoot,
                [StringComparison]::OrdinalIgnoreCase)) {
            $staleReason = "toolchain $cachedPython"
        }
    }
} elseif (Test-Path -LiteralPath $flash -PathType Leaf) {
    $staleReason = "missing CMake cache"
}

if ($staleReason) {
    $expectedBuild = [IO.Path]::GetFullPath(
        (Join-Path $project "build-qemu-demo")
    )
    $resolvedBuild = [IO.Path]::GetFullPath($build)
    if (-not $resolvedBuild.Equals(
            $expectedBuild, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected build directory: $resolvedBuild"
    }
    Write-Host "Removing stale QEMU demo build for $staleReason"
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

if ($Rebuild -or
    $staleReason -or
    -not (Test-Path -LiteralPath $flash -PathType Leaf)) {
    & $idf -IdfArguments @(
        "-C", $project,
        "-B", $build,
        "build"
    )
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32 demo firmware build failed."
    }

    & $idf `
        -EsptoolArguments @(
            "--chip", "esp32",
            "merge_bin",
            "-o", $flash,
            "--flash_mode", "dio",
            "--flash_freq", "80m",
            "--flash_size", "4MB",
            "--fill-flash-size", "4MB",
            "0x1000", "bootloader\bootloader.bin",
            "0x8000", "partition_table\partition-table.bin",
            "0x10000", "hlv_esp32_player.bin"
        ) `
        -EsptoolWorkingDirectory $build
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32 demo flash image creation failed."
    }
}

$arguments = @{
    FlashImage = $flash
    Volume = $Volume
}
if ($Headless) {
    $arguments.Headless = $true
}

& (Join-Path $project "run-qemu-sdspi-windows.ps1") @arguments
