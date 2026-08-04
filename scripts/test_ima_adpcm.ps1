[CmdletBinding()]
param(
    [string]$OutputDirectory = "build\ima-adpcm-test"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$vswhere =
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1
if (-not $installation) {
    throw "Visual Studio C++ tools are missing."
}

$devcmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
$include = Join-Path $repo "codecs\common\include"
$testSource = Join-Path $repo "codecs\common\tests\test_ima_adpcm.c"
$codecSource = Join-Path $repo "codecs\common\src\ima_adpcm.c"
$wavDecoderSource = Join-Path $repo (
    "codecs\common\tests\decode_ima_adpcm_wav_block.c"
)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$output = Join-Path $OutputDirectory "test_ima_adpcm.exe"
$command = (
    'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /O2 /W4 /D_CRT_SECURE_NO_WARNINGS /TC /I"{2}" ' +
    '"{3}" "{4}" /Fe:"{5}"'
) -f $devcmd, $OutputDirectory, $include, $testSource, `
    $codecSource, $output
& cmd.exe /d /c $command | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while building IMA ADPCM tests."
}

& $output
if ($LASTEXITCODE -ne 0) {
    throw "IMA ADPCM tests failed."
}

$wavDecoder = Join-Path $OutputDirectory "decode_ima_adpcm_wav_block.exe"
$wavCommand = (
    'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /O2 /W4 /D_CRT_SECURE_NO_WARNINGS /TC /I"{2}" ' +
    '"{3}" "{4}" /Fe:"{5}"'
) -f $devcmd, $OutputDirectory, $include, $wavDecoderSource, `
    $codecSource, $wavDecoder
& cmd.exe /d /c $wavCommand | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while building the WAV IMA oracle decoder."
}

$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
if (-not (Test-Path -LiteralPath $ffmpeg)) {
    throw "Repository-local FFmpeg is unavailable."
}
$fixture = Join-Path $OutputDirectory "ima-reference.wav"
$block = Join-Path $OutputDirectory "ima-reference.block"
$referencePcm = Join-Path $OutputDirectory "ima-reference-ffmpeg.s16le"
$decodedPcm = Join-Path $OutputDirectory "ima-reference-common.s16le"
& $ffmpeg -hide_banner -loglevel error -y `
    -f lavfi -i "sine=frequency=997:sample_rate=32000:duration=1" `
    -c:a adpcm_ima_wav -ar 32000 -ac 1 $fixture
if ($LASTEXITCODE -ne 0) { throw "Could not create WAV IMA fixture." }
& $ffmpeg -hide_banner -loglevel error -y -i $fixture `
    -map 0:a:0 -c:a copy -frames:a 1 -f data $block
if ($LASTEXITCODE -ne 0) { throw "Could not extract WAV IMA block." }
& $ffmpeg -hide_banner -loglevel error -y -i $fixture `
    -map 0:a:0 -frames:a 1 -f s16le $referencePcm
if ($LASTEXITCODE -ne 0) { throw "Could not decode WAV IMA oracle." }
& $wavDecoder $block $decodedPcm
if ($LASTEXITCODE -ne 0) { throw "Common WAV IMA decoder failed." }
$referenceHash = (Get-FileHash -Algorithm SHA256 $referencePcm).Hash
$decodedHash = (Get-FileHash -Algorithm SHA256 $decodedPcm).Hash
if ($referenceHash -ne $decodedHash) {
    throw (
        "WAV IMA output differs from FFmpeg: " +
        "reference=$referenceHash decoded=$decodedHash"
    )
}
Write-Host "WAV IMA FFmpeg equivalence passed: $decodedHash"
