#!/usr/bin/env python3
"""List files in the ESP32 player's /HLV directory over UART0."""

from __future__ import annotations

import argparse
import json
import serial
import struct
import sys
import time
import zlib
from dataclasses import asdict, dataclass

from uart_baud import BaudError, begin_session, enable_monitoring


CONTROL_BAUD = 1_000_000
LIST_MAGIC = b"HLVL"
LIST_HEADER = struct.Struct("<4sIIBI")
MAX_PACKET_ATTEMPTS = 20


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
    port.stopbits = serial.STOPBITS_TWO
    port.timeout = 0.2
    port.write_timeout = 10
    port.dtr = False
    port.rts = False
    port.open()
    try:
        port.set_buffer_size(rx_size=1024 * 1024, tx_size=64 * 1024)
    except (AttributeError, NotImplementedError, OSError):
        pass
    port.reset_input_buffer()
    return port


def _read_exact(port: serial.Serial, count: int, deadline: float) -> bytes:
    result = bytearray()
    while len(result) < count and time.monotonic() < deadline:
        chunk = port.read(count - len(result))
        if chunk:
            result.extend(chunk)
    if len(result) != count:
        raise ListError(f"timeout after {len(result)} of {count} bytes")
    return bytes(result)


def _find_magic(port: serial.Serial, deadline: float) -> None:
    window = bytearray()
    while time.monotonic() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        window.extend(byte)
        if len(window) > len(LIST_MAGIC):
            del window[0]
        if bytes(window) == LIST_MAGIC:
            return
    raise ListError("timeout waiting for an HLVLIST packet")


def _send_ack(port: serial.Serial, sequence: int, accepted: bool) -> None:
    prefix = struct.pack("<4sIB", b"HLVA", sequence, int(accepted))
    port.write(prefix + struct.pack("<I", zlib.crc32(prefix) & 0xFFFFFFFF))
    port.flush()


def list_files(port_name: str, timeout: float) -> list[FileRecord]:
    try:
        port = open_port(port_name)
    except serial.SerialException as error:
        raise ListError(
            f"cannot open {port_name}; close the serial monitor first: {error}"
        ) from error

    records: list[FileRecord] = []
    expected_sequence = 0
    failures = 0
    with port:
        try:
            begin_session(port, "LIST", timeout)
        except BaudError as error:
            raise ListError(str(error)) from error
        deadline = time.monotonic() + timeout
        port.write(b"\nHLVLIST 2\n")
        port.flush()
        while time.monotonic() < deadline:
            _find_magic(port, deadline)
            remainder = _read_exact(
                port, LIST_HEADER.size - len(LIST_MAGIC), deadline
            )
            header = LIST_MAGIC + remainder
            _, sequence, size, name_size, checksum = LIST_HEADER.unpack(header)
            name_bytes = _read_exact(port, name_size, deadline)
            calculated = zlib.crc32(header[:13])
            calculated = zlib.crc32(name_bytes, calculated) & 0xFFFFFFFF
            if calculated != checksum or sequence > expected_sequence:
                _send_ack(port, expected_sequence, False)
                failures += 1
                if failures >= MAX_PACKET_ATTEMPTS:
                    raise ListError("too many corrupt HLVLIST packets")
                continue
            if sequence < expected_sequence:
                _send_ack(port, sequence, True)
                continue
            if name_size == 0:
                if size != expected_sequence:
                    _send_ack(port, expected_sequence, False)
                    failures += 1
                    continue
                _send_ack(port, sequence, True)
                result = sorted(
                    records, key=lambda record: record.name.casefold()
                )
                try:
                    enable_monitoring(port, timeout)
                except BaudError as error:
                    raise ListError(str(error)) from error
                return result
            try:
                name = name_bytes.decode("utf-8")
            except UnicodeDecodeError:
                _send_ack(port, expected_sequence, False)
                failures += 1
                continue
            if not name or "\x00" in name:
                _send_ack(port, expected_sequence, False)
                failures += 1
                continue
            records.append(FileRecord(name, size))
            _send_ack(port, sequence, True)
            expected_sequence += 1
            failures = 0
    raise ListError("timeout waiting for the HLVLIST terminator")


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
