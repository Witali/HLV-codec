#!/usr/bin/env python3
"""Run reproducible HLV and reference-codec rate-distortion benchmarks.

Every codec is fed from the same normalized FFV1/YUV420 reference.  Results are
written after each completed point and can be resumed after interruption.
"""

from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HLVENC = ROOT / "codecs" / "hlv" / "hlvenc"
HLVDEC = ROOT / "codecs" / "hlv" / "hlvdec"
BPVENC = ROOT / "codecs" / "bpv" / "tools" / "bpv1enc.js"
BPVDEC = ROOT / "codecs" / "bpv" / "tools" / "bpv1dec.js"
RESULTS = ROOT / "bench" / "results"

PSNR_RE = re.compile(
    r"PSNR y:([0-9.]+|inf) u:([0-9.]+|inf) v:([0-9.]+|inf) average:([0-9.]+|inf)"
)
SSIM_RE = re.compile(
    r"SSIM Y:([0-9.]+) \([^)]*\) U:([0-9.]+) \([^)]*\) "
    r"V:([0-9.]+) \([^)]*\) All:([0-9.]+)"
)


@dataclass
class Result:
    source: str
    duration_s: float
    frames: int
    codec: str
    setting: str
    bytes: int
    bitrate_kbps: float
    encode_s: float
    encode_fps: float
    decode_s: float
    decode_fps: float
    psnr_y: float
    psnr_u: float
    psnr_v: float
    psnr_avg: float


def parse_list(s: str, cast=int):
    return [cast(x.strip()) for x in s.split(",") if x.strip()]


def run(cmd, *, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True):
    print("+", " ".join(shlex.quote(str(x)) for x in cmd), file=sys.stderr)
    try:
        return subprocess.run(cmd, stdin=stdin, stdout=stdout, stderr=stderr, check=check)
    except subprocess.CalledProcessError as exc:
        if exc.stderr:
            print(exc.stderr.decode(errors="replace"), file=sys.stderr)
        raise


def source_duration(path: Path, limit: float) -> float:
    p = run([
        "ffprobe", "-v", "error", "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=1", str(path),
    ])
    duration = float(p.stdout.decode().strip())
    return min(duration, limit) if limit > 0 else duration


def normalize_filter(fps: int) -> str:
    # Preserve source aspect ratio and letterbox to the normative HLV test size.
    return (
        f"fps={fps},"
        "scale=320:240:force_original_aspect_ratio=decrease:flags=lanczos,"
        "pad=320:240:(ow-iw)/2:(oh-ih)/2:black,format=yuv420p"
    )


def psnr(ref: Path, dist: Path, duration: float) -> tuple[float, float, float, float]:
    p = run([
        "ffmpeg", "-hide_banner", "-nostats", "-loglevel", "info",
        "-t", f"{duration:.9f}", "-i", str(ref),
        "-t", f"{duration:.9f}", "-i", str(dist),
        "-lavfi", "[0:v]format=yuv420p[ref];[1:v]format=yuv420p[dist];[ref][dist]psnr",
        "-f", "null", "-",
    ])
    text = p.stderr.decode(errors="replace")
    matches = PSNR_RE.findall(text)
    if not matches:
        raise RuntimeError("Cannot parse PSNR output\n" + text[-2000:])

    def cv(x: str) -> float:
        return float("inf") if x == "inf" else float(x)

    return tuple(cv(x) for x in matches[-1])  # type: ignore


def ssim(ref: Path, dist: Path, duration: float) -> tuple[float, float, float, float]:
    p = run([
        "ffmpeg", "-hide_banner", "-nostats", "-loglevel", "info",
        "-t", f"{duration:.9f}", "-i", str(ref),
        "-t", f"{duration:.9f}", "-i", str(dist),
        "-lavfi", "[0:v]format=yuv420p[ref];[1:v]format=yuv420p[dist];[ref][dist]ssim",
        "-f", "null", "-",
    ])
    text = p.stderr.decode(errors="replace")
    matches = SSIM_RE.findall(text)
    if not matches:
        raise RuntimeError("Cannot parse SSIM output\n" + text[-2000:])
    return tuple(float(x) for x in matches[-1])  # type: ignore


