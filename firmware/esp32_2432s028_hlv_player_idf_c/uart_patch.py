#!/usr/bin/env python3
"""Replace one byte range in an existing ESP32 SD-card file."""

import argparse
import pathlib
import struct
import sys
import time
import zlib

import serial

from uart_baud import BaudError, begin_session, change_baud, enable_monitoring
from uart_upload import (
    CONTROL_BAUD,
    DEFAULT_DATA_BAUD,
    SUPPORTED_DATA_BAUDS,
    VALID_NAME,
    open_port,
    print_progress,
)


PATCH_PROTOCOL_VERSION = 1
PATCH_MAGIC = b"HLVP"
PATCH_HEADER = struct.Struct("<4sIHI")
MAX_ATTEMPTS = 5
PATCH_FINALIZE_BYTES_PER_SECOND = 128 * 1024


class PatchError(RuntimeError):
    pass


def read_line(port: serial.Serial, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("ascii", errors="replace").strip()
        if line.startswith("HLV"):
            return line
    raise PatchError("timeout waiting for the ESP32 response")


def wait_for(port: serial.Serial, prefixes: tuple[str, ...],
             timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = read_line(port, max(deadline - time.monotonic(), 0.001))
        if line.startswith("HLVERR "):
            raise PatchError(f"ESP32 rejected the patch: {line}")
        if line.startswith(prefixes):
            return line
    raise PatchError("timeout waiting for the ESP32 response")


def range_crc(path: pathlib.Path, offset: int, size: int) -> int:
    checksum = 0
    remaining = size
    with path.open("rb") as source:
        source.seek(offset)
        while remaining:
            data = source.read(min(remaining, 1024 * 1024))
            if not data:
                raise PatchError("source ended inside the selected range")
            checksum = zlib.crc32(data, checksum)
            remaining -= len(data)
    return checksum & 0xFFFFFFFF


def make_packet(sequence: int, data: bytes) -> bytes:
    checksum = zlib.crc32(data) & 0xFFFFFFFF
    return PATCH_HEADER.pack(PATCH_MAGIC, sequence, len(data), checksum) + data


def patch_finalize_timeout(size: int, timeout: float) -> float:
    """Allow the ESP32 to back up, apply, flush, and verify a large range."""
    return max(
        timeout * 4,
        timeout + size / PATCH_FINALIZE_BYTES_PER_SECOND,
    )


def patch(path: pathlib.Path, port_name: str, remote_name: str,
          remote_offset: int, source_offset: int, size: int,
          data_baud: int, timeout: float) -> int:
    if len(remote_name) > 48 or not VALID_NAME.fullmatch(remote_name):
        raise PatchError("invalid destination filename")
    source_size = path.stat().st_size
    if source_offset < 0 or source_offset > source_size or size <= 0 or \
            size > source_size - source_offset:
        raise PatchError("source range is outside the input file")
    if remote_offset < 0 or remote_offset > 0xFFFFFFFF or \
            size > 0xFFFFFFFF - remote_offset:
        raise PatchError("destination range exceeds FAT32 limits")

    checksum = range_crc(path, source_offset, size)
    try:
        port = open_port(port_name, CONTROL_BAUD)
    except serial.SerialException as error:
        raise PatchError(
            f"cannot open {port_name}; close the serial monitor first: {error}"
        ) from error

    with port:
        if data_baud != CONTROL_BAUD:
            try:
                begin_session(port, "BAUD", timeout)
                change_baud(port, data_baud, timeout)
            except BaudError as error:
                raise PatchError(str(error)) from error
        try:
            begin_session(port, "PATCH", timeout)
        except BaudError as error:
            raise PatchError(str(error)) from error
        command = (
            f"\nHLVPATCH {PATCH_PROTOCOL_VERSION} {remote_name} "
            f"{remote_offset} {size} {checksum:08x} {data_baud}\n"
        ).encode("ascii")
        port.write(command)
        port.flush()

        ready_values = None
        for _ in range(MAX_ATTEMPTS):
            line = wait_for(port, ("HLVPATCHREADY ",), timeout)
            fields = line.split()
            if len(fields) != 4 or fields[:2] != [
                    "HLVPATCHREADY", str(PATCH_PROTOCOL_VERSION)]:
                continue
            try:
                chunk_size, accepted_baud = map(int, fields[2:])
            except ValueError:
                continue
            if 0 < chunk_size <= 4096 and accepted_baud == data_baud:
                ready_values = chunk_size, accepted_baud
                break
        if ready_values is None:
            raise PatchError("no valid HLVPATCHREADY response received")
        chunk_size, accepted_baud = ready_values
        port.baudrate = accepted_baud

        sent = 0
        sequence = 0
        started = time.monotonic()
        next_report = 0.0
        with path.open("rb") as source:
            source.seek(source_offset)
            while sent < size:
                data = source.read(min(chunk_size, size - sent))
                if not data:
                    raise PatchError("source ended inside the selected range")
                packet = make_packet(sequence, data)
                expected_end = sent + len(data)
                for attempt in range(MAX_ATTEMPTS):
                    port.write(packet)
                    try:
                        response = wait_for(
                            port,
                            ("HLVPATCHACK ", "HLVPATCHNAK "),
                            timeout,
                        )
                    except PatchError:
                        if attempt + 1 == MAX_ATTEMPTS:
                            raise
                        continue
                    fields = response.split()
                    if response.startswith("HLVPATCHNAK "):
                        continue
                    if len(fields) != 3:
                        continue
                    try:
                        ack_sequence = int(fields[1])
                        ack_bytes = int(fields[2])
                    except ValueError:
                        continue
                    if ack_sequence == sequence and ack_bytes == expected_end:
                        break
                else:
                    raise PatchError(f"block {sequence} failed {MAX_ATTEMPTS} times")
                sent = expected_end
                sequence += 1
                now = time.monotonic()
                if now >= next_report or sent == size:
                    print_progress(sent, size, started)
                    next_report = now + 0.5

        expected_done = (
            f"HLVPATCHDONE {PATCH_PROTOCOL_VERSION} {remote_offset} {size} "
            f"{checksum:08x} {remote_name}"
        )
        while True:
            completion = wait_for(
                port,
                ("HLVPATCHDONE ",),
                patch_finalize_timeout(size, timeout),
            )
            if completion == expected_done:
                break
        if data_baud != CONTROL_BAUD:
            try:
                begin_session(port, "BAUD", timeout)
                change_baud(port, CONTROL_BAUD, timeout)
            except BaudError as error:
                raise PatchError(str(error)) from error
        try:
            enable_monitoring(port, timeout)
        except BaudError as error:
            raise PatchError(str(error)) from error
        print_progress(sent, size, started, force=True)
        print(
            f"Patched /HLV/{remote_name} at {remote_offset} for {size} byte(s); "
            f"CRC32 {checksum:08x}"
        )
    return checksum


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Replace one range in an existing ESP32 SD-card file"
    )
    parser.add_argument("file", type=pathlib.Path)
    parser.add_argument("--port", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--offset", required=True, type=int)
    parser.add_argument("--source-offset", type=int, default=0)
    parser.add_argument("--length", type=int)
    parser.add_argument("--data-baud", type=int, choices=SUPPORTED_DATA_BAUDS,
                        default=DEFAULT_DATA_BAUD)
    parser.add_argument("--timeout", type=float, default=15.0)
    return parser.parse_args(sys.argv[1:] if argv is None else argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    path = args.file.expanduser().resolve()
    if not path.is_file():
        print(f"error: file not found: {path}", file=sys.stderr)
        return 2
    length = args.length
    if length is None:
        length = path.stat().st_size - args.source_offset
    try:
        patch(path, args.port, args.name, args.offset,
              args.source_offset, length, args.data_baud, args.timeout)
    except (PatchError, OSError, ValueError, serial.SerialException) as error:
        print(f"\nerror: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
