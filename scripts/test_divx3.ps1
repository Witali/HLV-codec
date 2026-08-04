[CmdletBinding()]
param(
    [string]$OutputDirectory = (
        Join-Path (Split-Path $PSScriptRoot -Parent) "build\divx3-tests"
    )
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$sourceRelative = (
    "out\sources\big_buck_bunny_1080p_h264\" +
    "big_buck_bunny_1080p_h264.mov"
)
$source = Join-Path $repo $sourceRelative
if (-not (Test-Path -LiteralPath $source)) {
    $worktrees = git -C $repo worktree list --porcelain
    foreach ($line in $worktrees) {
        if (-not $line.StartsWith("worktree ")) {
            continue
        }
        $candidate = Join-Path $line.Substring(9) $sourceRelative
        if (Test-Path -LiteralPath $candidate) {
            $source = $candidate
            break
        }
    }
}
if (-not (Test-Path -LiteralPath $source)) {
    throw "Required 1080p Big Buck Bunny source is missing: $sourceRelative"
}

$ffmpeg = (Get-Command ffmpeg -ErrorAction Stop).Source
$vswhere =
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1
if (-not $installation) {
    throw "Visual Studio C++ tools are missing."
}
$devcmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$testExe = Join-Path $OutputDirectory "test_divx3_decode.exe"
$inputAvi = Join-Path $OutputDirectory "bbb_divx3_256x144.avi"
$inputMp3Avi = Join-Path $OutputDirectory "bbb_divx3_mp3_256x144.avi"
$inputImaAvi = Join-Path $OutputDirectory "bbb_divx3_ima_256x144.avi"
$referenceYuv = Join-Path $OutputDirectory "reference.yuv"
$decodedYuv = Join-Path $OutputDirectory "decoded.yuv"
$decodedMp3Yuv = Join-Path $OutputDirectory "decoded_mp3.yuv"
$decodedImaYuv = Join-Path $OutputDirectory "decoded_ima.yuv"
$include = Join-Path $repo "codecs\divx3\include"
$compactInclude = Join-Path $repo "codecs\common\include"
$testSource = Join-Path $repo "codecs\divx3\tests\test_decode.c"
$decoderSource = Join-Path $repo "codecs\divx3\src\divx3_decode.c"
$aviSource = Join-Path $repo "codecs\divx3\src\divx3_avi.c"
$imaSource = Join-Path $repo "codecs\common\src\ima_adpcm.c"
$demuxSource = Join-Path $repo "codecs\common\src\avi_demux.c"

$compile = (
    'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /O2 /W4 /TC /D_CRT_SECURE_NO_WARNINGS ' +
    '/I"{2}" /I"{3}" "{4}" "{5}" "{6}" "{7}" "{8}" /Fe:"{9}"'
) -f $devcmd, $OutputDirectory, $include, $compactInclude, $testSource, `
    $decoderSource, $aviSource, $imaSource, $demuxSource, $testExe
& cmd.exe /d /c $compile | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while building the DivX 3 regression test."
}

& $ffmpeg -hide_banner -loglevel error -y -ss 00:00:00 -t 1 `
    -i $source -vf "fps=12,scale=256:144:flags=lanczos" -an `
    -c:v msmpeg4 -q:v 4 -g 12 -bf 0 -vtag DIV3 $inputAvi
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg failed while creating the DivX 3 fixture."
}
& $ffmpeg -hide_banner -loglevel error -y -i $inputAvi `
    -pix_fmt yuv420p -f rawvideo $referenceYuv
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg failed while decoding the reference YUV."
}

$decoderResult = & $testExe $inputAvi $decodedYuv
if ($LASTEXITCODE -ne 0) {
    throw "Portable DivX 3 decoder regression failed."
}
$referenceHash = (Get-FileHash -Algorithm SHA256 $referenceYuv).Hash
$decodedHash = (Get-FileHash -Algorithm SHA256 $decodedYuv).Hash
if ($referenceHash -ne $decodedHash) {
    throw (
        "Decoded pixels differ from FFmpeg: " +
        "reference=$referenceHash decoder=$decodedHash"
    )
}

& $ffmpeg -hide_banner -loglevel error -y -ss 00:00:00 -t 1 `
    -i $source -vf "fps=12,scale=256:144:flags=lanczos" `
    -c:v msmpeg4 -q:v 4 -g 12 -bf 0 -vtag DIV3 `
    -c:a mp3 -b:a 64k -ac 1 -ar 16000 $inputMp3Avi
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg failed while creating the DivX 3 + MP3 fixture."
}
$mp3DecoderResult = & $testExe $inputMp3Avi $decodedMp3Yuv
if ($LASTEXITCODE -ne 0) {
    throw "Portable decoder rejected a DivX 3 AVI with MP3 audio."
}
$decodedMp3Hash = (Get-FileHash -Algorithm SHA256 $decodedMp3Yuv).Hash
if ($referenceHash -ne $decodedMp3Hash) {
    throw (
        "Ignoring unsupported MP3 audio changed decoded video pixels: " +
        "reference=$referenceHash decoded=$decodedMp3Hash"
    )
}

& $ffmpeg -hide_banner -loglevel error -y -ss 00:00:00 -t 1 `
    -i $source -vf "fps=12,scale=256:144:flags=lanczos" `
    -c:v msmpeg4 -q:v 4 -g 12 -bf 0 -vtag DIV3 `
    -c:a adpcm_ima_wav -ac 1 -ar 48000 $inputImaAvi
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg failed while creating the DivX 3 + IMA fixture."
}
$imaDecoderResult = & $testExe $inputImaAvi $decodedImaYuv
if ($LASTEXITCODE -ne 0) {
    throw "Portable decoder rejected a DivX 3 AVI with IMA audio."
}
$decodedImaHash = (Get-FileHash -Algorithm SHA256 $decodedImaYuv).Hash
if ($referenceHash -ne $decodedImaHash) {
    throw (
        "IMA audio changed decoded video pixels: " +
        "reference=$referenceHash decoded=$decodedImaHash"
    )
}

$imaRateResults = @()
foreach ($rate in @(8000, 16000, 22050, 32000, 44100)) {
    $rateInput = Join-Path $OutputDirectory "bbb_divx3_ima_${rate}hz.avi"
    $rateDecoded = Join-Path $OutputDirectory "decoded_ima_${rate}hz.yuv"
    & $ffmpeg -hide_banner -loglevel error -y -ss 00:00:00 -t 1 `
        -i $source -vf "fps=12,scale=256:144:flags=lanczos" `
        -c:v msmpeg4 -q:v 4 -g 12 -bf 0 -vtag DIV3 `
        -c:a adpcm_ima_wav -ac 1 -ar $rate $rateInput
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg failed while creating the ${rate} Hz IMA fixture."
    }
    $rateResult = & $testExe $rateInput $rateDecoded
    if ($LASTEXITCODE -ne 0) {
        throw "Portable decoder rejected ${rate} Hz IMA audio."
    }
    $rateHash = (Get-FileHash -Algorithm SHA256 $rateDecoded).Hash
    if ($referenceHash -ne $rateHash) {
        throw "${rate} Hz IMA audio changed decoded video pixels."
    }
    $imaRateResults += $rateResult
}

Write-Host $decoderResult
Write-Host $mp3DecoderResult
Write-Host $imaDecoderResult
Write-Host $imaRateResults
Write-Host "IMA AVI sample-rate matrix passed: 8, 16, 22.05, 32, 44.1 and 48 kHz"
Write-Host "DivX 3 pixel-exact regression passed: $decodedHash"