def reference_pipe(ref: Path, frames: int):
    return subprocess.Popen([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(ref),
        "-an", "-frames:v", str(frames), "-pix_fmt", "yuv420p",
        "-f", "yuv4mpegpipe", "-",
    ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def make_reference(source: Path, source_limit: float, fps: int, frames: int, out: Path) -> None:
    if out.exists():
        # A persistent work directory may already contain the normalized reference.
        return
    run([
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-t", f"{source_limit:.9f}", "-i", str(source), "-an",
        "-vf", normalize_filter(fps), "-frames:v", str(frames),
        "-c:v", "ffv1", "-level", "3", "-pix_fmt", "yuv420p", str(out),
    ])


def bench_hlv(source: Path, ref: Path, duration: float, frames: int,
              quality: int, preset: str, syntax: int, work: Path) -> Result:
    stem = f"hlv_v{syntax}_q{quality}_{preset}"
    out = work / f"{stem}.hlv"
    dec = work / f"{stem}.y4m"
    src = reference_pipe(ref, frames)
    assert src.stdout is not None
    t0 = time.perf_counter()
    run([
        str(HLVENC), "-", str(out), "--quality", str(quality),
        "--preset", preset, "--syntax", str(syntax), "--max-frames", str(frames),
    ], stdin=src.stdout)
    src.stdout.close()
    stderr = src.stderr.read() if src.stderr else b""
    rc = src.wait()
    if rc != 0:
        raise RuntimeError("FFmpeg reference pipe failed\n" + stderr.decode(errors="replace"))
    enc_s = time.perf_counter() - t0

    t0 = time.perf_counter()
    run([str(HLVDEC), str(out), os.devnull], stdout=subprocess.DEVNULL)
    dec_s = time.perf_counter() - t0

    run([str(HLVDEC), str(out), str(dec)], stdout=subprocess.DEVNULL)
    py, pu, pv, pa = psnr(ref, dec, duration)
    size = out.stat().st_size
    dec.unlink(missing_ok=True)
    return Result(
        source.name, duration, frames, f"HLV-v{syntax}", f"q={quality},{preset}", size,
        size * 8 / duration / 1000, enc_s, frames / enc_s,
        dec_s, frames / dec_s, py, pu, pv, pa,
    )


def bench_bpv(source: Path, ref: Path, duration: float, frames: int,
              fps: int, lambda_value: int, work: Path) -> Result:
    stem = f"bpv1_v2_lambda{lambda_value}"
    out = work / f"{stem}.bpv1"
    dec = work / f"{stem}.y4m"
    src = reference_pipe(ref, frames)
    assert src.stdout is not None
    t0 = time.perf_counter()
    run([
        "node", str(BPVENC), "-", str(out),
        "--lambda", str(lambda_value),
        "--gop", str(max(1, fps * 2)),
        "--max-frames", str(frames),
        "--no-progress",
    ], stdin=src.stdout)
    src.stdout.close()
    stderr = src.stderr.read() if src.stderr else b""
    rc = src.wait()
    if rc != 0:
        raise RuntimeError("FFmpeg reference pipe failed\n" + stderr.decode(errors="replace"))
    enc_s = time.perf_counter() - t0

    t0 = time.perf_counter()
    run([
        "node", str(BPVDEC), str(out), "-", "--no-progress",
    ], stdout=subprocess.DEVNULL)
    dec_s = time.perf_counter() - t0

    run([
        "node", str(BPVDEC), str(out), str(dec), "--no-progress",
    ], stdout=subprocess.DEVNULL)
    py, pu, pv, pa = psnr(ref, dec, duration)
    size = out.stat().st_size
    dec.unlink(missing_ok=True)
    return Result(
        source.name, duration, frames, "BPV1-v6", f"lambda={lambda_value}", size,
        size * 8 / duration / 1000, enc_s, frames / enc_s,
        dec_s, frames / dec_s, py, pu, pv, pa,
    )


def ffmpeg_codec_args(codec: str, value: int, fps: int) -> tuple[list[str], str, str]:
    if codec == "mjpeg":
        return ["-c:v", "mjpeg", "-q:v", str(value), "-pix_fmt", "yuvj420p"], f"q={value}", "avi"
    if codec == "mpeg1":
        return ["-c:v", "mpeg1video", "-q:v", str(value), "-g", str(fps * 2), "-bf", "0"], f"q={value}", "mpg"
    if codec == "mpeg2":
        return ["-c:v", "mpeg2video", "-q:v", str(value), "-g", str(fps * 2), "-bf", "0"], f"q={value}", "mpg"
    if codec == "h264":
        return [
            "-c:v", "libx264", "-preset", "medium", "-profile:v", "baseline",
            "-crf", str(value), "-g", str(fps * 2), "-bf", "0", "-refs", "1",
            "-pix_fmt", "yuv420p",
        ], f"crf={value}", "mp4"
    if codec == "vp8":
        return [
            "-c:v", "libvpx", "-deadline", "good", "-cpu-used", "4", "-crf", str(value),
            "-b:v", "0", "-g", str(fps * 2), "-pix_fmt", "yuv420p",
        ], f"crf={value}", "webm"
    if codec == "vp9":
        return [
            "-c:v", "libvpx-vp9", "-deadline", "good", "-cpu-used", "4", "-crf", str(value),
            "-b:v", "0", "-g", str(fps * 2), "-pix_fmt", "yuv420p",
        ], f"crf={value}", "webm"
    if codec == "av1":
        return [
            "-c:v", "libaom-av1", "-cpu-used", "6", "-crf", str(value), "-b:v", "0",
            "-g", str(fps * 2), "-pix_fmt", "yuv420p",
        ], f"crf={value}", "mkv"
    raise ValueError(codec)


def bench_ffmpeg(source: Path, ref: Path, duration: float, frames: int,
                  fps: int, codec: str, value: int, work: Path) -> Result:
    args, setting, ext = ffmpeg_codec_args(codec, value, fps)
    out = work / f"{codec}_{value}.{ext}"
    t0 = time.perf_counter()
    run([
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-i", str(ref),
        "-an", "-frames:v", str(frames), *args, str(out),
    ])
    enc_s = time.perf_counter() - t0
    t0 = time.perf_counter()
    run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(out), "-f", "null", "-"])
    dec_s = time.perf_counter() - t0
    py, pu, pv, pa = psnr(ref, out, duration)
    size = out.stat().st_size
    return Result(
        source.name, duration, frames, codec, setting, size,
        size * 8 / duration / 1000, enc_s, frames / enc_s,
        dec_s, frames / dec_s, py, pu, pv, pa,
    )


