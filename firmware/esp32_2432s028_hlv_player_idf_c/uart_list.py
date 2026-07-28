#!/usr/bin/env python3
"""List files in the ESP32 player's /HLV directory over UART0."""

from __future__ import annotations

import argparse
import json
import serial
import sys
import time
from dataclasses import asdict, dataclass


CONTROL_BAUD = 460_800


class ListError(RuntimeError):
    pass


@dataclass(frozen=True)
class FileRecord:
    name: str
    size: int


def open_port(name: str) -> serial.Serial:
    port = serial.Serial()
    port.port = name
    port.baudrate = CONTROL_BAUD
    port.bytesize = serial.EIGHTBITS
    port.parity = serial.PARITY_NONE
    port.stopbits = serial.STOPBITS_ONE
    port.timeout = 0.2
    port.write_timeout = 10
    port.dtr = False
    port.rts = False
    port.open()
    port.reset_input_buffer()
    return port


def list_files(port_name: str, timeout: float) -> list[FileRecord]:
    try:
        port = open_port(port_name)
    except serial.SerialException as error:
        raise ListError(
            f"cannot open {port_name}; close the serial monitor first: {error}"
        ) from error

    records: list[FileRecord] = []
    expected_count: int | None = None
    deadline = time.monotonic() + timeout
    with port:
        port.write(b"\nHLVLIST 1\n")
        port.flush()
        started = False
        while time.monotonic() < deadline:
            raw = port.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if line.startswith("HLVERR "):
                raise ListError(f"ESP32 rejected the request: {line}")
            if line == "HLVLISTBEGIN 1":
                started = True
                continue
            if line.startswith("HLVFILE 1 "):
                if not started:
                    raise ListError("received a file before HLVLISTBEGIN")
                fields = line.split(" ", 3)
                if len(fields) != 4:
                    raise ListError(f"malformed file record: {line}")
                try:
                    size = int(fields[2])
                except ValueError as error:
                    raise ListError(f"invalid file size: {line}") from error
                if size < 0 or not fields[3]:
                    raise ListError(f"invalid file record: {line}")
                records.append(FileRecord(fields[3], size))
                continue
            if line.startswith("HLVLISTEND 1 "):
                fields = line.split()
                if len(fields) != 3:
                    raise ListError(f"malformed completion record: {line}")
                try:
                    expected_count = int(fields[2])
                except ValueError as error:
                    raise ListError(
                        f"invalid completion count: {line}"
                    ) from error
                break
    if expected_count is None:
        raise ListError("timeout waiting for HLVLISTEND")
    if expected_count != len(records):
        raise ListError(
            f"ESP32 reported {expected_count} files, received {len(records)}"
        )
    return sorted(records, key=lambda record: record.name.casefold())


def format_size(size: int) -> str:
    if size < 1024:
        return f"{size} B"
    if size < 1024 * 1024:
        return f"{size / 1024:.1f} KiB"
    return f"{size / (1024 * 1024):.2f} MiB"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="List files in /HLV on the ESP32 player's SD card"
    )
    parser.add_argument("--port", required=True,
                        help="serial port, for example COM8")
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--json", action="store_true")
    return parser.parse_args(sys.argv[1:] if argv is None else argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.timeout <= 0:
        raise ListError("--timeout must be positive")
    records = list_files(args.port, args.timeout)
    if args.json:
        print(json.dumps([asdict(record) for record in records],
                         ensure_ascii=False, indent=2))
    else:
        print(f"/HLV: {len(records)} file(s)")
        for record in records:
            print(f"{format_size(record.size):>12}  {record.name}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ListError as error:
        print(f"uart_list: {error}", file=sys.stderr)
        raise SystemExit(1)
