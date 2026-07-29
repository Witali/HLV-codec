[CmdletBinding()]
param([switch]$Clean)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$buildDirectory = Join-Path $project "build"
$cache = Join-Path $buildDirectory "CMakeCache.txt"
$toolchainProject = $project

& (Join-Path $project "setup.ps1")

# CMake records an absolute source path. Recreate only the generated default
# build directory when a repository directory rename makes that path stale.
if (Test-Path -LiteralPath $cache) {
    $cacheLines = Get-Content -LiteralPath $cache
    $homeEntry = $cacheLines |
        Where-Object { $_ -like "CMAKE_HOME_DIRECTORY:INTERNAL=*" } |
        Select-Object -First 1
    $pythonEntry = $cacheLines |
        Where-Object { $_ -like "PYTHON:UNINITIALIZED=*" } |
        Select-Object -First 1
    $staleReason = $null
    if ($homeEntry) {
        $cachedProject = $homeEntry.Substring($homeEntry.IndexOf("=") + 1)
        $expectedProject = [IO.Path]::GetFullPath($project)
        $cachedFullPath = [IO.Path]::GetFullPath($cachedProject)
        if (-not $cachedFullPath.Equals(
                $expectedProject, [StringComparison]::OrdinalIgnoreCase)) {
            $staleReason = "project $cachedFullPath"
        }
    }
    if (-not $staleReason -and $pythonEntry) {
        $cachedPython = $pythonEntry.Substring($pythonEntry.IndexOf("=") + 1)
        $expectedToolRoot =
            [IO.Path]::GetFullPath($toolchainProject).TrimEnd("\", "/") +
            [IO.Path]::DirectorySeparatorChar
        $cachedPythonPath = [IO.Path]::GetFullPath($cachedPython)
        if (-not $cachedPythonPath.StartsWith(
                $expectedToolRoot, [StringComparison]::OrdinalIgnoreCase)) {
            $staleReason = "toolchain $cachedPythonPath"
        }
    }
    if ($staleReason) {
        $expectedProject = [IO.Path]::GetFullPath($project)
        $resolvedBuild = [IO.Path]::GetFullPath($buildDirectory)
        $expectedBuild = Join-Path $expectedProject "build"
        if (-not $resolvedBuild.Equals(
                $expectedBuild, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unexpected build directory: $resolvedBuild"
        }
        Write-Host "Removing stale build cache for $staleReason"
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
    }
}

if ($Clean) {
    & (Join-Path $project "idf.ps1") -IdfArguments @("fullclean")
}
& (Join-Path $project "idf.ps1") -IdfArguments @("build")

Write-Host "ESP-IDF firmware is ready in $buildDirectory"
