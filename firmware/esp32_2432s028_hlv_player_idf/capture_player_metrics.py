#!/usr/bin/env python3
"""Collect normal-player video and audio telemetry from an ESP32 UART."""

from __future__ import annotations

import argparse
import math
import statistics
import sys
import time
from dataclasses import dataclass

import serial


@dataclass(frozen=True)
class FrameRecord:
    frame: int
    sd_us: int
    decode_us: int
    render_us: int
    work_us: int
    present_us: int


@dataclass(frozen=True)
class AudioRecord:
    frame: int
    queued: int
    pending: int
    played: int
    rebuffers: int
    underrun_samples: int
    silence_chunks: int
    loop_events: int
    loop_chunks: int


def percentile(values: list[int], percent: int) -> int:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * percent / 100) - 1)]


def print_metric(name: str, values: list[int]) -> None:
    print(
        f"{name}: avg={statistics.fmean(values):.1f} us "
        f"p50={percentile(values, 50)} "
        f"p95={percentile(values, 95)} max={max(values)}"
    )


def parse_frame(line: str) -> FrameRecord | None:
    fields = line.split(",")
    if len(fields) != 7 or fields[0] != "F":
        return None
    try:
        values = [int(value) for value in fields[1:]]
    except ValueError:
        return None
    return FrameRecord(*values)


def parse_audio(line: str) -> AudioRecord | None:
    fields = line.split(",")
    if len(fields) != 10 or fields[0] != "A":
        return None
    try:
        values = [int(value) for value in fields[1:]]
    except ValueError:
        return None
    return AudioRecord(*values)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--frames", type=int, default=900)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument(
        "--reset",
        action="store_true",
        help="pulse EN through RTS before collecting records",
    )
    parser.add_argument(
        "--allow-audio-underrun",
        action="store_true",
        help="return success even if the audio telemetry reports an underrun",
    )
    args = parser.parse_args()
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("--frames and --timeout must be positive")

    frames: list[FrameRecord] = []
    audio: list[AudioRecord] = []
    statuses: list[str] = []
    deadline = time.monotonic() + args.timeout

    port = serial.Serial()
    port.port = args.port
    port.baudrate = args.baud
    port.timeout = 0.5
    port.dsrdtr = False
    port.rtscts = False
    # Leave the modified CH340C auto-boot circuit idle. Merely collecting
    # telemetry must not reset the board or enter the ROM downloader.
    port.dtr = False
    port.rts = False
    port.open()
    try:
        if args.reset:
            # Normal application reset: keep GPIO0 released (DTR=0), hold EN
            # low through RTS for 500 ms, then release EN. This is deliberately
            # different from the ROM-download sequence in esptool.cfg.
            port.dtr = False
            port.rts = True
            time.sleep(0.5)
            port.rts = False
        while len(frames) < args.frames and time.monotonic() < deadline:
            raw = port.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="ignore").strip()
            if line.startswith("S,"):
                statuses.append(line)
                continue
            frame = parse_frame(line)
            if frame is not None:
                if frames and frame.frame != frames[-1].frame + 1:
                    print(
                        f"frame sequence gap: {frames[-1].frame} -> "
                        f"{frame.frame}",
                        file=sys.stderr,
                    )
                    return 2
                frames.append(frame)
                continue
            audio_record = parse_audio(line)
            if audio_record is not None:
                audio.append(audio_record)
    finally:
        port.close()

    if len(frames) != args.frames:
        print(
            f"collected only {len(frames)} of {args.frames} frame records "
            f"and {len(audio)} audio records",
            file=sys.stderr,
        )
        for status in statuses[-8:]:
            print(status, file=sys.stderr)
        return 3

    print(
        f"frames={frames[0].frame}-{frames[-1].frame} "
        f"count={len(frames)} gaps=0 audio_records={len(audio)}"
    )
    print_metric("SD", [record.sd_us for record in frames])
    print_metric("Decode", [record.decode_us for record in frames])
    print_metric("Render", [record.render_us for record in frames])
    print_metric("Work", [record.work_us for record in frames])
    print(
        "work_over_66667us="
        f"{sum(record.work_us > 66667 for record in frames)}"
    )

    if not audio:
        print("no audio telemetry records received", file=sys.stderr)
        return 4
    last = audio[-1]
    print(
        f"audio_last_frame={last.frame} queued={last.queued} "
        f"pending={last.pending} played={last.played} "
        f"rebuffers={last.rebuffers} "
        f"underrun_samples={last.underrun_samples} "
        f"silence_chunks={last.silence_chunks} "
        f"loop_events={last.loop_events} loop_chunks={last.loop_chunks}"
    )
    if not args.allow_audio_underrun and (
        last.rebuffers or last.underrun_samples or last.silence_chunks
    ):
        print("audio underrun acceptance check failed", file=sys.stderr)
        return 5
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
