#!/usr/bin/env python3
"""Synchronize only CRC-mismatching blocks of an ESP32 SD-card file."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import pathlib
import struct
import sys
import time
import zlib

import serial

from uart_baud import BaudError, begin_session, enable_monitoring
from uart_patch import PatchError, patch
from uart_upload import (
    CONTROL_BAUD,
    DEFAULT_DATA_BAUD,
    SUPPORTED_DATA_BAUDS,
    VALID_NAME,
    open_port,
)


BLOCK_CRC_PROTOCOL_VERSION = 1
BLOCK_CRC_MAGIC = b"HLVK"
BLOCK_CRC_PACKET = struct.Struct("<4sIIIII")
MAX_PACKET_ATTEMPTS = 20
MINIMUM_BLOCK_SIZE = 4096
MAXIMUM_BLOCK_SIZE = 1024 * 1024
DEFAULT_BLOCK_SIZE = 64 * 1024


class SyncError(RuntimeError):
    pass


@dataclass(frozen=True)
class BlockRecord:
    offset: int
    size: int
    crc32: int


@dataclass(frozen=True)
class BlockScan:
    size: int
    crc32: int
    blocks: tuple[BlockRecord, ...]


def calculate_blocks(path: pathlib.Path, block_size: int) -> BlockScan:
    blocks = []
    offset = 0
    file_crc = 0
    with path.open("rb") as source:
        while True:
            data = source.read(block_size)
            if not data:
                break
            checksum = zlib.crc32(data) & 0xFFFFFFFF
            blocks.append(BlockRecord(offset, len(data), checksum))
            file_crc = zlib.crc32(data, file_crc) & 0xFFFFFFFF
            offset += len(data)
    return BlockScan(offset, file_crc, tuple(blocks))


def _read_exact(port: serial.Serial, count: int, deadline: float) -> bytes:
    result = bytearray()
    while len(result) < count and time.monotonic() < deadline:
        data = port.read(count - len(result))
        if data:
            result.extend(data)
    if len(result) != count:
        raise SyncError(f"timeout after {len(result)} of {count} bytes")
    return bytes(result)


def _find_magic(port: serial.Serial, deadline: float) -> None:
    window = bytearray()
    while time.monotonic() < deadline:
        data = port.read(1)
        if not data:
            continue
        window.extend(data)
        if len(window) > len(BLOCK_CRC_MAGIC):
            del window[0]
        if bytes(window) == BLOCK_CRC_MAGIC:
            return
    raise SyncError("timeout waiting for an HLV block-CRC packet")


def _send_ack(port: serial.Serial, sequence: int, accepted: bool) -> None:
    prefix = struct.pack("<4sIB", b"HLVA", sequence, int(accepted))
    port.write(prefix + struct.pack(
        "<I", zlib.crc32(prefix) & 0xFFFFFFFF
    ))
    port.flush()


def scan_remote(port_name: str, remote_name: str, block_size: int,
                timeout: float) -> BlockScan:
    try:
        port = open_port(port_name, CONTROL_BAUD)
    except serial.SerialException as error:
        raise SyncError(
            f"cannot open {port_name}; close the serial monitor first: {error}"
        ) from error

    blocks = []
    expected_sequence = 0
    failures = 0
    with port:
        try:
            begin_session(port, "CRC32", timeout)
        except BaudError as error:
            raise SyncError(str(error)) from error
        port.write(
            f"\nHLVBLOCKCRC {BLOCK_CRC_PROTOCOL_VERSION} "
            f"{remote_name} {block_size}\n".encode("ascii")
        )
        port.flush()
        while True:
            deadline = time.monotonic() + timeout
            _find_magic(port, deadline)
            remainder = _read_exact(
                port, BLOCK_CRC_PACKET.size - len(BLOCK_CRC_MAGIC), deadline
            )
            packet = BLOCK_CRC_MAGIC + remainder
            magic, sequence, offset, size, checksum, packet_crc = \
                BLOCK_CRC_PACKET.unpack(packet)
            valid = (
                magic == BLOCK_CRC_MAGIC
                and (zlib.crc32(packet[:20]) & 0xFFFFFFFF) == packet_crc
                and sequence <= expected_sequence
            )
            if not valid:
                _send_ack(port, expected_sequence, False)
                failures += 1
                if failures >= MAX_PACKET_ATTEMPTS:
                    raise SyncError("too many corrupt block-CRC packets")
                continue
            if sequence < expected_sequence:
                _send_ack(port, sequence, True)
                continue
            if size == 0:
                _send_ack(port, sequence, True)
                try:
                    enable_monitoring(port, timeout)
                except BaudError as error:
                    raise SyncError(str(error)) from error
                return BlockScan(offset, checksum, tuple(blocks))
            expected_offset = expected_sequence * block_size
            if offset != expected_offset or size > block_size:
                _send_ack(port, sequence, False)
                failures += 1
                if failures >= MAX_PACKET_ATTEMPTS:
                    raise SyncError("invalid block geometry from ESP32")
                continue
            blocks.append(BlockRecord(offset, size, checksum))
            _send_ack(port, sequence, True)
            expected_sequence += 1
            failures = 0


def differing_ranges(local: BlockScan,
                     remote: BlockScan) -> list[tuple[int, int]]:
    if local.size != remote.size:
        raise SyncError(
            f"file sizes differ: computer={local.size}, ESP32={remote.size}; "
            "partial synchronization cannot resize a file"
        )
    if len(local.blocks) != len(remote.blocks):
        raise SyncError("computer and ESP32 returned different block counts")
    mismatches = []
    for expected, actual in zip(local.blocks, remote.blocks):
        if (expected.offset, expected.size) != (actual.offset, actual.size):
            raise SyncError("computer and ESP32 returned different block geometry")
        if expected.crc32 != actual.crc32:
            mismatches.append(expected)
    ranges = []
    for block in mismatches:
        if ranges and ranges[-1][0] + ranges[-1][1] == block.offset:
            start, length = ranges[-1]
            ranges[-1] = start, length + block.size
        else:
            ranges.append((block.offset, block.size))
    return ranges


def synchronize(path: pathlib.Path, port_name: str, remote_name: str,
                block_size: int, data_baud: int, timeout: float,
                dry_run: bool = False) -> BlockScan:
    local = calculate_blocks(path, block_size)
    print(
        f"Computer: {len(local.blocks)} block(s), {local.size} byte(s), "
        f"CRC32 {local.crc32:08x}"
    )
    remote = scan_remote(port_name, remote_name, block_size, timeout)
    print(
        f"ESP32:    {len(remote.blocks)} block(s), {remote.size} byte(s), "
        f"CRC32 {remote.crc32:08x}"
    )
    ranges = differing_ranges(local, remote)
    differing_bytes = sum(length for _, length in ranges)
    print(
        f"Different: {sum(1 for left, right in zip(local.blocks, remote.blocks) if left.crc32 != right.crc32)} "
        f"block(s), {len(ranges)} contiguous range(s), "
        f"{differing_bytes} byte(s)"
    )
    for offset, length in ranges:
        print(f"  offset {offset}, length {length}")
    if dry_run:
        return remote
    for index, (offset, length) in enumerate(ranges, 1):
        print(f"Patching range {index}/{len(ranges)}...")
        try:
            patch(path, port_name, remote_name, offset, offset, length,
                  data_baud, timeout)
        except PatchError as error:
            raise SyncError(str(error)) from error

    verified = scan_remote(port_name, remote_name, block_size, timeout)
    remaining = differing_ranges(local, verified)
    if remaining or verified.crc32 != local.crc32:
        raise SyncError(
            f"final verification failed: {len(remaining)} range(s) still "
            f"differ, computer CRC32={local.crc32:08x}, "
            f"ESP32 CRC32={verified.crc32:08x}"
        )
    print(
        f"Verified /HLV/{remote_name}: every block and full-file CRC32 "
        f"match ({local.crc32:08x})"
    )
    return verified


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Replace only CRC-mismatching blocks of an ESP32 file"
    )
    parser.add_argument("file", type=pathlib.Path)
    parser.add_argument("--port", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--block-size", type=int, default=DEFAULT_BLOCK_SIZE)
    parser.add_argument("--data-baud", type=int, choices=SUPPORTED_DATA_BAUDS,
                        default=DEFAULT_DATA_BAUD)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args(sys.argv[1:] if argv is None else argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    path = args.file.expanduser().resolve()
    if not path.is_file():
        raise SyncError(f"file not found: {path}")
    if len(args.name) > 48 or not VALID_NAME.fullmatch(args.name):
        raise SyncError("invalid destination filename")
    if args.block_size < MINIMUM_BLOCK_SIZE or \
            args.block_size > MAXIMUM_BLOCK_SIZE or \
            args.block_size & (args.block_size - 1):
        raise SyncError("--block-size must be a power of two from 4096 to 1048576")
    if args.timeout <= 0:
        raise SyncError("--timeout must be positive")
    synchronize(path, args.port, args.name, args.block_size,
                args.data_baud, args.timeout, args.dry_run)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SyncError as error:
        print(f"uart_sync: {error}", file=sys.stderr)
        raise SystemExit(1)
