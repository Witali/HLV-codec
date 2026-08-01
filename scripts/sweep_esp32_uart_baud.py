#!/usr/bin/env python3
"""Build, flash, and CRC-test ESP32 UART calibration candidates.

The calibration is compiled into each firmware image.  It is therefore fixed
from boot and shared by the ESP32 receive and transmit paths.  The probe reads
an existing SD-card file; it creates no files on the card.
"""

from __future__ import annotations

import argparse
import csv
import io
import multiprocessing
import os
import pathlib
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass


REPOSITORY = pathlib.Path(__file__).resolve().parents[1]
PROJECTS = {
    "c": REPOSITORY / "firmware" / "esp32_2432s028_hlv_player_idf_c",
    "cpp": REPOSITORY / "firmware" / "esp32_2432s028_hlv_player_idf_cpp",
}
CONTROL_BAUD = 1_000_000
APB_CLOCK_HZ = 80_000_000
DEFAULT_FILE = "Danila_320x180_30fps_HLVv14_38dB.hlv"
DEFAULT_DIVISORS = {
    # The crystal-less CH340C on the measured board needed the ESP32 side
    # moved upward at 2 Mbaud.  Enumerate the distinct 1/16 APB-divider steps
    # around that window; closer integer baud values would map to duplicates.
    2_000_000: (
        40.0,
        39.9375,
        39.875,
        39.8125,
        39.75,
        39.6875,
        39.625,
        39.5625,
        39.5,
    ),
    3_000_000: (
        80 / 3,
        26.875,
        27.0,
        27.125,
        27.25,
        27.3125,
        27.375,
        27.4375,
        27.5,
        27.5625,
        27.625,
    ),
}
DEFINE_NAMES = {
    2_000_000: "UART_CALIBRATED_BAUD_2000K",
    3_000_000: "UART_CALIBRATED_BAUD_3000K",
}


@dataclass
class ProbeResult:
    nominal_baud: int
    calibrated_baud: int
    repetition: int
    status: str
    crc_rejections: int
    accepted_blocks: int
    received_bytes: int
    crc32: str
    elapsed_seconds: float
    detail: str


def default_candidates(nominal_baud: int) -> tuple[int, ...]:
    rates = {
        round(APB_CLOCK_HZ / divisor)
        for divisor in DEFAULT_DIVISORS[nominal_baud]
    }
    return tuple(sorted(rates, reverse=True))


def parse_candidate(value: str) -> tuple[int, int]:
    try:
        nominal_text, calibrated_text = value.split("=", 1)
        nominal = int(nominal_text)
        calibrated = int(calibrated_text)
    except (ValueError, TypeError) as error:
        raise argparse.ArgumentTypeError(
            "candidate must look like 2000000=1957187"
        ) from error
    if nominal not in DEFINE_NAMES or calibrated <= 0:
        raise argparse.ArgumentTypeError("unsupported UART candidate")
    return nominal, calibrated


def powershell(
    command: str, environment: dict[str, str], cwd: pathlib.Path
) -> None:
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
        cwd=cwd,
        env=process_environment,
        check=False,
    )
    if completed.returncode:
        raise RuntimeError(
            f"PowerShell command failed with exit code {completed.returncode}"
        )


def build_variant(
    project: pathlib.Path,
    build_directory: pathlib.Path,
    calibrated: dict[int, int],
) -> None:
    command = r"""
$ErrorActionPreference = 'Stop'
& $env:HLV_SWEEP_IDF -IdfArguments @(
    '-B', $env:HLV_SWEEP_BUILD,
    '-D', "UART_CALIBRATED_BAUD_2000K=$env:HLV_SWEEP_BAUD_2M",
    '-D', "UART_CALIBRATED_BAUD_3000K=$env:HLV_SWEEP_BAUD_3M",
    'build'
)
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
"""
    powershell(
        command,
        {
            "HLV_SWEEP_IDF": str(project / "idf.ps1"),
            "HLV_SWEEP_BUILD": str(build_directory),
            "HLV_SWEEP_BAUD_2M": str(calibrated[2_000_000]),
            "HLV_SWEEP_BAUD_3M": str(calibrated[3_000_000]),
        },
        project,
    )


