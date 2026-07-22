#!/usr/bin/env python3
"""Compare codecs at the same measured file bitrate.

Each source is normalized once to FFV1 YUV420p.  The script searches a codec's
integer quality control, selects the closest actual bitrate, and only then
measures PSNR/SSIM so the final result is genuinely rate matched.
"""

from __future__ import annotations

import argparse
import csv
import glob
import json
import math
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path

from benchmark import make_reference, normalize_filter, psnr, ssim, reference_pipe, run, source_duration

ROOT = Path(__file__).resolve().parents[1]
HLVENC = ROOT / "hlvenc"
HLVDEC = ROOT / "hlvdec"
RESULTS = ROOT / "bench" / "results"


@dataclass
class MatchedResult:
    source: str
    duration_s: float
    frames: int
    target_kbps: float
    codec: str
    setting: str
    bytes: int
    bitrate_kbps: float
    bitrate_error_pct: float
    encode_s: float
    encode_fps: float
    decode_s: float
    decode_fps: float
    psnr_y: float
    psnr_u: float
    psnr_v: float
    psnr_avg: float
    ssim_y: float = 0.0
    ssim_u: float = 0.0
    ssim_v: float = 0.0
    ssim_all: float = 0.0


@dataclass
class Candidate:
    parameter: int
    setting: str
    path: Path
    bytes: int
    bitrate_kbps: float
    encode_s: float


def parse_list(value: str, cast=float):
    return [cast(x.strip()) for x in value.split(",") if x.strip()]


def codec_domain(codec: str, args=None) -> list[int]:
    # Ordered from lowest expected bitrate to highest expected bitrate.
    if codec == "hlv":
        if args is not None and args.hlv_search_mode == "qstep":
            maximum = 2040 if args.hlv_syntax >= 4 else 255
            return list(range(maximum, 0, -1))
        return list(range(1, 101))
    if codec == "mjpeg":
        return list(range(31, 1, -1))
    if codec in {"mpeg1", "mpeg2"}:
        return list(range(31, 0, -1))
    if codec == "h264":
        return list(range(51, 0, -1))
    if codec == "vp8":
        # libvpx VP8 accepts CQ levels 4..63.
        return list(range(63, 3, -1))
    if codec == "vp9":
        return list(range(63, -1, -1))
    if codec == "av1":
        return list(range(63, -1, -1))
    raise ValueError(codec)


def ffmpeg_args(codec: str, value: int, fps: int) -> tuple[list[str], str, str]:
    if codec == "mjpeg":
        return ["-c:v", "mjpeg", "-q:v", str(value), "-pix_fmt", "yuvj420p"], f"q={value}", "mkv"
    if codec == "mpeg1":
        return ["-c:v", "mpeg1video", "-q:v", str(value), "-g", str(fps * 2), "-bf", "0"], f"q={value}", "mkv"
    if codec == "mpeg2":
        return ["-c:v", "mpeg2video", "-q:v", str(value), "-g", str(fps * 2), "-bf", "0"], f"q={value}", "mkv"
    if codec == "h264":
        return [
            "-c:v", "libx264", "-preset", "medium", "-profile:v", "baseline",
            "-crf", str(value), "-g", str(fps * 2), "-bf", "0", "-refs", "1",
            "-pix_fmt", "yuv420p",
        ], f"crf={value}", "mkv"
    if codec == "vp8":
        return [
            "-c:v", "libvpx", "-deadline", "good", "-cpu-used", "4",
            "-crf", str(value), "-b:v", "0", "-g", str(fps * 2),
            "-pix_fmt", "yuv420p",
        ], f"crf={value}", "mkv"
    if codec == "vp9":
        return [
            "-c:v", "libvpx-vp9", "-deadline", "good", "-cpu-used", "4",
            "-crf", str(value), "-b:v", "0", "-g", str(fps * 2),
            "-pix_fmt", "yuv420p",
        ], f"crf={value}", "mkv"
    if codec == "av1":
        return [
            "-c:v", "libaom-av1", "-cpu-used", "6", "-crf", str(value),
            "-b:v", "0", "-g", str(fps * 2), "-pix_fmt", "yuv420p",
        ], f"crf={value}", "mkv"
    raise ValueError(codec)