def result_key(source: str, frames: int, codec: str, setting: str) -> tuple[str, int, str, str]:
    return source, frames, codec, setting


def load_results(prefix: str) -> list[Result]:
    path = RESULTS / f"{prefix}.json"
    if not path.exists():
        return []
    data = json.loads(path.read_text(encoding="utf-8"))
    return [Result(**row) for row in data]


def write_results(rows: list[Result], prefix: str) -> None:
    if not rows:
        return
    RESULTS.mkdir(parents=True, exist_ok=True)
    js = RESULTS / f"{prefix}.json"
    csvp = RESULTS / f"{prefix}.csv"
    tmp = js.with_suffix(".json.tmp")
    tmp.write_text(json.dumps([asdict(r) for r in rows], indent=2), encoding="utf-8")
    tmp.replace(js)
    with csvp.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(asdict(rows[0]).keys()))
        w.writeheader()
        w.writerows(asdict(r) for r in rows)
    print(f"Wrote {js} and {csvp}", file=sys.stderr)


def parse_grid(args) -> dict[str, list[int]]:
    return {
        "bpv": parse_list(args.bpv_lambdas),
        "mjpeg": parse_list(args.mjpeg_values),
        "mpeg1": parse_list(args.mpeg1_values),
        "mpeg2": parse_list(args.mpeg2_values),
        "h264": parse_list(args.h264_values),
        "vp8": parse_list(args.vp8_values),
        "vp9": parse_list(args.vp9_values),
        "av1": parse_list(args.av1_values),
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sources", nargs="+", required=True)
    ap.add_argument("--duration", type=float, default=60)
    ap.add_argument("--fps", type=int, default=25)
    ap.add_argument("--hlv-qualities", default="40,55,70")
    ap.add_argument("--hlv-presets", default="fast,balanced")
    ap.add_argument("--hlv-syntaxes", default="14,15")
    ap.add_argument("--skip-hlv", action="store_true",
                    help="run only codecs selected by --codecs")
    ap.add_argument("--codecs", default="mjpeg,mpeg1,mpeg2,h264,vp8,vp9")
    ap.add_argument("--include-av1", action="store_true")
    ap.add_argument("--bpv-lambdas", default="0,16,64")
    ap.add_argument("--mjpeg-values", default="2,6,12")
    ap.add_argument("--mpeg1-values", default="2,6,12")
    ap.add_argument("--mpeg2-values", default="2,6,12")
    ap.add_argument("--h264-values", default="18,26,34")
    ap.add_argument("--vp8-values", default="10,28,46")
    ap.add_argument("--vp9-values", default="18,34,50")
    ap.add_argument("--av1-values", default="20,36,52")
    ap.add_argument("--prefix", default="benchmark")
    ap.add_argument("--resume", action="store_true", help="continue an existing result JSON")
    ap.add_argument("--work-dir", type=Path, help="persistent references and encoded files")
    ap.add_argument("--keep-files", action="store_true", help="keep default work directory under bench/work")
    args = ap.parse_args()

    if not args.skip_hlv and (not HLVENC.exists() or not HLVDEC.exists()):
        raise SystemExit("Build HLV tools first with make")
    if args.fps <= 0 or args.duration <= 0:
        raise SystemExit("fps and duration must be positive")

    sources: list[Path] = []
    for pat in args.sources:
        matches = [Path(x) for x in glob.glob(pat)]
        sources.extend(matches or [Path(pat)])
    sources = [x for x in sources if x.exists()]
    if not sources:
        raise SystemExit("No source files found")

    grids = parse_grid(args)
    codecs = [x.strip() for x in args.codecs.split(",") if x.strip()]
    if args.include_av1 and "av1" not in codecs:
        codecs.append("av1")
    unknown = [x for x in codecs if x not in grids]
    if unknown:
        raise SystemExit("Unknown codecs: " + ", ".join(unknown))
    if "bpv" in codecs and (
        not BPVENC.exists() or
        not BPVDEC.exists() or
        shutil.which("node") is None
    ):
        raise SystemExit("BPV benchmarks require Node.js and codecs/bpv tools")

    rows = load_results(args.prefix) if args.resume else []
    done = {result_key(r.source, r.frames, r.codec, r.setting) for r in rows}

    owned_temp = None
    if args.work_dir:
        base = args.work_dir
    elif args.keep_files:
        base = ROOT / "bench" / "work" / args.prefix
    else:
        owned_temp = tempfile.mkdtemp(prefix="hlvbench_")
        base = Path(owned_temp)
    base.mkdir(parents=True, exist_ok=True)

    try:
        for source in sources:
            source_limit = source_duration(source, args.duration)
            frames = max(1, int(source_limit * args.fps + 0.5))
            duration = frames / args.fps
            work = base / source.stem
            work.mkdir(parents=True, exist_ok=True)
            ref = work / "reference_ffv1.mkv"
            make_reference(source, source_limit, args.fps, frames, ref)

            if not args.skip_hlv:
                for syntax in parse_list(args.hlv_syntaxes):
                    for preset in [x.strip() for x in args.hlv_presets.split(",") if x.strip()]:
                        for q in parse_list(args.hlv_qualities):
                            codec = f"HLV-v{syntax}"
                            setting = f"q={q},{preset}"
                            key = result_key(source.name, frames, codec, setting)
                            if key in done:
                                print(f"= resume: {source.name} {codec} {setting}", file=sys.stderr)
                                continue
                            row = bench_hlv(source, ref, duration, frames, q, preset, syntax, work)
                            rows.append(row)
                            done.add(key)
                            write_results(rows, args.prefix)

            for codec in codecs:
                for val in grids[codec]:
                    if codec == "bpv":
                        label = "BPV1-v6"
                        setting = f"lambda={val}"
                    else:
                        label = codec
                        setting = f"q={val}" if codec in {"mjpeg", "mpeg1", "mpeg2"} else f"crf={val}"
                    key = result_key(source.name, frames, label, setting)
                    if key in done:
                        print(f"= resume: {source.name} {label} {setting}", file=sys.stderr)
                        continue
                    if codec == "bpv":
                        row = bench_bpv(
                            source, ref, duration, frames, args.fps, val, work,
                        )
                    else:
                        row = bench_ffmpeg(
                            source, ref, duration, frames, args.fps, codec, val, work,
                        )
                    rows.append(row)
                    done.add(key)
                    write_results(rows, args.prefix)
    finally:
        if owned_temp:
            shutil.rmtree(owned_temp, ignore_errors=True)

    write_results(rows, args.prefix)


if __name__ == "__main__":
    main()