def flash_variant(
    project: pathlib.Path,
    build_directory: pathlib.Path,
    port: str,
    flash_baud: int,
) -> None:
    command = r"""
$ErrorActionPreference = 'Stop'
$env:ESPTOOL_OPEN_PORT_ATTEMPTS = '60'
& $env:HLV_SWEEP_IDF `
    -EsptoolWorkingDirectory $env:HLV_SWEEP_BUILD `
    -EsptoolArguments @(
    '--chip', 'esp32',
    '--port', $env:HLV_SWEEP_PORT,
    '--baud', $env:HLV_SWEEP_FLASH_BAUD,
    '--before', 'default_reset',
    '--after', 'hard_reset',
    'write_flash', '@flash_args'
)
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
"""
    powershell(
        command,
        {
            "HLV_SWEEP_IDF": str(project / "idf.ps1"),
            "HLV_SWEEP_BUILD": str(build_directory),
            "HLV_SWEEP_PORT": port,
            "HLV_SWEEP_FLASH_BAUD": str(flash_baud),
        },
        project,
    )


def load_uart_modules(project: pathlib.Path):
    sys.path.insert(0, str(project))
    import uart_read  # pylint: disable=import-outside-toplevel
    from uart_baud import (  # pylint: disable=import-outside-toplevel
        begin_session,
        change_baud,
        enable_monitoring,
        open_port,
    )

    return uart_read, begin_session, change_baud, enable_monitoring, open_port


def probe_once(
    modules,
    port_name: str,
    nominal_baud: int,
    calibrated_baud: int,
    filename: str,
    length: int,
    timeout: float,
    max_rejections: int,
    repetition: int,
) -> ProbeResult:
    (uart_read, begin_session, _, _, open_port) = modules
    counters = {"accepted": 0, "rejected": 0}
    received = 0
    checksum = 0
    started = time.monotonic()
    status = "PASS"
    detail = ""
    stage = "open control port"
    original_send_ack = uart_read._send_ack

    def counting_ack(target, sequence, accepted):
        counters["accepted" if accepted else "rejected"] += 1
        original_send_ack(target, sequence, accepted)
        if not accepted and counters["rejected"] >= max_rejections:
            raise RuntimeError(
                f"stopped after {max_rejections} CRC rejections"
            )

    try:
        port = open_port(port_name, CONTROL_BAUD)
        with port:
            stage = "begin READ session at control rate"
            begin_session(port, "READ", timeout)
            uart_read._send_ack = counting_ack
            try:
                output = io.BytesIO()
                stage = "CRC-protected range read"
                _, received, checksum = uart_read.read_range(
                    port,
                    filename,
                    0,
                    length,
                    output,
                    nominal_baud,
                    CONTROL_BAUD,
                    timeout,
                )
            finally:
                uart_read._send_ack = original_send_ack
    except Exception as error:  # Keep the sweep going after a bad candidate.
        uart_read._send_ack = original_send_ack
        status = "FAIL"
        detail = f"{stage}: {str(error).replace(chr(10), ' ')}"

    return ProbeResult(
        nominal_baud=nominal_baud,
        calibrated_baud=calibrated_baud,
        repetition=repetition,
        status=status,
        crc_rejections=counters["rejected"],
        accepted_blocks=counters["accepted"],
        received_bytes=received,
        crc32=f"{checksum:08x}" if received else "",
        elapsed_seconds=time.monotonic() - started,
        detail=detail,
    )


def probe_worker(queue, project: pathlib.Path, arguments: tuple) -> None:
    try:
        result = probe_once(load_uart_modules(project), *arguments)
    except Exception as error:
        result = ProbeResult(
            nominal_baud=arguments[1],
            calibrated_baud=arguments[2],
            repetition=arguments[-1],
            status="FAIL",
            crc_rejections=0,
            accepted_blocks=0,
            received_bytes=0,
            crc32="",
            elapsed_seconds=0.0,
            detail=str(error).replace("\n", " "),
        )
    queue.put(result)
    queue.close()
    queue.join_thread()


