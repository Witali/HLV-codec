#!/usr/bin/env python3
"""Upload an HLV video to the ESP32 player's SD card over UART0."""

from __future__ import annotations

import argparse
import pathlib
import re
import struct
import sys
import time
import zlib

import serial


CONTROL_BAUD = 460_800
DEFAULT_DATA_BAUD = 921_600
MAX_FILE_SIZE = 0xFFFFFFFF
BLOCK_MAGIC = b"HLVB"
BLOCK_HEADER = struct.Struct("<4sIHI")
VALID_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*\.hlv$", re.IGNORECASE)


class UploadError(RuntimeError):
    pass


def crc32_file(path: pathlib.Path) -> int:
    checksum = 0
    with path.open("rb") as source:
        while data := source.read(1024 * 1024):
            checksum = zlib.crc32(data, checksum)
    return checksum & 0xFFFFFFFF


def make_block(sequence: int, data: bytes) -> bytes:
    checksum = zlib.crc32(data) & 0xFFFFFFFF
    return BLOCK_HEADER.pack(BLOCK_MAGIC, sequence, len(data), checksum) + data


def read_protocol_line(port: serial.Serial, deadline: float) -> str:
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("ascii", errors="replace").strip()
        if line.startswith(("HLV",)):
            return line
    raise UploadError("timeout waiting for the ESP32 response")


def wait_for_response(port: serial.Serial, prefixes: tuple[str, ...],
                      timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while True:
        line = read_protocol_line(port, deadline)
        if line.startswith("HLVERR "):
            raise UploadError(f"ESP32 rejected the transfer: {line}")
        if line.startswith(prefixes):
            return line


def open_port(name: str, baud: int) -> serial.Serial:
    port = serial.Serial()
    port.port = name
    port.baudrate = baud
    port.bytesize = serial.EIGHTBITS
    port.parity = serial.PARITY_NONE
    port.stopbits = serial.STOPBITS_ONE
    port.timeout = 0.2
    port.write_timeout = 10
    # The UART uploader does not use the ROM bootloader. Keeping both modem
    # outputs inactive avoids an unintended reset on modified CYD boards.
    port.dtr = False
    port.rts = False
    port.open()
    port.reset_input_buffer()
    return port


def print_progress(sent: int, total: int, started: float, force: bool = False) -> None:
    elapsed = max(time.monotonic() - started, 0.001)
    rate = sent / elapsed
    remaining = (total - sent) / rate if rate else 0
    percent = sent * 100.0 / total
    text = (
        f"\r{percent:6.2f}%  {sent / 1048576:7.2f}/"
        f"{total / 1048576:.2f} MiB  {rate / 1024:6.1f} KiB/s  "
        f"ETA {remaining:6.1f}s"
    )
    print(text, end="\n" if force else "", flush=True)


def upload(path: pathlib.Path, port_name: str, remote_name: str,
           data_baud: int, response_timeout: float) -> None:
    if len(remote_name) > 48 or not VALID_NAME.fullmatch(remote_name):
        raise UploadError(
            "remote name must end in .hlv and contain only ASCII letters, "
            "digits, dot, underscore or dash (48 characters maximum)"
        )
    size = path.stat().st_size
    if not 0 < size <= MAX_FILE_SIZE:
        raise UploadError("FAT32 upload size must be between 1 byte and 4 GiB - 1")
    with path.open("rb") as source:
        if source.read(4) != b"HLV1":
            raise UploadError("source does not have an HLV1 file header")

    print(f"Calculating CRC32 for {path.name}...")
    file_crc = crc32_file(path)
    print(f"Opening {port_name} at {CONTROL_BAUD} baud")
    try:
        port = open_port(port_name, CONTROL_BAUD)
    except serial.SerialException as error:
        raise UploadError(
            f"cannot open {port_name}; close the serial monitor first: {error}"
        ) from error

    with port:
        # A leading newline discards a partial command left by a previous
        # interrupted terminal session.
        command = (
            f"\nHLVPUT 1 {remote_name} {size} {file_crc:08x} "
            f"{data_baud}\n"
        ).encode("ascii")
        port.write(command)
        port.flush()
        ready = wait_for_response(
            port, ("HLVREADY ",), response_timeout)
        fields = ready.split()
        if len(fields) != 4 or fields[:2] != ["HLVREADY", "1"]:
            raise UploadError(f"malformed ready response: {ready}")
        chunk_size = int(fields[2])
        accepted_baud = int(fields[3])
        if not 0 < chunk_size <= 65535 or accepted_baud != data_baud:
            raise UploadError(f"unsupported ready response: {ready}")

        time.sleep(0.05)
        port.baudrate = accepted_baud
        time.sleep(0.02)
        started = time.monotonic()
        sent = 0
        sequence = 0
        next_report = 0.0
        with path.open("rb") as source:
            while data := source.read(chunk_size):
                packet = make_block(sequence, data)
                for attempt in range(5):
                    port.write(packet)
                    port.flush()
                    response = wait_for_response(
                        port, ("HLVACK ", "HLVNAK "), response_timeout)
                    if response.startswith("HLVNAK "):
                        if attempt == 4:
                            raise UploadError(
                                f"block {sequence} failed CRC five times")
                        continue
                    fields = response.split()
                    expected_sent = sent + len(data)
                    if (len(fields) != 3 or int(fields[1]) != sequence or
                            int(fields[2]) != expected_sent):
                        raise UploadError(
                            f"unexpected block acknowledgement: {response}")
                    sent = expected_sent
                    sequence += 1
                    break
                now = time.monotonic()
                if now >= next_report or sent == size:
                    print_progress(sent, size, started)
                    next_report = now + 0.5

        done = wait_for_response(port, ("HLVDONE ",), response_timeout)
        fields = done.split()
        if (len(fields) != 5 or fields[:2] != ["HLVDONE", "1"] or
                int(fields[2]) != size or
                int(fields[3], 16) != file_crc or
                fields[4] != remote_name):
            raise UploadError(f"malformed completion response: {done}")
        print_progress(sent, size, started, force=True)
        print(f"Stored as /HLV/{remote_name}; CRC32 {file_crc:08x}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Upload an HLV file to the ESP32 player's SD card")
    parser.add_argument("file", type=pathlib.Path)
    parser.add_argument("--port", required=True, help="serial port, for example COM8")
    parser.add_argument("--name", default="video.hlv",
                        help="destination name in /HLV (default: video.hlv)")
    parser.add_argument("--data-baud", type=int, choices=(460800, 921600),
                        default=DEFAULT_DATA_BAUD)
    parser.add_argument("--timeout", type=float, default=15.0,
                        help="per-response timeout in seconds")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    path = args.file.expanduser().resolve()
    if not path.is_file():
        print(f"error: file not found: {path}", file=sys.stderr)
        return 2
    try:
        upload(path, args.port, args.name, args.data_baud, args.timeout)
    except (UploadError, serial.SerialException, OSError, ValueError) as error:
        print(f"\nerror: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
