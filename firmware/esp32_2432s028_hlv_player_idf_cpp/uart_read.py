#!/usr/bin/env python3
"""Read a complete file or a byte range from the ESP32 player's SD card."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import serial
import struct
import sys
import time
import zlib

from uart_list import CONTROL_BAUD, open_port
from uart_baud import (
    BaudError, HANDSHAKE_ATTEMPTS, begin_session, change_baud,
    enable_monitoring,
)


PROTOCOL_VERSION = 2
READ_MAGIC = b"HLVX"
READ_HEADER_BYTES = 14
READ_BLOCK_BYTES = 64
READ_BLOCK_ATTEMPTS = 20
READ_READY_MAGIC = b"HLVR"
READ_READY_FRAME = struct.Struct("<4sIIIII")
READ_DONE_MAGIC = b"HLVE"
READ_DONE_FRAME = struct.Struct("<4sIII")
SUPPORTED_DATA_BAUDS = (460_800, 921_600, 1_000_000, 1_500_000, 2_000_000, 3_000_000)
UINT32_MAX = 0xFFFFFFFF


class ReadError(RuntimeError):
    pass


def _read_exact(port: serial.Serial, count: int, timeout: float) -> bytes:
    result = bytearray()
    deadline = time.monotonic() + timeout
    while len(result) < count:
        chunk = port.read(count - len(result))
        if chunk:
            result.extend(chunk)
            deadline = time.monotonic() + timeout
        elif time.monotonic() >= deadline:
            raise ReadError(f"timeout after {len(result)} of {count} bytes")
    return bytes(result)


def _find_magic(port: serial.Serial, magic: bytes, timeout: float) -> None:
    window = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        window.extend(byte)
        if len(window) > len(magic):
            del window[0]
        if bytes(window) == magic:
            return
    raise ReadError(f"timeout waiting for {magic.decode('ascii')} frame")


def _send_ack(port: serial.Serial, sequence: int, accepted: bool) -> None:
    message = struct.pack("<4sIB", b"HLVA", sequence, int(accepted))
    message += struct.pack("<I", zlib.crc32(message) & UINT32_MAX)
    port.write(message)
    port.flush()


def _wait_ready(
    port: serial.Serial, offset: int, timeout: float
) -> tuple[int, int, int]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        _find_magic(port, READ_READY_MAGIC, remaining)
        frame = READ_READY_MAGIC + _read_exact(
            port, READ_READY_FRAME.size - len(READ_READY_MAGIC), remaining
        )
        (_, file_size, response_offset, response_size,
         data_baud, checksum) = READ_READY_FRAME.unpack(frame)
        if (
            zlib.crc32(frame[:-4]) & UINT32_MAX != checksum
            or response_offset != offset
            or not 0 <= response_size <= file_size <= UINT32_MAX
            or response_offset + response_size > file_size
            or data_baud not in SUPPORTED_DATA_BAUDS
        ):
            continue
        return file_size, response_size, data_baud
    raise ReadError("timeout waiting for HLVREADREADY")


def _wait_done(
    port: serial.Serial, size: int, crc32: int, timeout: float
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        _find_magic(port, READ_DONE_MAGIC, remaining)
        frame = READ_DONE_MAGIC + _read_exact(
            port, READ_DONE_FRAME.size - len(READ_DONE_MAGIC), remaining
        )
        _, response_size, response_crc, checksum = READ_DONE_FRAME.unpack(frame)
        if (
            zlib.crc32(frame[:-4]) & UINT32_MAX != checksum
            or response_size != size
            or response_crc != crc32
        ):
            continue
        return
    raise ReadError("timeout waiting for HLVREADDONE")


def read_range(
    port: serial.Serial,
    name: str,
    offset: int,
    length: int,
    output,
    data_baud: int,
    control_baud: int,
    timeout: float,
) -> tuple[int, int, int]:
    request = (
        f"\nHLVREAD {PROTOCOL_VERSION} {name} {offset} {length} "
        f"{data_baud}\n"
    )
    port.write(request.encode("ascii"))
    port.flush()
    file_size, response_size, response_baud = _wait_ready(
        port, offset, timeout
    )
    if response_baud != control_baud:
        port.baudrate = response_baud
        time.sleep(0.02)
        port.write(b"HLVGO 2\n")
        port.flush()
    received = 0
    sequence = 0
    range_crc = 0
    failed_blocks = 0
    last_rejection = "none"
    try:
        while received < response_size:
            try:
                _find_magic(port, READ_MAGIC, timeout)
            except ReadError as error:
                raise ReadError(
                    f"{error}; accepted {received} byte(s), "
                    f"rejected {failed_blocks} corrupt block(s); "
                    f"last rejection: {last_rejection}"
                ) from error
            header = _read_exact(port, READ_HEADER_BYTES - 4, timeout)
            block_sequence, block_size, block_crc = struct.unpack(
                "<IHI", header
            )
            expected_size = min(
                READ_BLOCK_BYTES, response_size - received
            )
            payload = _read_exact(port, expected_size, timeout)
            calculated = zlib.crc32(payload) & UINT32_MAX
            if (
                block_sequence != sequence
                or block_size != expected_size
                or calculated != block_crc
            ):
                last_rejection = (
                    f"sequence {block_sequence}/{sequence}, "
                    f"size {block_size}/{expected_size}, "
                    f"CRC {block_crc:08x}/{calculated:08x}"
                )
                _send_ack(port, sequence, False)
                failed_blocks += 1
                if failed_blocks >= READ_BLOCK_ATTEMPTS:
                    raise ReadError(
                        f"block {block_sequence} failed CRC verification "
                        f"{READ_BLOCK_ATTEMPTS} times"
                    )
                continue
            _send_ack(port, sequence, True)
            failed_blocks = 0
            output.write(payload)
            range_crc = zlib.crc32(payload, range_crc) & UINT32_MAX
            received += expected_size
            sequence += 1
        port.baudrate = control_baud
        _wait_done(port, received, range_crc, timeout)
    finally:
        port.baudrate = control_baud
    return file_size, received, range_crc


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Read a file or byte range from /HLV over UART"
    )
    parser.add_argument("--port", required=True,
                        help="serial port, for example COM8")
    parser.add_argument("--offset", type=int, default=0)
    parser.add_argument("--length", type=int,
                        help="number of bytes; omitted means through EOF")
    parser.add_argument("--data-baud", type=int, default=1_000_000,
                        choices=SUPPORTED_DATA_BAUDS)
    parser.add_argument("--timeout", type=float, default=15.0,
                        help="maximum idle time in seconds")
    parser.add_argument("--force", action="store_true",
                        help="replace an existing output and .part file")
    parser.add_argument("name", help="file name in /HLV")
    parser.add_argument("output", type=Path, help="local output path")
    return parser.parse_args(sys.argv[1:] if argv is None else argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    length = UINT32_MAX if args.length is None else args.length
    if not 0 <= args.offset <= UINT32_MAX:
        raise ReadError("--offset must be between 0 and 4294967295")
    if not 1 <= length <= UINT32_MAX:
        raise ReadError("--length must be between 1 and 4294967295")
    if args.timeout <= 0:
        raise ReadError("--timeout must be positive")
    try:
        args.name.encode("ascii")
    except UnicodeEncodeError as error:
        raise ReadError("remote name must contain ASCII characters") from error

    output_path = args.output.resolve()
    temporary_path = output_path.with_name(output_path.name + ".part")
    if not args.force and (output_path.exists() or temporary_path.exists()):
        raise ReadError(
            f"{output_path} or {temporary_path} already exists; use --force"
        )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        port = open_port(args.port)
    except serial.SerialException as error:
        raise ReadError(
            f"cannot open {args.port}; close the serial monitor first: {error}"
        ) from error

    mode = "wb" if args.force else "xb"
    try:
        with port, temporary_path.open(mode) as output:
            if args.data_baud != CONTROL_BAUD:
                try:
                    begin_session(port, "BAUD", args.timeout)
                    change_baud(port, args.data_baud, args.timeout)
                except BaudError as error:
                    raise ReadError(str(error)) from error
            try:
                begin_session(port, "READ", args.timeout)
            except BaudError as error:
                raise ReadError(str(error)) from error
            file_size, received, checksum = read_range(
                port, args.name, args.offset, length, output,
                args.data_baud, args.data_baud, args.timeout
            )
            if args.data_baud != CONTROL_BAUD:
                try:
                    begin_session(port, "BAUD", args.timeout)
                    change_baud(port, CONTROL_BAUD, args.timeout)
                except BaudError as error:
                    raise ReadError(str(error)) from error
            try:
                enable_monitoring(port, args.timeout)
            except BaudError as error:
                raise ReadError(str(error)) from error
        os.replace(temporary_path, output_path)
    except Exception:
        if temporary_path.exists():
            temporary_path.unlink()
        raise

    print(
        f"read {received} byte(s) at offset {args.offset} from "
        f"{args.name} ({file_size} byte file), CRC32 {checksum:08x}"
    )
    print(output_path)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ReadError as error:
        print(f"uart_read: {error}", file=sys.stderr)
        raise SystemExit(1)
