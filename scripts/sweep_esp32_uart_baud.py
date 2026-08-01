#!/usr/bin/env python3
"""Build, flash, and drive the standalone ESP32 UART SWEEP firmware."""

from __future__ import annotations

import argparse
import csv
import os
import pathlib
import struct
import subprocess
import sys
import time
import zlib
from dataclasses import dataclass


REPOSITORY = pathlib.Path(__file__).resolve().parents[1]
SWEEP_PROJECT = REPOSITORY / "firmware" / "esp32_2432s028_uart_sweep"
CONTROL_BAUD = 1_000_000
PROTOCOL_VERSION = 1
FRAME_HEADER = struct.Struct("<4sHHI")
CONTROL_FRAME_REPEATS = 20
HOST_REPORT = struct.Struct("<4sIIIII")
RX_READY = struct.Struct("<4sIIIII")
RESULT = struct.Struct("<4sIIIIIII")
MAX_PAYLOAD_BYTES = 512


class SweepError(RuntimeError):
    pass


@dataclass
class SweepResult:
    nominal_baud: int
    calibrated_baud: int
    tx_valid: int
    tx_errors: int
    rx_valid: int
    rx_errors: int

    @property
    def total_errors(self) -> int:
        return self.tx_errors + self.rx_errors


@dataclass
class BestResult:
    nominal_baud: int
    calibrated_baud: int
    errors: int
    clean: bool


def make_payload(size: int, sequence: int, actual_baud: int) -> bytes:
    return bytes(
        (
            sequence * 131
            + index * 17
            + actual_baud
            + (index >> 3)
        )
        & 0xFF
        for index in range(size)
    )


def make_frame(sequence: int, payload_bytes: int, actual_baud: int) -> bytes:
    payload = make_payload(payload_bytes, sequence, actual_baud)
    return FRAME_HEADER.pack(
        b"SWPB",
        sequence,
        payload_bytes,
        zlib.crc32(payload) & 0xFFFFFFFF,
    ) + payload


def valid_frame(
    frame: bytes, sequence: int, payload_bytes: int, actual_baud: int
) -> bool:
    if len(frame) != FRAME_HEADER.size + payload_bytes:
        return False
    magic, received_sequence, received_size, checksum = (
        FRAME_HEADER.unpack_from(frame)
    )
    payload = frame[FRAME_HEADER.size:]
    return (
        magic == b"SWPB"
        and received_sequence == sequence
        and received_size == payload_bytes
        and checksum == zlib.crc32(payload) & 0xFFFFFFFF
        and payload == make_payload(payload_bytes, sequence, actual_baud)
    )


def make_host_report(
    nominal: int, actual: int, valid: int, errors: int
) -> bytes:
    prefix = struct.pack("<4sIIII", b"SWPH", nominal, actual, valid, errors)
    return prefix + struct.pack("<I", zlib.crc32(prefix) & 0xFFFFFFFF)


def parse_best(line: str) -> BestResult:
    fields = line.split()
    if len(fields) < 7 or fields[:3] != ["SWEEP", "BEST", "1"]:
        raise SweepError(f"invalid best line: {line}")
    nominal, calibrated, errors = map(int, fields[3:6])
    # A CH340C may join the following status line to the final digit when it
    # reopens after reset (for example, "0P COMPLETE 1").  BEST's last field
    # is a boolean, so consume only that leading digit.
    if not fields[6] or fields[6][0] not in "01":
        raise SweepError(f"invalid best line: {line}")
    clean = int(fields[6][0])
    return BestResult(nominal, calibrated, errors, bool(clean))


def rate_mask(bauds: list[int] | None) -> int:
    selected = bauds or [2_000_000, 3_000_000]
    mask = 0
    for baud in selected:
        mask |= {2_000_000: 1, 3_000_000: 2}[baud]
    return mask


def powershell(command: str, environment: dict[str, str]) -> None:
    process_environment = os.environ.copy()
    process_environment.update(environment)
    completed = subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            command,
        ],
        cwd=SWEEP_PROJECT,
        env=process_environment,
        check=False,
    )
    if completed.returncode:
        raise SweepError(
            f"PowerShell command failed with exit code {completed.returncode}"
        )


def build_firmware() -> None:
    powershell(
        "& $env:HLV_SWEEP_BUILD",
        {"HLV_SWEEP_BUILD": str(SWEEP_PROJECT / "build.ps1")},
    )


def flash_firmware(port: str, flash_baud: int) -> None:
    command = "& $env:HLV_SWEEP_FLASH -Port $env:HLV_SWEEP_PORT " \
              "-Baud $env:HLV_SWEEP_FLASH_BAUD -SkipBuild"
    powershell(
        command,
        {
            "HLV_SWEEP_FLASH": str(SWEEP_PROJECT / "flash.ps1"),
            "HLV_SWEEP_PORT": port,
            "HLV_SWEEP_FLASH_BAUD": str(flash_baud),
        },
    )