def bounded_probe(
    project: pathlib.Path,
    arguments: tuple,
    wall_timeout: float,
) -> ProbeResult:
    context = multiprocessing.get_context("spawn")
    queue = context.Queue()
    process = context.Process(
        target=probe_worker,
        args=(queue, project, arguments),
    )
    started = time.monotonic()
    process.start()
    process.join(wall_timeout)
    if process.is_alive():
        process.terminate()
        process.join(5.0)
        queue.close()
        queue.join_thread()
        return ProbeResult(
            nominal_baud=arguments[1],
            calibrated_baud=arguments[2],
            repetition=arguments[-1],
            status="FAIL",
            crc_rejections=0,
            accepted_blocks=0,
            received_bytes=0,
            crc32="",
            elapsed_seconds=time.monotonic() - started,
            detail=f"probe exceeded {wall_timeout:.1f} second wall timeout",
        )
    if queue.empty():
        queue.close()
        queue.join_thread()
        raise RuntimeError(f"probe process exited with code {process.exitcode}")
    result = queue.get()
    queue.close()
    queue.join_thread()
    return result


def wait_for_application(modules, port_name: str, timeout: float) -> None:
    (_, begin_session, _, _, open_port) = modules
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            port = open_port(port_name, CONTROL_BAUD)
            with port:
                begin_session(port, "LIST", 1.0)
            return
        except Exception as error:  # Boot messages may consume the first try.
            last_error = error
            time.sleep(0.25)
    raise RuntimeError(
        f"application did not become ready on {port_name}: {last_error}"
    )


def resume_application(modules, port_name: str, timeout: float) -> None:
    (_, _, _, enable_monitoring, open_port) = modules
    port = open_port(port_name, CONTROL_BAUD)
    with port:
        enable_monitoring(port, timeout)


def result_score(rows: list[ProbeResult]) -> tuple[int, int, float]:
    failures = sum(row.status != "PASS" for row in rows)
    rejections = sum(row.crc_rejections for row in rows)
    elapsed = statistics.median(row.elapsed_seconds for row in rows)
    return failures, rejections, elapsed


def candidate_is_clean(rows: list[ProbeResult], expected_bytes: int) -> bool:
    """Return true only for complete transfers without a single CRC retry."""
    return bool(rows) and all(
        row.status == "PASS"
        and row.crc_rejections == 0
        and row.received_bytes == expected_bytes
        for row in rows
    )


