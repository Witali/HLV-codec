#!/usr/bin/env python3
"""Benchmark exact HLV encoder variants and compare algorithmic work."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import statistics
import subprocess
import tempfile
from pathlib import Path


VARIANTS = {
    "scalar-1": (1, "off"),
    "sse2-1": (1, "auto"),
    "scalar-4": (4, "off"),
    "sse2-4": (4, "auto"),
}

TIME_RE = re.compile(r"Encoded \d+ frames in ([0-9.]+) s")
WORK_RE = re.compile(r"^Encoder work: (.+)$", re.MULTILINE)
BIT_WORK_RE = re.compile(r"^Encoder bit work: (.+)$", re.MULTILINE)
OPS_RE = re.compile(
    r"^Encoder primitive operation estimate: ([0-9.]+) total",
    re.MULTILINE,
)
FIELD_RE = re.compile(r"([a-z_]+)=([0-9/]+)")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def run_checked(command: list[str], **kwargs: object) -> subprocess.CompletedProcess:
    result = subprocess.run(command, **kwargs)
    if result.returncode:
        stdout = getattr(result, "stdout", "") or ""
        stderr = getattr(result, "stderr", "") or ""
        raise RuntimeError(
            f"{' '.join(command)} failed ({result.returncode})\n"
            f"{stdout}\n{stderr}"
        )
    return result


def make_fixture(
    ffmpeg: Path, source: Path, output: Path, start: float, frames: int
) -> None:
    command = [
        str(ffmpeg),
        "-y",
        "-hide_banner",
        "-loglevel",
        "error",
        "-ss",
        f"{start:g}",
        "-i",
        str(source),
        "-an",
        "-vf",
        "fps=24,scale=320:180:flags=lanczos,format=yuv420p",
        "-frames:v",
        str(frames),
        "-f",
        "yuv4mpegpipe",
        str(output),
    ]
    run_checked(command)


def parse_fields(line: str) -> dict[str, str]:
    return {key: value for key, value in FIELD_RE.findall(line)}


def encode_once(
    encoder: Path,
    fixture: Path,
    root: Path,
    variant: str,
    run_number: int,
) -> dict[str, object]:
    threads, simd = VARIANTS[variant]
    output = root / f"{variant}-{run_number}.hlv"
    reconstruction = root / f"{variant}-{run_number}.y4m"
    command = [
        str(encoder),
        str(fixture),
        str(output),
        "--preset",
        "balanced",
        "--quality",
        "45",
        "--gop",
        "30",
        "--syntax",
        "13",
        "--threads",
        str(threads),
        "--simd",
        simd,
        "--recon",
        str(reconstruction),
    ]
    result = run_checked(command, capture_output=True, text=True)
    time_match = TIME_RE.search(result.stderr)
    work_match = WORK_RE.search(result.stderr)
    bit_match = BIT_WORK_RE.search(result.stderr)
    ops_match = OPS_RE.search(result.stderr)
    if not time_match or not work_match or not bit_match or not ops_match:
        raise RuntimeError(f"Cannot parse encoder report:\n{result.stderr}")
    work = parse_fields(work_match.group(1))
    work.update(parse_fields(bit_match.group(1)))
    return {
        "seconds": float(time_match.group(1)),
        "operations": int(float(ops_match.group(1))),
        "work": work,
        "hlv_sha256": digest(output),
        "reconstruction_sha256": digest(reconstruction),
    }


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--encoder", type=Path, default=repo / "build/msvc/hlvenc.exe"
    )
    parser.add_argument(
        "--ffmpeg",
        type=Path,
        default=repo / "local_tools/ffmpeg/bin/ffmpeg.exe",
    )
    parser.add_argument("--start", type=float, default=120.0)
    parser.add_argument("--frames", type=int, default=360)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument(
        "--variants",
        default="scalar-1,sse2-1,scalar-4,sse2-4",
        help="comma-separated: scalar-1,sse2-1,scalar-4,sse2-4",
    )
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    if args.frames < 1 or args.runs < 1:
        parser.error("--frames and --runs must be positive")
    variants = [
        item for item in re.split(r"[,\s]+", args.variants.strip()) if item
    ]
    unknown = [item for item in variants if item not in VARIANTS]
    if unknown:
        parser.error(f"unknown variants: {', '.join(unknown)}")

    source = (
        repo
        / "out/sources/big_buck_bunny_1080p_h264"
        / "big_buck_bunny_1080p_h264.mov"
    )
    for required in (source, args.encoder, args.ffmpeg):
        if not required.is_file():
            raise FileNotFoundError(required)

    report: dict[str, object] = {
        "source": str(source),
        "start_seconds": args.start,
        "frames": args.frames,
        "runs": args.runs,
        "variants": {},
    }
    reference_hashes: tuple[str, str] | None = None
    reference_work: dict[str, str] | None = None

    with tempfile.TemporaryDirectory(prefix="hlv-encoder-benchmark-") as temp:
        root = Path(temp)
        fixture = root / "input.y4m"
        make_fixture(args.ffmpeg.resolve(), source, fixture, args.start, args.frames)

        for variant in variants:
            print(f"Warm-up {variant}...", flush=True)
            encode_once(args.encoder.resolve(), fixture, root, variant, 0)
            samples = []
            for run_number in range(1, args.runs + 1):
                print(f"Run {run_number}/{args.runs} {variant}...", flush=True)
                samples.append(
                    encode_once(
                        args.encoder.resolve(),
                        fixture,
                        root,
                        variant,
                        run_number,
                    )
                )

            hashes = (
                str(samples[0]["hlv_sha256"]),
                str(samples[0]["reconstruction_sha256"]),
            )
            work = dict(samples[0]["work"])
            operations = int(samples[0]["operations"])
            for sample in samples[1:]:
                if (
                    sample["hlv_sha256"],
                    sample["reconstruction_sha256"],
                ) != hashes:
                    raise AssertionError(f"{variant}: hashes vary between runs")
                if sample["work"] != work or sample["operations"] != operations:
                    raise AssertionError(f"{variant}: work counters vary between runs")
            if reference_hashes is None:
                reference_hashes = hashes
                reference_work = work
            elif hashes != reference_hashes:
                raise AssertionError(f"{variant}: output differs from reference")
            elif work != reference_work:
                raise AssertionError(f"{variant}: logical work differs from reference")

            times = [float(sample["seconds"]) for sample in samples]
            report["variants"][variant] = {
                "seconds": times,
                "median_seconds": statistics.median(times),
                "fps": args.frames / statistics.median(times),
                "operations": operations,
                "operations_per_frame": operations / args.frames,
                "work": work,
                "hlv_sha256": hashes[0],
                "reconstruction_sha256": hashes[1],
            }

    print("\n| Variant | Median | Throughput | Primitive ops/frame |")
    print("|---|---:|---:|---:|")
    for variant in variants:
        item = report["variants"][variant]
        print(
            f"| {variant} | {item['median_seconds']:.3f} s | "
            f"{item['fps']:.2f} fps | {item['operations_per_frame']:.0f} |"
        )
    if reference_hashes:
        print(f"\nHLV SHA-256: `{reference_hashes[0]}`")
        print(f"Reconstruction SHA-256: `{reference_hashes[1]}`")

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
