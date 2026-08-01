#!/usr/bin/env python3
"""Summarize ESP32 decode and display timings over representative windows."""

from __future__ import annotations

import argparse
import csv
import statistics
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Test:
    codec: str
    source: str
    filename: str
    frames: int
    fps: float


def read_manifest(path: Path) -> list[Test]:
    with path.open(newline="", encoding="utf-8-sig") as source:
        return [
            Test(
                codec=row["codec"],
                source=row["source"],
                filename=row["filename"],
                frames=int(row["frames"]),
                fps=float(row["fps"]),
            )
            for row in csv.DictReader(source)
            if row.get("enabled", "true").strip().lower() != "false"
        ]


def read_timings(path: Path) -> list[dict[str, int]]:
    with path.open(newline="", encoding="utf-8-sig") as source:
        return [
            {key: int(value) for key, value in row.items()}
            for row in csv.DictReader(source)
        ]


def metric(values: list[int], prefix: str) -> dict[str, str | int]:
    return {
        f"{prefix}_samples": len(values),
        f"{prefix}_avg_us": f"{statistics.fmean(values):.1f}",
        f"{prefix}_min_us": min(values),
        f"{prefix}_max_us": max(values),
    }


def summarize_window(
    test: Test,
    name: str,
    rows: list[dict[str, int]],
) -> dict[str, str | int]:
    decode = [row["decode_us"] for row in rows if row["decode_us"] > 0]
    render = [row["render_us"] for row in rows if row["render_us"] > 0]
    if not decode or not render:
        raise ValueError(f"{test.filename} {name}: empty timing metric")
    result: dict[str, str | int] = {
        "codec": test.codec,
        "source": test.source,
        "filename": test.filename,
        "window": name,
        "frame_first": rows[0]["frame"],
        "frame_last": rows[-1]["frame"],
        "frame_records": len(rows),
        "decode_zero_records": len(rows) - len(decode),
        "display_skips": len(rows) - len(render),
    }
    result.update(metric(decode, "decode"))
    result.update(metric(render, "render"))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--input-directory", type=Path, required=True)
    parser.add_argument("--window-seconds", type=int, default=60)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.window_seconds <= 0:
        parser.error("--window-seconds must be positive")

    summaries: list[dict[str, str | int]] = []
    for test in read_manifest(args.manifest):
        expected = max(
            1,
            round(
                test.fps
                * min(args.window_seconds, test.frames / test.fps)
            ),
        )
        windows: list[tuple[str, list[dict[str, int]]]] = []
        for name in ("start", "middle", "end"):
            rows = read_timings(
                args.input_directory / f"{test.filename}.{name}.csv"
            )
            if len(rows) != expected:
                raise ValueError(
                    f"{test.filename} {name}: expected {expected} records, "
                    f"found {len(rows)}"
                )
            windows.append((name, rows))
        for name, rows in windows:
            summaries.append(summarize_window(test, name, rows))
        summaries.append(
            summarize_window(
                test,
                "combined",
                [row for _, rows in windows for row in rows],
            )
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=list(summaries[0]))
        writer.writeheader()
        writer.writerows(summaries)
    print(f"wrote {len(summaries)} rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