def encode_candidate(codec: str, value: int, ref: Path, frames: int,
                     duration: float, fps: int, work: Path, args) -> Candidate:
    if codec == "hlv":
        quant_args: list[str]
        if args.hlv_search_mode == "qstep":
            maximum = 2040 if args.hlv_syntax >= 4 else 255
            q_uv = max(1, min(maximum, int(value * args.hlv_chroma_scale + 0.5)))
            quant_args = ["--qstep-y", str(value), "--qstep-uv", str(q_uv)]
            quant_label = f"qy={value},quv={q_uv}"
        else:
            quant_args = ["--quality", str(value)]
            quant_label = f"q={value}"
        setting = (
            f"{quant_label},{args.hlv_preset},v={args.hlv_syntax},"
            f"cs={args.hlv_chroma_scale:g},lw={args.hlv_luma_weight},"
            f"ls={args.hlv_lambda_scale:g},dz={args.hlv_ac_deadzone:g}"
        )
        out = work / ("hlv_" + setting.replace(",", "_").replace("=", "") + ".hlv")
        src = reference_pipe(ref, frames)
        assert src.stdout is not None
        t0 = time.perf_counter()
        run([
            str(HLVENC), "-", str(out), *quant_args,
            "--preset", args.hlv_preset, "--syntax", str(args.hlv_syntax),
            "--chroma-scale", str(args.hlv_chroma_scale),
            "--rd-luma-weight", str(args.hlv_luma_weight),
            "--rd-lambda-scale", str(args.hlv_lambda_scale),
            "--ac-deadzone", str(args.hlv_ac_deadzone),
            "--max-frames", str(frames),
        ], stdin=src.stdout)
        src.stdout.close()
        err = src.stderr.read() if src.stderr else b""
        rc = src.wait()
        if rc:
            raise RuntimeError("reference pipe failed\n" + err.decode(errors="replace"))
        elapsed = time.perf_counter() - t0
    else:
        cargs, setting, ext = ffmpeg_args(codec, value, fps)
        out = work / f"{codec}_{setting.replace('=', '')}.{ext}"
        t0 = time.perf_counter()
        run([
            "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
            "-i", str(ref), "-an", "-frames:v", str(frames), *cargs, str(out),
        ])
        elapsed = time.perf_counter() - t0
    size = out.stat().st_size
    return Candidate(value, setting, out, size, size * 8 / duration / 1000, elapsed)


def nearest_candidate(codec: str, target: float, ref: Path, frames: int,
                      duration: float, fps: int, work: Path, args,
                      cache: dict[int, Candidate] | None = None) -> Candidate:
    domain = codec_domain(codec, args)
    if cache is None:
        cache = {}

    def get(index: int) -> Candidate:
        parameter = domain[index]
        if parameter not in cache:
            cache[parameter] = encode_candidate(codec, parameter, ref, frames, duration, fps, work, args)
        return cache[parameter]

    lo, hi = 0, len(domain) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        c = get(mid)
        if c.bitrate_kbps < target:
            lo = mid + 1
        elif c.bitrate_kbps > target:
            hi = mid - 1
        else:
            return c

    # Test the bracketing neighbours and one extra point on either side.  Some
    # source/codec combinations are only approximately monotonic.
    indices = {0, len(domain) - 1}
    for i in (lo - 2, lo - 1, lo, lo + 1, hi - 1, hi, hi + 1):
        if 0 <= i < len(domain):
            indices.add(i)
    candidates = [get(i) for i in sorted(indices)]
    return min(candidates, key=lambda c: (abs(c.bitrate_kbps - target), -c.bitrate_kbps))


def finalize(source: Path, codec: str, target: float, candidate: Candidate,
             ref: Path, duration: float, frames: int, work: Path, hlv_syntax: int) -> MatchedResult:
    if codec == "hlv":
        decoded = work / f"decoded_target_{target:g}.y4m"
        t0 = time.perf_counter()
        run([str(HLVDEC), str(candidate.path), os.devnull], stdout=subprocess.DEVNULL)
        decode_s = time.perf_counter() - t0
        run([str(HLVDEC), str(candidate.path), str(decoded)], stdout=subprocess.DEVNULL)
        py, pu, pv, pa = psnr(ref, decoded, duration)
        sy, su, sv, sa = ssim(ref, decoded, duration)
        decoded.unlink(missing_ok=True)
        label = f"HLV-v{hlv_syntax}"
    else:
        t0 = time.perf_counter()
        run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(candidate.path), "-f", "null", "-"])
        decode_s = time.perf_counter() - t0
        py, pu, pv, pa = psnr(ref, candidate.path, duration)
        sy, su, sv, sa = ssim(ref, candidate.path, duration)
        label = codec
    return MatchedResult(
        source=source.name,
        duration_s=duration,
        frames=frames,
        target_kbps=target,
        codec=label,
        setting=candidate.setting,
        bytes=candidate.bytes,
        bitrate_kbps=candidate.bitrate_kbps,
        bitrate_error_pct=100.0 * (candidate.bitrate_kbps - target) / target,
        encode_s=candidate.encode_s,
        encode_fps=frames / candidate.encode_s,
        decode_s=decode_s,
        decode_fps=frames / decode_s,
        psnr_y=py,
        psnr_u=pu,
        psnr_v=pv,
        psnr_avg=pa,
        ssim_y=sy,
        ssim_u=su,
        ssim_v=sv,
        ssim_all=sa,
    )


