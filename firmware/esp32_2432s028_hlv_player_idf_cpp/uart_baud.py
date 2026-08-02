#!/usr/bin/env python3
"""Change the ESP32 player's UART control rate with a verified handshake."""

from __future__ import annotations

import argparse
import serial
import struct
import sys
import time
import zlib


SUPPORTED_BAUDS = (460_800, 921_600, 1_000_000, 1_500_000, 2_000_000, 3_000_000)
HANDSHAKE_ATTEMPTS = 5
SESSION_COMMANDS = {
    "UPLOAD", "LIST", "READ", "PATCH", "CRC32", "BAUD", "DELETE", "SDBENCH"
}


class BaudError(RuntimeError):
    pass


def open_port(name: str, baud: int) -> serial.Serial:
    port = serial.Serial()
    port.port = name
    port.baudrate = baud
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


def _wait_line(
    port: serial.Serial, prefix: str, timeout: float
) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("ascii", errors="replace").strip()
        if line.startswith(prefix):
            return line
    raise BaudError(f"timeout waiting for {prefix.strip()}")


def _make_frame(magic: bytes, baud: int) -> bytes:
    prefix = struct.pack("<4sI", magic, baud)
    return prefix + struct.pack("<I", zlib.crc32(prefix) & 0xFFFFFFFF)


def _read_exact(port: serial.Serial, size: int, timeout: float) -> bytes:
    data = bytearray()
    deadline = time.monotonic() + timeout
    while len(data) < size and time.monotonic() < deadline:
        chunk = port.read(size - len(data))
        if chunk:
            data.extend(chunk)
    if len(data) != size:
        raise BaudError("timeout during binary baud handshake")
    return bytes(data)


def _wait_frame(
    port: serial.Serial, magic: bytes, baud: int, timeout: float
) -> None:
    window = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        window.extend(byte)
        if len(window) > 4:
            del window[0]
        if bytes(window) != magic:
            continue
        frame = magic + _read_exact(port, 8, timeout)
        frame_baud, frame_crc = struct.unpack("<II", frame[4:])
        if (
            frame_baud == baud
            and frame_crc == zlib.crc32(frame[:8]) & 0xFFFFFFFF
        ):
            return
    raise BaudError(f"timeout waiting for {magic.decode('ascii')}")


def change_baud(
    port: serial.Serial, baud: int, timeout: float
) -> None:
    if baud not in SUPPORTED_BAUDS:
        raise BaudError(f"unsupported baud rate {baud}")
    command = f"\nHLVBAUD 1 {baud}\n".encode("ascii")
    port.write(command)
    port.flush()
    for _ in range(HANDSHAKE_ATTEMPTS):
        ready = _wait_line(port, "HLVBAUDREADY 1 ", timeout)
        if ready == f"HLVBAUDREADY 1 {baud}":
            break
    else:
        raise BaudError("no valid HLVBAUDREADY response received")

    port.write(b"HLVBAUDSWITCH 1\n")
    port.flush()
    time.sleep(0.05)
    port.baudrate = baud
    time.sleep(0.02)
    go = _make_frame(b"HLVG", baud)
    acknowledged = False
    for _ in range(HANDSHAKE_ATTEMPTS):
        port.write(go)
        port.flush()
        try:
            _wait_frame(port, b"HLVA", baud, min(timeout, 2.0))
            acknowledged = True
            break
        except BaudError:
            continue
    if not acknowledged:
        raise BaudError("new baud rate was not acknowledged")
    done = _make_frame(b"HLVD", baud)
    for _ in range(HANDSHAKE_ATTEMPTS):
        port.write(done)
    port.flush()
    _wait_frame(port, b"HLVF", baud, timeout)


def begin_session(port: serial.Serial, command: str, timeout: float) -> None:
    if command not in SESSION_COMMANDS:
        raise BaudError(f"unsupported UART session {command}")
    port.reset_input_buffer()
    port.write(f"\nHLVSESSION 1 {command}\n".encode("ascii"))
    port.flush()
    expected = f"HLVSESSIONREADY 1 {command}"
    response = _wait_line(port, "HLVSESSIONREADY 1 ", timeout)
    if response != expected:
        raise BaudError(f"unexpected UART session response: {response}")


def enable_monitoring(port: serial.Serial, timeout: float) -> None:
    port.reset_input_buffer()
    port.write(b"\nHLVMONITOR 1 ON\n")
    port.flush()
    response = _wait_line(port, "HLVMONITORREADY 1 ", timeout)
    if response != "HLVMONITORREADY 1 ON":
        raise BaudError(f"unexpected UART monitoring response: {response}")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Change the ESP32 player's UART control baud rate"
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--from-baud", type=int, default=460_800,
                        choices=SUPPORTED_BAUDS)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--to-baud", type=int, choices=SUPPORTED_BAUDS)
    action.add_argument(
        "--monitor", action="store_true",
        help="enable diagnostics and resume video without changing baud"
    )
    parser.add_argument("--timeout", type=float, default=15.0)
    return parser.parse_args(sys.argv[1:] if argv is None else argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.timeout <= 0:
        raise BaudError("--timeout must be positive")
    try:
        port = open_port(args.port, args.from_baud)
    except serial.SerialException as error:
        raise BaudError(
            f"cannot open {args.port}; close the serial monitor first: {error}"
        ) from error
    with port:
        if args.monitor:
            enable_monitoring(port, args.timeout)
        else:
            begin_session(port, "BAUD", args.timeout)
            change_baud(port, args.to_baud, args.timeout)
            enable_monitoring(port, args.timeout)
    if args.monitor:
        print(f"{args.port}: UART monitoring enabled at {args.from_baud} baud")
        return 0
    print(
        f"{args.port}: UART changed from {args.from_baud} "
        f"to {args.to_baud} baud"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BaudError as error:
        print(f"uart_baud: {error}", file=sys.stderr)
        raise SystemExit(1)