def load_serial():
    try:
        import serial  # pylint: disable=import-outside-toplevel
    except ModuleNotFoundError as error:
        raise SweepError(
            "pyserial is missing; run this script with the pinned ESP-IDF "
            "Python shown in firmware/esp32_2432s028_uart_sweep/README.md"
        ) from error
    return serial


def open_port(serial, name: str):
    port = serial.Serial()
    port.port = name
    port.baudrate = CONTROL_BAUD
    port.bytesize = serial.EIGHTBITS
    port.parity = serial.PARITY_NONE
    port.stopbits = serial.STOPBITS_TWO
    port.timeout = 0.1
    port.write_timeout = 10
    port.dtr = False
    port.rts = False
    port.open()
    try:
        port.set_buffer_size(rx_size=1024 * 1024, tx_size=256 * 1024)
    except (AttributeError, NotImplementedError, OSError):
        pass
    return port


def wait_line(port, prefix: str, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("ascii", errors="replace").strip()
        marker = line.find(prefix)
        if marker >= 0:
            return line[marker:]
    raise SweepError(f"timeout waiting for {prefix.strip()}")


def read_exact(port, size: int, timeout: float) -> bytes:
    data = bytearray()
    deadline = time.monotonic() + timeout
    while len(data) < size and time.monotonic() < deadline:
        chunk = port.read(size - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def find_magic(port, magic: bytes, timeout: float) -> None:
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
    raise SweepError(f"timeout waiting for {magic.decode('ascii')}")


def wait_rx_ready(
    port, nominal: int, actual: int, blocks: int, payload: int, timeout: float
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        find_magic(port, b"SWPG", remaining)
        suffix = read_exact(port, RX_READY.size - 4, remaining)
        if len(suffix) != RX_READY.size - 4:
            continue
        frame = b"SWPG" + suffix
        fields = RX_READY.unpack(frame)
        if (
            fields[1:5] == (nominal, actual, blocks, payload)
            and fields[-1] == zlib.crc32(frame[:-4]) & 0xFFFFFFFF
        ):
            return
    raise SweepError("timeout waiting for valid SWPG")


def wait_result_frame(port, timeout: float) -> SweepResult:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        find_magic(port, b"SWPR", remaining)
        suffix = read_exact(port, RESULT.size - 4, remaining)
        if len(suffix) != RESULT.size - 4:
            continue
        frame = b"SWPR" + suffix
        fields = RESULT.unpack(frame)
        if fields[-1] != zlib.crc32(frame[:-4]) & 0xFFFFFFFF:
            continue
        return SweepResult(*fields[1:-1])
    raise SweepError("timeout waiting for valid SWPR")


def receive_frames(
    port, blocks: int, payload_bytes: int, actual_baud: int, timeout: float
) -> tuple[int, int]:
    frame_bytes = FRAME_HEADER.size + payload_bytes
    valid = 0
    errors = 0
    for sequence in range(blocks):
        frame = read_exact(port, frame_bytes, timeout)
        if valid_frame(frame, sequence, payload_bytes, actual_baud):
            valid += 1
        else:
            errors += 1
            if len(frame) != frame_bytes:
                errors += blocks - sequence - 1
                break
    return valid, errors


def send_frames(
    port, blocks: int, payload_bytes: int, actual_baud: int
) -> None:
    for sequence in range(blocks):
        port.write(make_frame(sequence, payload_bytes, actual_baud))
    port.flush()


def switch_host_rate(port, baud: int) -> None:
    if baud == CONTROL_BAUD:
        port.close()
        port.baudrate = baud
        port.dtr = False
        port.rts = False
        port.open()
        return
    port.baudrate = baud
    time.sleep(0.02)


def parse_candidate(line: str) -> tuple[int, int, int, int, int]:
    fields = line.split()
    if (
        len(fields) != 8
        or fields[:3] != ["SWEEP", "CANDIDATE", "1"]
    ):
        raise SweepError(f"invalid candidate line: {line}")
    nominal = int(fields[3])
    actual = int(fields[4])
    blocks = int(fields[5])
    payload_bytes = int(fields[6])
    switch_delay_ms = int(fields[7])
    return nominal, actual, blocks, payload_bytes, switch_delay_ms


def reopen_control(port) -> None:
    time.sleep(1.2)
    port.close()
    port.baudrate = CONTROL_BAUD
    port.dtr = False
    port.rts = False
    time.sleep(0.05)
    port.open()


def run_protocol(
    port, blocks: int, payload_bytes: int, selected_mask: int,
    timeout: float, csv_path: pathlib.Path | None = None
) -> tuple[list[SweepResult], list[BestResult]]:
    results: list[SweepResult] = []
    result_keys: set[tuple[int, int]] = set()
    best: list[BestResult] = []
    while True:
        ready = wait_line(port, "SWEEP READY 1 ", 30.0)
        status = ready.split()[-1]
        if status == "IDLE":
            port.write(
                f"SWEEP START 1 {blocks} {payload_bytes} "
                f"{selected_mask}\n".encode("ascii")
            )
            port.flush()
            wait_line(port, "SWEEP STARTED 1", timeout)
        elif status == "ACTIVE":
            port.write(b"SWEEP CONTINUE 1\n")
            port.flush()
            wait_line(port, "SWEEP CONTINUING 1", timeout)
        elif status == "COMPLETE":
            while True:
                line = wait_line(port, "SWEEP ", timeout)
                if line.startswith("SWEEP BEST 1 "):
                    selected = parse_best(line)
                    if all(
                        item.nominal_baud != selected.nominal_baud
                        for item in best
                    ):
                        best.append(selected)
                elif line.startswith("SWEEP COMPLETE 1"):
                    port.write(b"SWEEP ACK 1\n")
                    port.flush()
                    return results, best

        candidate_line = wait_line(port, "SWEEP CANDIDATE 1 ", timeout)
        nominal, actual, candidate_blocks, candidate_payload, _switch_delay_ms = (
            parse_candidate(candidate_line)
        )
        if candidate_blocks != blocks or candidate_payload != payload_bytes:
            raise SweepError("firmware changed the requested probe size")
        switch_host_rate(port, nominal)
        valid, errors = receive_frames(
            port, blocks, payload_bytes, actual, timeout
        )
        print(
            f"phase TX {nominal}: ESP32={actual} "
            f"valid={valid}/{blocks} errors={errors}",
            flush=True,
        )
        report = make_host_report(nominal, actual, valid, errors)
        port.write(report * CONTROL_FRAME_REPEATS)
        port.flush()
        wait_rx_ready(
            port, nominal, actual, blocks, payload_bytes, timeout + 5.0
        )
        send_frames(port, blocks, payload_bytes, actual)
        result = wait_result_frame(port, timeout + 5.0)
        key = (result.nominal_baud, result.calibrated_baud)
        if key not in result_keys:
            result_keys.add(key)
            results.append(result)
            if csv_path is not None:
                write_results(csv_path, results)
            print(
                f"{result.nominal_baud}: ESP32={result.calibrated_baud} "
                f"TX={result.tx_valid}/{blocks} "
                f"RX={result.rx_valid}/{blocks} "
                f"errors={result.total_errors}",
                flush=True,
            )
        reopen_control(port)


def write_results(path: pathlib.Path, rows: list[SweepResult]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=SweepResult.__annotations__)
        writer.writeheader()
        for row in rows:
            writer.writerow(row.__dict__)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument(
        "--baud",
        type=int,
        choices=(2_000_000, 3_000_000),
        action="append",
        help="rate to sweep; repeat for both (default: both)",
    )
    parser.add_argument("--blocks", type=int, default=128)
    parser.add_argument("--payload", type=int, default=256)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--flash-baud", type=int, default=460_800)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-flash", action="store_true")
    parser.add_argument(
        "--csv",
        type=pathlib.Path,
        default=REPOSITORY / ".tmp" / "uart-baud-sweep.csv",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not 1 <= args.blocks <= 1000:
        raise SweepError("--blocks must be between 1 and 1000")
    if not 1 <= args.payload <= MAX_PAYLOAD_BYTES:
        raise SweepError(f"--payload must be between 1 and {MAX_PAYLOAD_BYTES}")
    if args.timeout <= 0:
        raise SweepError("--timeout must be positive")
    if not args.skip_build:
        build_firmware()
    if not args.skip_flash:
        flash_firmware(args.port, args.flash_baud)

    serial = load_serial()
    port = open_port(serial, args.port)
    with port:
        rows, best = run_protocol(
            port,
            args.blocks,
            args.payload,
            rate_mask(args.baud),
            args.timeout,
            args.csv.resolve(),
        )
    write_results(args.csv.resolve(), rows)
    for selected in best:
        reliability = "clean" if selected.clean else "no clean candidate"
        print(
            f"selected {selected.nominal_baud}={selected.calibrated_baud} "
            f"({reliability}, errors={selected.errors})"
        )
    print(args.csv.resolve())
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SweepError as error:
        print(f"uart-sweep: {error}", file=sys.stderr)
        raise SystemExit(1)
