#!/usr/bin/env python3
"""Generate compact C VLC tables from the clean-room go-msmpeg4 project.

The input is expected to be a checkout of:
https://github.com/mgvs/go-msmpeg4
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess


def named_block(text: str, name: str) -> str:
    match = re.search(
        rf"\b{re.escape(name)}\b[^\n]*\{{(.*?)\n\}}",
        text,
        re.S,
    )
    if not match:
        raise ValueError(f"cannot find {name}")
    return match.group(1)


def map_block(text: str, name: str) -> str:
    match = re.search(
        rf"var {re.escape(name)}\b.*?raw := map.*?\{{(.*?)\n\t\}}",
        text,
        re.S,
    )
    if not match:
        raise ValueError(f"cannot find map {name}")
    return match.group(1)


def bits_value(bits: str) -> int:
    return int(bits, 2)


def emit_array(
    out: list[str], c_type: str, name: str, entries: list[tuple[int, ...]]
) -> None:
    out.append(f"static const {c_type} {name}[] = {{")
    for entry in entries:
        out.append("    {" + ", ".join(str(value) for value in entry) + "},")
    out.append("};")
    out.append("")


def emit_mcbpc(
    out: list[str], name: str, entries: list[tuple[int, ...]]
) -> None:
    out.append(f"static const Divx3McbpcVlc {name}[] = {{")
    for length, code, *cbp in entries:
        out.append(
            f"    {{{length}, {code}, "
            "{" + ", ".join(str(value) for value in cbp) + "}},"
        )
    out.append("};")
    out.append("")


def parse_dc(root: pathlib.Path) -> dict[str, list[tuple[int, int, int]]]:
    text = (root / "dc_table.go").read_text(encoding="utf-8")
    result: dict[str, list[tuple[int, int, int]]] = {}
    for table_index in range(2):
        for chroma in range(2):
            name = f"dcRaw{table_index}_{chroma}"
            match = re.search(
                rf"\b{re.escape(name)}\b\s*:=\s*map\[string\]int\s*\{{"
                rf"(.*?)\n\t\}}",
                text,
                re.S,
            )
            if not match:
                raise ValueError(f"cannot find {name}")
            block = match.group(1)
            entries = [
                (len(bits), bits_value(bits), int(value))
                for bits, value in re.findall(r'"([01]+)"\s*:\s*(\d+)', block)
            ]
            result[name] = sorted(entries)
    return result


def parse_tcoef(
    root: pathlib.Path,
) -> dict[str, list[tuple[int, int, int, int, int]]]:
    text = "\n".join(
        (root / name).read_text(encoding="utf-8")
        for name in (
            "tcoef_table.go",
            "tcoef_tables_extra.go",
            "tcoef_table_inter.go",
        )
    )
    names = (
        "tcoefTable0VLC",
        "tcoefTable1VLC",
        "tcoefTable2VLC",
        "tcoefLumaVLC",
        "tcoefChromaVLC",
        "tcoefInterVLC",
    )
    result: dict[str, list[tuple[int, int, int, int, int]]] = {}
    for name in names:
        block = named_block(text, name)
        entries: list[tuple[int, int, int, int, int]] = []
        for match in re.finditer(r"\{([^{}]+)\}", block):
            body = match.group(1)
            values = [
                int(value, 0)
                for value in re.findall(r"(?:0b[01]+|-?\d+)", body)
            ]
            if len(values) == 5:
                run, level, last, length, code = values
                entries.append((length, code, run, level, last))
        if not entries:
            raise ValueError(f"no entries in {name}")
        result[name] = sorted(entries)
    return result


def parse_motion(root: pathlib.Path) -> dict[str, list[tuple[int, int, int, int]]]:
    text = (root / "pframe_mv_vlc.go").read_text(encoding="utf-8")
    result: dict[str, list[tuple[int, int, int, int]]] = {}
    for name in ("mvVLC0", "mvVLC1"):
        block = map_block(text, name)
        entries = [
            (len(bits), bits_value(bits), int(x), int(y))
            for bits, x, y in re.findall(
                r'"([01]+)"\s*:\s*\{\s*(-?\d+)\s*,\s*(-?\d+)\s*\}',
                block,
            )
        ]
        result[name] = sorted(entries)
    return result


def parse_mb(root: pathlib.Path) -> list[tuple[int, int, int, int]]:
    text = (root / "pframe_vlc.go").read_text(encoding="utf-8")
    block = map_block(text, "mbNonIntraVLC")
    entries = [
        (len(bits), bits_value(bits), int(intra), int(cbp))
        for bits, intra, cbp in re.findall(
            r'"([01]+)"\s*:\s*\{\s*(\d+)\s*,\s*(\d+)\s*\}', block
        )
    ]
    return sorted(entries)


def parse_mcbpc(root: pathlib.Path) -> list[tuple[int, ...]]:
    text = (root / "mcbpc_table.go").read_text(encoding="utf-8")
    block = named_block(text, "mcbpcVLC")
    entries = []
    for values in re.findall(
        r"\{\s*(\d+)\s*,\s*(0b[01]+|\d+)\s*,\s*"
        r"(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*"
        r"(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
        block,
    ):
        length, code, cb, cr, y0, y1, y2, y3 = values
        entries.append(
            (
                int(length),
                int(code, 0),
                int(y0),
                int(y1),
                int(y2),
                int(y3),
                int(cb),
                int(cr),
            )
        )
    return sorted(entries)


def maxima(
    entries: list[tuple[int, int, int, int, int]]
) -> tuple[list[list[int]], list[list[int]]]:
    max_level = [[0] * 64 for _ in range(2)]
    max_run = [[0] * 64 for _ in range(2)]
    for _length, _code, run, level, last in entries:
        if run < 64:
            max_level[last][run] = max(max_level[last][run], level)
        if level < 64:
            max_run[last][level] = max(max_run[last][level], run)
    return max_level, max_run


def emit_matrix(out: list[str], name: str, matrix: list[list[int]]) -> None:
    out.append(f"static const uint8_t {name}[2][64] = {{")
    for row in matrix:
        out.append("    {")
        for offset in range(0, 64, 16):
            values = ", ".join(str(value) for value in row[offset : offset + 16])
            out.append(f"        {values},")
        out.append("    },")
    out.append("};")
    out.append("")


def revision(root: pathlib.Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def generate(root: pathlib.Path) -> str:
    dc = parse_dc(root)
    tcoef = parse_tcoef(root)
    motion = parse_motion(root)
    mb = parse_mb(root)
    mcbpc = parse_mcbpc(root)
    out = [
        "/* Generated by tools/gen_tables.py; do not edit manually.",
        " * Source: https://github.com/mgvs/go-msmpeg4",
        f" * Revision: {revision(root)}",
        " */",
        "",
    ]
    for name, entries in dc.items():
        emit_array(out, "Divx3DcVlc", f"k{name}", entries)
    for name, entries in tcoef.items():
        emit_array(out, "Divx3TcoefVlc", f"k{name}", entries)
        max_level, max_run = maxima(entries)
        emit_matrix(out, f"k{name}MaxLevel", max_level)
        emit_matrix(out, f"k{name}MaxRun", max_run)
    for name, entries in motion.items():
        emit_array(out, "Divx3MotionVlc", f"k{name}", entries)
    emit_array(out, "Divx3MbVlc", "kMbNonIntraVlc", mb)
    emit_mcbpc(out, "kMcbpcVlc", mcbpc)

    set_specs = {
        "kLumaSets": (
            ("tcoefTable0VLC", 0b0010110, 7),
            ("tcoefTable2VLC", 0b001001010, 9),
            ("tcoefLumaVLC", 0b0000011, 7),
        ),
        "kChromaSets": (
            ("tcoefTable1VLC", 0b000001101, 9),
            ("tcoefChromaVLC", 0b101101001, 9),
            ("tcoefInterVLC", 0b0000011, 7),
        ),
    }
    for set_name, specs in set_specs.items():
        out.append(f"static const Divx3TcoefSet {set_name}[3] = {{")
        for name, escape, escape_length in specs:
            out.append(
                "    {"
                f"k{name}, sizeof(k{name}) / sizeof(k{name}[0]), "
                f"{escape}, {escape_length}, "
                f"k{name}MaxLevel, k{name}MaxRun"
                "},"
            )
        out.append("};")
        out.append("")
    return "\n".join(out)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("upstream", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    args.output.write_text(generate(args.upstream.resolve()), encoding="utf-8")


if __name__ == "__main__":
    main()
