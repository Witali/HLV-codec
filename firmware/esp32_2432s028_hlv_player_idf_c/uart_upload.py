#!/usr/bin/env python3
"""Upload a player video or selection file to microSD over UART0."""

from __future__ import annotations

import argparse
import pathlib
import re
import struct
import sys
import time
import zlib

import serial

from uart_baud import (
    BaudError, HANDSHAKE_ATTEMPTS, begin_session, change_baud,
    enable_monitoring,
)


CONTROL_BAUD = 1_000_000
DEFAULT_DATA_BAUD = 1_000_000
UPLOAD_PROTOCOL_VERSION = 2
WINDOW_ACK_TIMEOUT_SECONDS = 2.0
WINDOW_PROGRESS_TIMEOUT_SECONDS = 10.0
MINIMUM_VERIFY_BYTES_PER_SECOND = 256 * 1024
MAX_BLOCK_ATTEMPTS = 5
SUPPORTED_DATA_BAUDS = (
    460_800,
    921_600,
    1_000_000,
    1_500_000,
    2_000_000,
    3_000_000,
)
MAX_FILE_SIZE = 0xFFFFFFFF
BLOCK_MAGIC = b"HLVB"
BLOCK_HEADER = struct.Struct("<4sIHI")
VALID_NAME = re.compile(
    r"^[A-Za-z0-9][A-Za-z0-9._-]*\.(?:hlv|avi|bpv1|mpg|mpeg|3gp|txt)$",
    re.IGNORECASE,
)


class UploadError(RuntimeError):
    pass


class ResponseTimeout(UploadError):
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
    raise ResponseTimeout("timeout waiting for the ESP32 response")


