#!/usr/bin/env python3
"""Aggregate benchmark JSON into compact Markdown and CSV reports."""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path


def fmt(v, digits=2):
    if isinstance(v, float) and math.isinf(v):
        return "inf"
    return f"{v:.{digits}f}"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("input", type=Path)
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()

    rows = json.loads(args.input.read_text(encoding="utf-8"))
    if not rows:
        raise SystemExit("No benchmark rows")
    output = args.output or args.input.with_name(args.input.stem + "_report.md")

    by_source = defaultdict(list)
    for row in rows:
        by_source[row["source"]].append(row)

    lines = [
        f"# HLV-1 benchmark report: {args.input.stem}", "",
        "All bitrates use the encoded file size. PSNR is measured against the common normalized FFV1 reference.", "",
    ]

    for source, items in sorted(by_source.items()):
        lines += [f"## {source}", "", "| Codec | Setting | kbps | PSNR avg | Enc fps | Dec fps |", "|---|---:|---:|---:|---:|---:|"]
        for r in sorted(items, key=lambda x: (x["bitrate_kbps"], x["codec"], x["setting"])):
            lines.append(
                f"| {r['codec']} | {r['setting']} | {fmt(r['bitrate_kbps'])} | "
                f"{fmt(r['psnr_avg'])} | {fmt(r['encode_fps'], 1)} | {fmt(r['decode_fps'], 1)} |"
            )
        lines.append("")

    # Aggregate the same codec/setting over all sources.
    agg = defaultdict(list)
    for r in rows:
        agg[(r["codec"], r["setting"])].append(r)
    aggregate_rows = []
    for (codec, setting), vals in sorted(agg.items()):
        aggregate_rows.append({
            "codec": codec,
            "setting": setting,
            "sources": len(vals),
            "bitrate_kbps_mean": sum(x["bitrate_kbps"] for x in vals) / len(vals),
            "psnr_avg_mean": sum(x["psnr_avg"] for x in vals) / len(vals),
            "encode_fps_hmean": len(vals) / sum(1 / max(x["encode_fps"], 1e-9) for x in vals),
            "decode_fps_hmean": len(vals) / sum(1 / max(x["decode_fps"], 1e-9) for x in vals),
        })

    lines += ["## Aggregate mean", "", "| Codec | Setting | Sources | Mean kbps | Mean PSNR | Enc fps (harmonic) | Dec fps (harmonic) |", "|---|---:|---:|---:|---:|---:|---:|"]
    for r in sorted(aggregate_rows, key=lambda x: (x["bitrate_kbps_mean"], x["codec"], x["setting"])):
        lines.append(
            f"| {r['codec']} | {r['setting']} | {r['sources']} | "
            f"{fmt(r['bitrate_kbps_mean'])} | {fmt(r['psnr_avg_mean'])} | "
            f"{fmt(r['encode_fps_hmean'], 1)} | {fmt(r['decode_fps_hmean'], 1)} |"
        )
    lines.append("")

    output.write_text("\n".join(lines), encoding="utf-8")
    csv_path = output.with_suffix(".csv")
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(aggregate_rows[0].keys()))
        writer.writeheader()
        writer.writerows(aggregate_rows)
    print(output)
    print(csv_path)


if __name__ == "__main__":
    main()