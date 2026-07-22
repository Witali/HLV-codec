#!/usr/bin/env python3
"""Smoke-test bounded local two-pass rate control through an FFmpeg Y4M pipe."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=True, text=True, **kwargs)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--target-kbps", type=float, default=300.0)
    parser.add_argument("--tolerance", type=float, default=0.12)
    args = parser.parse_args()

    project = args.project.resolve()
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("ffmpeg is required")

    with tempfile.TemporaryDirectory(prefix="hlv-window-test-") as directory:
        work = Path(directory)
        y4m = work / "source.y4m"
        hlv = work / "encoded.hlv"
        decoded = work / "decoded.y4m"
        csv = work / "windows.csv"

        run([
            ffmpeg, "-hide_banner", "-loglevel", "error",
            "-f", "lavfi", "-i", "testsrc2=size=160x120:rate=10:duration=6",
            "-pix_fmt", "yuv420p", "-f", "yuv4mpegpipe", str(y4m),
        ])
        run([
            str(project / "hlvenc"), str(y4m), str(hlv),
            "--preset", "fast",
            "--bitrate", str(int(args.target_kbps)),
            "--two-pass-window", "3",
            "--two-pass-trials", "5",
            "--two-pass-log", str(csv),
        ], capture_output=True)
        run([str(project / "hlvdec"), str(hlv), str(decoded)], capture_output=True)

        duration = 6.0
        actual_kbps = hlv.stat().st_size * 8.0 / duration / 1000.0
        relative = abs(actual_kbps - args.target_kbps) / args.target_kbps
        if relative > args.tolerance:
            raise SystemExit(
                f"bitrate miss: target={args.target_kbps:.1f}, "
                f"actual={actual_kbps:.1f} kbit/s ({relative:.1%})"
            )
        if decoded.stat().st_size <= y4m.stat().st_size * 0.95:
            raise SystemExit("decoded Y4M is unexpectedly short")
        rows = csv.read_text(encoding="utf-8").strip().splitlines()
        if len(rows) != 3:  # header plus two 3-second windows
            raise SystemExit(f"unexpected window log length: {len(rows)}")
        print(
            f"windowed two-pass PASS: target={args.target_kbps:.1f}, "
            f"actual={actual_kbps:.1f} kbit/s, windows={len(rows)-1}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())