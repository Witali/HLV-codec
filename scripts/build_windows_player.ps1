[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) "build\msvc"),
    [switch]$SkipCompilerCheck,

    [ValidateRange(64, 1048576)]
    [int]$H263PacketBufferBytes = 4096,

    [switch]$H263StageProfile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$hlv = Join-Path $repo "codecs\hlv"
$bpv = Join-Path $repo "codecs\bpv"
$mpeg = Join-Path $repo "codecs\mpeg1"
$h263 = Join-Path $repo "codecs\h263"
$pv = Join-Path $h263 "third_party\pv"
$amrnb = Join-Path $repo "codecs\amrnb"
$amrPv = Join-Path $amrnb "third_party\pv"
$plMpeg = Join-Path $repo "third_party\pl_mpeg"
$compactInclude = Join-Path $repo "codecs\common\include"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (-not $SkipCompilerCheck) {
    & (Join-Path $PSScriptRoot "setup_msvc.ps1")
}

$installation = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1
if (-not $installation) {
    throw "Visual Studio C++ tools are missing. Run .\setup.ps1 first."
}

$devcmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$include = Join-Path $hlv "include"
$bpvInclude = Join-Path $bpv "include"
$player = Join-Path $hlv "tools\hlvplay_win32.cpp"
$common = Join-Path $hlv "src\hlv1_common.c"
$decoder = Join-Path $hlv "src\hlv1_decode.c"
$bpvDecoder = Join-Path $bpv "src\bpv1_decode.c"
$imaAdpcm = Join-Path $repo "codecs\common\src\ima_adpcm.c"
$aviDemux = Join-Path $repo "codecs\common\src\avi_demux.c"
$mpegDecoder = Join-Path $mpeg "src\pl_mpeg.c"
$h263Sources = @(
    (Join-Path $h263 "src\h263_3gp.c")
) + @(Get-ChildItem -LiteralPath (Join-Path $pv "src") -Filter "*.c" |
    Sort-Object Name | ForEach-Object { $_.FullName })
$h263SourceArguments = ($h263Sources | ForEach-Object {
    '"{0}"' -f $_
}) -join " "
$h263SourceArguments = ('"{0}" "{1}" {2}' -f
    $imaAdpcm, $aviDemux, $h263SourceArguments)
$amrAdapter = Join-Path $amrnb "src\amrnb_3gp.c"
$amrSources = @(Get-ChildItem -LiteralPath (Join-Path $amrPv "common\src") `
        -Filter "*.c" |
    Sort-Object Name | ForEach-Object { $_.FullName }) +
    @(Get-ChildItem -LiteralPath (Join-Path $amrPv "dec\src") `
        -Filter "*.c" |
    Sort-Object Name | ForEach-Object { $_.FullName })
$amrObjectDirectory = Join-Path $OutputDirectory "amrnb-obj"
New-Item -ItemType Directory -Force -Path $amrObjectDirectory | Out-Null
Get-ChildItem -LiteralPath $amrObjectDirectory -Filter "*.obj" |
    Remove-Item -Force
$amrResponse = Join-Path $OutputDirectory "amrnb-compile.rsp"
$amrArguments = @(
    ('/FI"{0}"' -f (Join-Path $amrnb "include\amrnb_port.h")),
    ('/I"{0}"' -f (Join-Path $amrnb "include")),
    ('/I"{0}"' -f (Join-Path $amrPv "common\include")),
    ('/I"{0}"' -f (Join-Path $amrPv "dec\src")),
    ('/I"{0}"' -f (Join-Path $amrPv "include"))
) + @($amrSources | ForEach-Object { '"{0}"' -f $_ })
$amrArguments | Set-Content -LiteralPath $amrResponse -Encoding ascii
$amrLibrary = Join-Path $OutputDirectory "amrnb.lib"
$output = Join-Path $OutputDirectory "hlvplay.exe"

$amrCommandTemplate =
    'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /c /O2 /W4 /utf-8 ' +
    '/D_CRT_SECURE_NO_WARNINGS @"{2}"'
$amrCommand = $amrCommandTemplate -f
    $devcmd, $amrObjectDirectory, $amrResponse
Write-Host "Building AMR-NB decoder library..."
$amrAdapterCommand =
    'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /c /O2 /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS ' +
    '/I"{2}" /I"{3}" /I"{4}" /I"{5}" "{6}"'
$amrAdapterCommand = $amrAdapterCommand -f
    $devcmd, $amrObjectDirectory, (Join-Path $amrnb "include"),
    (Join-Path $amrPv "common\include"), (Join-Path $amrPv "dec\src"),
    (Join-Path $amrPv "include"), $amrAdapter
& cmd.exe /d /c $amrAdapterCommand
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while compiling the AMR-NB 3GP adapter."
}
& cmd.exe /d /c $amrCommand
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while compiling the AMR-NB decoder."
}
$amrLibraryResponse = Join-Path $OutputDirectory "amrnb-library.rsp"
@(Get-ChildItem -LiteralPath $amrObjectDirectory -Filter "*.obj" |
    Sort-Object Name |
    ForEach-Object { '"{0}"' -f $_.FullName }) |
    Set-Content -LiteralPath $amrLibraryResponse -Encoding ascii
$libraryCommand =
    'call "{0}" -no_logo -arch=x64 && lib /nologo /OUT:"{1}" @"{2}"' -f
    $devcmd, $amrLibrary, $amrLibraryResponse
& cmd.exe /d /c $libraryCommand
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while archiving the AMR-NB decoder."
}

$profileDefine = if ($H263StageProfile) {
    "/DPV_H263_STAGE_PROFILE=1"
}
else {
    "/DPV_H263_STAGE_PROFILE=0"
}
$commandTemplate = 'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 ' +
    '/D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE ' +
    '/DH263_PACKET_BUFFER_BYTES={18} {19} ' +
    '/I"{2}" /I"{3}" /I"{4}" /I"{5}" /I"{6}" /I"{7}" /I"{8}" /I"{9}" ' +
    '"{10}" "{11}" "{12}" "{13}" "{14}" {15} "{16}" ' +
    '/Fe:"{17}" /link /SUBSYSTEM:WINDOWS'
$command = $commandTemplate -f $devcmd, $OutputDirectory, $include, `
    $bpvInclude, $plMpeg, $compactInclude, (Join-Path $h263 "include"), `
    (Join-Path $pv "include"), (Join-Path $pv "src"), `
    (Join-Path $amrnb "include"), $player, $common, $decoder, `
    $bpvDecoder, $mpegDecoder, $h263SourceArguments, $amrLibrary, $output, `
    $H263PacketBufferBytes, $profileDefine

Write-Host "Building hlvplay..."
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while building hlvplay."
}

Write-Host (
    "Windows HLV/BPV/MPEG-1/H.263/MPEG-4 SP/AMR-NB/AVI+PCM player " +
    "is ready: $output"
)
