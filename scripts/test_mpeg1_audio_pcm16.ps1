#requires -Version 7.4

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$work = Join-Path $repo "build\mpeg1-audio-pcm16-test"
$fixture = Join-Path $work "mp2-tone.mpg"
$test = Join-Path $work "test_audio_pcm16.exe"
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
if (-not (Test-Path -LiteralPath $ffmpeg -PathType Leaf)) {
    throw "Repository-local FFmpeg is unavailable. Run setup.ps1."
}
$vswhere =
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1
if (-not $installation) {
    throw "Visual Studio C++ tools are missing."
}
$devcmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
New-Item -ItemType Directory -Force -Path $work | Out-Null

& $ffmpeg -hide_banner -loglevel error -y `
    -f lavfi -i "testsrc2=size=32x32:rate=25:duration=1" `
    -f lavfi -i "sine=frequency=997:sample_rate=32000:duration=1" `
    -map 0:v:0 -map 1:a:0 `
    -c:v mpeg1video -g 25 -bf 0 -pix_fmt yuv420p `
    -c:a mp2 -sample_fmt s16 -b:a 64k -ac 1 -ar 32000 `
    -f mpeg $fixture
if ($LASTEXITCODE -ne 0) {
    throw "Could not generate the signed PCM16-to-MP2 fixture."
}

$include = Join-Path $repo "third_party\pl_mpeg"
$commonInclude = Join-Path $repo "codecs\common\include"
$testSource = Join-Path $repo "codecs\mpeg1\tests\test_audio_pcm16.c"
$decoderSource = Join-Path $repo "codecs\mpeg1\src\pl_mpeg.c"
$command = (
    'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /O2 /W4 /DPLM_AUDIO_MONO_S16=1 /I"{2}" /I"{3}" ' +
    '"{4}" "{5}" /Fe:"{6}"'
) -f $devcmd, $work, $include, $commonInclude, $testSource, $decoderSource, $test
& cmd.exe /d /c $command | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed to build the MP2 PCM16 regression test."
}

& $test $fixture
if ($LASTEXITCODE -ne 0) {
    throw "MP2 signed PCM16 regression failed."
}