def wait_for_response(port: serial.Serial, prefixes: tuple[str, ...],
                      timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while True:
        line = read_protocol_line(port, deadline)
        if line.startswith("HLVERR "):
            raise UploadError(f"ESP32 rejected the transfer: {line}")
        if line.startswith(prefixes):
            return line


def wait_for_completion(port: serial.Serial, size: int, checksum: int,
                        remote_name: str, timeout: float) -> None:
    expected = (
        f"HLVDONE {UPLOAD_PROTOCOL_VERSION} {size} "
        f"{checksum:08x} {remote_name}"
    ).encode("ascii")
    deadline = time.monotonic() + timeout
    received = bytearray()
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        received.extend(raw)
        if expected in received:
            return
        if b"HLVERR " in received:
            line = bytes(received).decode("ascii", errors="replace").strip()
            raise UploadError(f"ESP32 rejected the transfer: {line}")
        if len(received) > 1024:
            del received[:-512]
    raise ResponseTimeout("timeout waiting for the ESP32 completion response")


def completion_timeout(size: int, response_timeout: float) -> float:
    """Allow time for ESP32 to reread and CRC the staged SD-card file."""
    return max(
        response_timeout,
        30.0 + size / MINIMUM_VERIFY_BYTES_PER_SECOND,
    )


def open_port(name: str, baud: int) -> serial.Serial:
    port = serial.Serial()
    port.port = name
    port.baudrate = baud
    port.bytesize = serial.EIGHTBITS
    port.parity = serial.PARITY_NONE
    port.stopbits = serial.STOPBITS_TWO
    port.timeout = 0.2
    port.write_timeout = 10
    # The UART uploader does not use the ROM bootloader. Keeping both modem
    # outputs inactive avoids an unintended reset on modified CYD boards.
    port.dtr = False
    port.rts = False
    port.open()
    try:
        port.set_buffer_size(rx_size=1024 * 1024, tx_size=64 * 1024)
    except (AttributeError, NotImplementedError, OSError):
        pass
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
            "remote name must end in .hlv, .avi, .bpv1, .mpg, .mpeg, .3gp or .txt and contain only ASCII letters, "
            "digits, dot, underscore or dash (48 characters maximum)"
        )
    size = path.stat().st_size
    if not 0 < size <= MAX_FILE_SIZE:
        raise UploadError("FAT32 upload size must be between 1 byte and 4 GiB - 1")
    suffix = pathlib.Path(remote_name).suffix.lower()
    with path.open("rb") as source:
        header = source.read(12)
    if suffix == ".hlv" and header[:4] != b"HLV1":
        raise UploadError("source does not have an HLV1 file header")
    if suffix == ".bpv1" and header[:4] != b"BPV1":
        raise UploadError("source does not have a BPV1 file header")
    if suffix == ".avi" and (
        header[:4] != b"RIFF" or header[8:12] != b"AVI "
    ):
        raise UploadError("source does not have a RIFF AVI file header")
    if suffix in (".mpg", ".mpeg") and header[:4] != b"\x00\x00\x01\xba":
        raise UploadError("source does not have an MPEG Program Stream header")
    if suffix == ".3gp" and header[4:8] != b"ftyp":
        raise UploadError("source does not have an ISO BMFF/3GP file header")
    if suffix == ".txt":
        try:
            selected = path.read_text(encoding="ascii").strip()
        except UnicodeError as error:
            raise UploadError("selection file must be ASCII") from error
        if not re.fullmatch(
                r"[A-Za-z0-9][A-Za-z0-9._ -]*\.(?:hlv|avi|bpv1|mpg|mpeg|3gp)",
                            selected, re.IGNORECASE):
            raise UploadError(
                "selection file must contain one safe .hlv, .avi, .bpv1, .mpg, .mpeg or .3gp filename"
            )

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
        if data_baud != CONTROL_BAUD:
            try:
                begin_session(port, "BAUD", response_timeout)
                change_baud(port, data_baud, response_timeout)
            except BaudError as error:
                raise UploadError(str(error)) from error
        try:
            begin_session(port, "UPLOAD", response_timeout)
        except BaudError as error:
            raise UploadError(str(error)) from error
        # A leading newline discards a partial command left by a previous
        # interrupted terminal session.
        command = (
            f"\nHLVPUT {UPLOAD_PROTOCOL_VERSION} {remote_name} "
            f"{size} {file_crc:08x} "
            f"{data_baud}\n"
        ).encode("ascii")
        port.write(command)
        port.flush()
        ready_values: tuple[int, int, int] | None = None
        for _ in range(HANDSHAKE_ATTEMPTS):
            ready = wait_for_response(
                port, ("HLVREADY ",), response_timeout)
            fields = ready.split()
            if (
                len(fields) != 5
                or fields[:2]
                != ["HLVREADY", str(UPLOAD_PROTOCOL_VERSION)]
            ):
                continue
            try:
                values = tuple(int(value) for value in fields[2:])
            except ValueError:
                continue
            chunk_size, accepted_baud, window_size = values
            if (
                0 < chunk_size <= 65535
                and accepted_baud == data_baud
                and 0 < window_size <= 16
            ):
                ready_values = values
                break
        if ready_values is None:
            raise UploadError("no valid HLVREADY response received")
        chunk_size, accepted_baud, window_size = ready_values

        port.baudrate = accepted_baud
        started = time.monotonic()
        acknowledged = 0
        next_sequence = 0
        next_offset = 0
        next_report = 0.0
        last_progress = started
        in_flight: dict[int, tuple[bytes, int, int]] = {}

        def retransmit_from(sequence: int) -> None:
            retransmitted = False
            for pending_sequence in sorted(in_flight):
                if pending_sequence < sequence:
                    continue
                packet, end_offset, attempts = in_flight[pending_sequence]
                if attempts >= MAX_BLOCK_ATTEMPTS:
                    raise UploadError(
                        f"block {pending_sequence} failed "
                        f"{MAX_BLOCK_ATTEMPTS} times"
                    )
                in_flight[pending_sequence] = (
                    packet, end_offset, attempts + 1
                )
                port.write(packet)
                retransmitted = True
            if not retransmitted:
                raise UploadError(
                    f"ESP32 requested unknown block {sequence}"
                )

        with path.open("rb") as source:
            source_finished = False
            while in_flight or not source_finished:
                while not source_finished and len(in_flight) < window_size:
                    data = source.read(chunk_size)
                    if not data:
                        source_finished = True
                        break
                    packet = make_block(next_sequence, data)
                    next_offset += len(data)
                    in_flight[next_sequence] = (
                        packet, next_offset, 1
                    )
                    port.write(packet)
                    next_sequence += 1

                if not in_flight:
                    break

                try:
                    response = wait_for_response(
                        port,
                        ("HLVACK ", "HLVNAK ", "HLVWAIT "),
                        min(
                            response_timeout,
                            WINDOW_ACK_TIMEOUT_SECONDS,
                        ),
                    )
                except ResponseTimeout:
                    if (
                        time.monotonic() - last_progress
                        >= WINDOW_PROGRESS_TIMEOUT_SECONDS
                    ):
                        raise UploadError(
                            "sliding window made no progress for "
                            f"{WINDOW_PROGRESS_TIMEOUT_SECONDS:.0f} seconds"
                        )
                    retransmit_from(min(in_flight))
                    continue

                if response.startswith("HLVWAIT "):
                    if (
                        time.monotonic() - last_progress
                        >= WINDOW_PROGRESS_TIMEOUT_SECONDS
                    ):
                        raise UploadError(
                            "ESP32 is alive but the SD pipeline made no "
                            f"progress for "
                            f"{WINDOW_PROGRESS_TIMEOUT_SECONDS:.0f} seconds"
                        )
                    continue

                if response.startswith("HLVNAK "):
                    fields = response.split()
                    if len(fields) < 2:
                        raise UploadError(
                            f"malformed block rejection: {response}"
                        )
                    retransmit_from(int(fields[1]))
                    continue

                fields = response.split()
                if len(fields) != 3:
                    raise UploadError(
                        f"malformed block acknowledgement: {response}"
                    )
                acknowledged_sequence = int(fields[1])
                acknowledged_bytes = int(fields[2])
                if acknowledged_sequence not in in_flight:
                    if acknowledged_bytes <= acknowledged:
                        continue
                    raise UploadError(
                        f"unexpected block acknowledgement: {response}"
                    )
                expected_bytes = in_flight[acknowledged_sequence][1]
                if acknowledged_bytes != expected_bytes:
                    raise UploadError(
                        f"unexpected block acknowledgement: {response}"
                    )
                for pending_sequence in list(in_flight):
                    if pending_sequence <= acknowledged_sequence:
                        del in_flight[pending_sequence]
                acknowledged = acknowledged_bytes
                last_progress = time.monotonic()

                now = time.monotonic()
                if now >= next_report or acknowledged == size:
                    print_progress(acknowledged, size, started)
                    next_report = now + 0.5

        wait_for_completion(
            port, size, file_crc, remote_name,
            completion_timeout(size, response_timeout),
        )
        if data_baud != CONTROL_BAUD:
            try:
                begin_session(port, "BAUD", response_timeout)
                change_baud(port, CONTROL_BAUD, response_timeout)
            except BaudError as error:
                raise UploadError(str(error)) from error
        try:
            enable_monitoring(port, response_timeout)
        except BaudError as error:
            raise UploadError(str(error)) from error
        print_progress(acknowledged, size, started, force=True)
        print(f"Stored as /HLV/{remote_name}; CRC32 {file_crc:08x}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Upload a video or play.txt to the ESP32 player's SD card")
    parser.add_argument("file", type=pathlib.Path)
    parser.add_argument("--port", required=True, help="serial port, for example COM8")
    parser.add_argument("--name",
                        help="destination name in /HLV (default: source name)")
    parser.add_argument("--data-baud", type=int, choices=SUPPORTED_DATA_BAUDS,
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
        upload(path, args.port, args.name or path.name,
               args.data_baud, args.timeout)
    except (UploadError, serial.SerialException, OSError, ValueError) as error:
        print(f"\nerror: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
