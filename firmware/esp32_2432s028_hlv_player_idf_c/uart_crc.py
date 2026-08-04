#!/usr/bin/env python3
"""Calculate CRC32 checksums for files on the ESP32 player's SD card."""

from __future__ import annotations

import argparse
import json
import serial
import sys
import time
from dataclasses import asdict, dataclass

from uart_baud import BaudError, begin_session, enable_monitoring
from uart_list import list_files, open_port


VIDEO_SUFFIXES = (".avi", ".bpv1", ".hlv", ".mpg", ".mpeg")


class CrcError(RuntimeError):
    pass


@dataclass(frozen=True)
class CrcRecord:
    name: str
    size: int
    crc32: str


def checksum_file(
    port: serial.Serial, name: str, timeout: float
) -> CrcRecord:
    port.write(f"\nHLVCRC 1 {name}\n".encode("ascii"))
    port.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if line.startswith("HLVERR "):
            raise CrcError(f"ESP32 rejected {name}: {line}")
        if not line.startswith("HLVCRC 1 "):
            continue
        fields = line.split(" ", 4)
        if len(fields) != 5:
            raise CrcError(f"malformed CRC record: {line}")
        try:
            size = int(fields[2])
            checksum = int(fields[3], 16)
        except ValueError as error:
            raise CrcError(f"invalid CRC record: {line}") from error
        if fields[4] != name or size < 0 or not 0 <= checksum <= 0xFFFFFFFF:
            raise CrcError(f"unexpected CRC record: {line}")
        return CrcRecord(name, size, f"{checksum:08x}")
    raise CrcError(f"timeout waiting for CRC32 of {name}")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Calculate CRC32 for files in /HLV on the ESP32 SD card"
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=900.0,
                        help="timeout for each file in seconds")
    parser.add_argument("--all", action="store_true",
                        help="include non-video files such as play.txt")
    parser.add_argument("--json", action="store_true")
    return parser.parse_args(sys.argv[1:] if argv is None else argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.timeout <= 0:
        raise CrcError("--timeout must be positive")
    files = list_files(args.port, min(args.timeout, 30.0))
    selected = [
        record for record in files
        if args.all or record.name.casefold().endswith(VIDEO_SUFFIXES)
    ]
    results: list[CrcRecord] = []
    try:
        port = open_port(args.port)
    except serial.SerialException as error:
        raise CrcError(
            f"cannot open {args.port}; close the serial monitor first: {error}"
        ) from error
    with port:
        for index, record in enumerate(selected, 1):
            print(
                f"[{index}/{len(selected)}] {record.name}",
                file=sys.stderr, flush=True
            )
            try:
                begin_session(port, "CRC32", min(args.timeout, 30.0))
            except BaudError as error:
                raise CrcError(str(error)) from error
            result = checksum_file(port, record.name, args.timeout)
            if result.size != record.size:
                raise CrcError(
                    f"{record.name} changed while checksumming: "
                    f"{record.size} -> {result.size} bytes"
                )
            results.append(result)
        try:
            enable_monitoring(port, min(args.timeout, 30.0))
        except BaudError as error:
            raise CrcError(str(error)) from error
    if args.json:
        print(json.dumps([asdict(record) for record in results],
                         ensure_ascii=False, indent=2))
    else:
        for record in results:
            print(f"{record.crc32}  {record.size:12d}  {record.name}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CrcError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