def write_results(path: pathlib.Path, rows: list[ProbeResult]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=ProbeResult.__annotations__)
        writer.writeheader()
        for row in rows:
            writer.writerow(row.__dict__)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--project", choices=PROJECTS, default="c")
    parser.add_argument(
        "--baud",
        type=int,
        choices=tuple(DEFINE_NAMES),
        action="append",
        help="nominal rate to sweep; repeat for both (default: both)",
    )
    parser.add_argument(
        "--candidate",
        type=parse_candidate,
        action="append",
        default=[],
        help="override defaults, for example 2000000=1957187",
    )
    parser.add_argument("--file", default=DEFAULT_FILE)
    parser.add_argument("--length", type=int, default=64 * 1024)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--max-rejections", type=int, default=20)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--probe-wall-timeout", type=float, default=45.0)
    parser.add_argument("--flash-baud", type=int, default=460_800)
    parser.add_argument("--build-dir", type=pathlib.Path)
    parser.add_argument("--csv", type=pathlib.Path)
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print the candidate plan without building or touching the board",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if (
        args.length <= 0
        or args.repetitions <= 0
        or args.max_rejections <= 0
        or args.timeout <= 0
        or args.probe_wall_timeout <= 0
    ):
        raise SystemExit(
            "length, repetitions, rejection limit, and timeouts must be positive"
        )
    project = PROJECTS[args.project]
    bauds = args.baud or list(DEFINE_NAMES)
    custom = {baud: [] for baud in DEFINE_NAMES}
    for nominal, calibrated in args.candidate:
        custom[nominal].append(calibrated)
    candidates = {
        baud: tuple(custom[baud]) or default_candidates(baud)
        for baud in bauds
    }
    build_directory = (
        args.build_dir.resolve()
        if args.build_dir
        else project / "build-uart-sweep"
    )
    csv_path = (
        args.csv.resolve()
        if args.csv
        else REPOSITORY / ".tmp" / "uart-baud-sweep.csv"
    )

    calibrated = {2_000_000: 2_000_000, 3_000_000: 3_000_000}
    original_calibration = dict(calibrated)
    print(f"project={project}")
    print(f"build={build_directory}")
    for baud in bauds:
        print(f"{baud}: {', '.join(map(str, candidates[baud]))}")
    if args.dry_run:
        return 0

    modules = load_uart_modules(project)
    wait_for_application(modules, args.port, 30.0)
    all_rows: list[ProbeResult] = []
    last_flashed: dict[int, int] | None = None
    for nominal_baud in bauds:
        candidate_rows: dict[int, list[ProbeResult]] = {}
        for candidate in candidates[nominal_baud]:
            calibrated[nominal_baud] = candidate
            print(
                f"\n=== nominal {nominal_baud}, ESP32 {candidate} "
                f"({candidate / nominal_baud:.6f}) ===",
                flush=True,
            )
            build_variant(project, build_directory, calibrated)
            flash_variant(
                project, build_directory, args.port, args.flash_baud
            )
            last_flashed = dict(calibrated)
            wait_for_application(modules, args.port, 30.0)
            rows = []
            for repetition in range(1, args.repetitions + 1):
                row = bounded_probe(
                    project,
                    (
                        args.port,
                        nominal_baud,
                        candidate,
                        args.file,
                        args.length,
                        args.timeout,
                        args.max_rejections,
                        repetition,
                    ),
                    args.probe_wall_timeout,
                )
                rows.append(row)
                all_rows.append(row)
                print(
                    f"run={repetition} status={row.status} "
                    f"crc_rejections={row.crc_rejections} "
                    f"accepted={row.accepted_blocks} "
                    f"crc={row.crc32 or '-'} "
                    f"seconds={row.elapsed_seconds:.3f} {row.detail}",
                    flush=True,
                )
            candidate_rows[candidate] = rows
            write_results(csv_path, all_rows)

        passing = [
            value
            for value, rows in candidate_rows.items()
            if candidate_is_clean(rows, args.length)
        ]
        if not passing:
            closest = max(
                candidate_rows,
                key=lambda value: (
                    sum(row.accepted_blocks for row in candidate_rows[value]),
                    -sum(
                        row.crc_rejections for row in candidate_rows[value]
                    ),
                ),
            )
            calibrated[nominal_baud] = original_calibration[nominal_baud]
            print(
                f"NO PASS nominal={nominal_baud}; "
                f"closest={closest} "
                f"accepted={sum(row.accepted_blocks for row in candidate_rows[closest])}; "
                f"restoring={calibrated[nominal_baud]}",
                flush=True,
            )
            continue
        best = min(
            passing,
            key=lambda value: (
                result_score(candidate_rows[value]),
                abs(value - nominal_baud),
            ),
        )
        calibrated[nominal_baud] = best
        print(
            f"BEST nominal={nominal_baud} calibrated={best} "
            f"coefficient={best / nominal_baud:.6f} "
            f"score={result_score(candidate_rows[best])}",
            flush=True,
        )

    if last_flashed != calibrated:
        print("\n=== flashing best combined calibration ===", flush=True)
        build_variant(project, build_directory, calibrated)
        flash_variant(project, build_directory, args.port, args.flash_baud)
        wait_for_application(modules, args.port, 30.0)
    else:
        print("\n=== best combined calibration is already flashed ===")
    resume_application(modules, args.port, args.timeout)
    print(
        "selected " + " ".join(
            f"{baud}={calibrated[baud]}"
            for baud in sorted(calibrated)
        )
    )
    print(csv_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