def save(rows: list[MatchedResult], prefix: str) -> None:
    RESULTS.mkdir(parents=True, exist_ok=True)
    jp = RESULTS / f"{prefix}.json"
    cp = RESULTS / f"{prefix}.csv"
    tmp = jp.with_suffix(".json.tmp")
    tmp.write_text(json.dumps([asdict(r) for r in rows], indent=2), encoding="utf-8")
    tmp.replace(jp)
    if rows:
        with cp.open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=list(asdict(rows[0]).keys()))
            writer.writeheader()
            writer.writerows(asdict(r) for r in rows)
    print(f"Wrote {jp} and {cp}", file=sys.stderr)


def key(row: MatchedResult):
    return row.source, row.frames, row.target_kbps, row.codec, row.setting.split(",q=")[0]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sources", nargs="+", required=True)
    ap.add_argument("--duration", type=float, default=10.0)
    ap.add_argument("--fps", type=int, default=15)
    ap.add_argument("--targets", default="200,400,800")
    ap.add_argument("--codecs", default="hlv,mjpeg,mpeg1,h264,vp8")
    ap.add_argument("--prefix", default="matched_bitrate")
    ap.add_argument("--work-dir", type=Path)
    ap.add_argument("--keep-files", action="store_true")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--hlv-preset", default="balanced", choices=["fast", "balanced", "slow"])
    ap.add_argument("--hlv-search-mode", default="qstep", choices=["qstep", "quality"],
                    help="use exact 1..255 qsteps for finer rate matching")
    ap.add_argument("--hlv-syntax", type=int, default=10,
                    choices=[1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
    ap.add_argument("--hlv-chroma-scale", type=float, default=1.35)
    ap.add_argument("--hlv-luma-weight", type=int, default=4)
    ap.add_argument("--hlv-lambda-scale", type=float, default=1.0)
    ap.add_argument("--hlv-ac-deadzone", type=float, default=1.0)
    args = ap.parse_args()

    if not HLVENC.exists() or not HLVDEC.exists():
        raise SystemExit("Build HLV first with make")
    targets = parse_list(args.targets, float)
    codecs = [x.strip() for x in args.codecs.split(",") if x.strip()]
    unknown = [c for c in codecs if c not in {"hlv", "mjpeg", "mpeg1", "mpeg2", "h264", "vp8", "vp9", "av1"}]
    if unknown:
        raise SystemExit("Unknown codecs: " + ", ".join(unknown))
    if any(t <= 0 for t in targets):
        raise SystemExit("Targets must be positive")

    sources: list[Path] = []
    for pattern in args.sources:
        matches = [Path(x) for x in glob.glob(pattern)]
        sources.extend(matches or [Path(pattern)])
    sources = [p for p in sources if p.exists()]
    if not sources:
        raise SystemExit("No source files")

    result_path = RESULTS / f"{args.prefix}.json"
    rows = [MatchedResult(**r) for r in json.loads(result_path.read_text())] if args.resume and result_path.exists() else []
    done = {(r.source, r.frames, r.target_kbps, r.codec) for r in rows}

    owned = None
    if args.work_dir:
        base = args.work_dir
    elif args.keep_files:
        base = ROOT / "bench" / "work" / args.prefix
    else:
        owned = tempfile.mkdtemp(prefix="hlvmatched_")
        base = Path(owned)
    base.mkdir(parents=True, exist_ok=True)

    try:
        for source in sources:
            limit = source_duration(source, args.duration)
            frames = max(1, int(limit * args.fps + 0.5))
            duration = frames / args.fps
            work = base / source.stem
            work.mkdir(parents=True, exist_ok=True)
            ref = work / "reference_ffv1.mkv"
            make_reference(source, limit, args.fps, frames, ref)
            candidate_caches: dict[str, dict[int, Candidate]] = {c: {} for c in codecs}
            for target in targets:
                for codec in codecs:
                    label = f"HLV-v{args.hlv_syntax}" if codec == "hlv" else codec
                    if (source.name, frames, target, label) in done:
                        print(f"= resume {source.name} {target:g} {label}", file=sys.stderr)
                        continue
                    candidate = nearest_candidate(codec, target, ref, frames, duration, args.fps,
                                                  work, args, candidate_caches[codec])
                    row = finalize(source, codec, target, candidate, ref, duration, frames, work, args.hlv_syntax)
                    rows.append(row)
                    done.add((source.name, frames, target, label))
                    save(rows, args.prefix)
    finally:
        if owned:
            shutil.rmtree(owned, ignore_errors=True)
    save(rows, args.prefix)


if __name__ == "__main__":
    main()