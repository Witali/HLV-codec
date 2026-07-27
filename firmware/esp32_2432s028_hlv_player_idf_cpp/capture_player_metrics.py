#!/usr/bin/env python3
"""Collect normal-player video and optional audio telemetry from an ESP32 UART."""

from __future__ import annotations

import argparse
import collections
import csv
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
    bpv_input_us: int
    bpv_block_us: int
    bpv_reference_us: int
    bpv_input_calls: int
    bpv_input_bytes: int


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
    decode_frames: int
    decode_us: int
    convert_us: int


@dataclass(frozen=True)
class VideoRecord:
    width: int
    height: int
    fps_num: int
    fps_den: int
    audio_rate: int
    frames: int


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
    if len(fields) not in (7, 12) or fields[0] != "F":
        return None
    try:
        values = [int(value) for value in fields[1:]]
    except ValueError:
        return None
    if len(values) == 6:
        values.extend((0, 0, 0, 0, 0))
    return FrameRecord(*values)


def parse_audio(line: str) -> AudioRecord | None:
    fields = line.split(",")
    if len(fields) not in (10, 12, 13) or fields[0] != "A":
        return None
    try:
        values = [int(value) for value in fields[1:]]
    except ValueError:
        return None
    if len(values) == 9:
        values.extend((0, 0))
    if len(values) == 11:
        values.append(0)
    return AudioRecord(*values)


def parse_video(line: str) -> VideoRecord | None:
    marker = line.find("V,")
    if marker < 0:
        return None
    fields = line[marker:].split(",")
    if len(fields) != 7 or fields[0] != "V":
        return None
    try:
        values = [int(value) for value in fields[1:]]
    except ValueError:
        return None
    record = VideoRecord(*values)
    if record.fps_num <= 0 or record.fps_den <= 0:
        return None
    return record


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
    parser.add_argument(
        "--output-csv",
        help="write every collected frame timing record to this CSV file",
    )
    args = parser.parse_args()
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("--frames and --timeout must be positive")

    frames: list[FrameRecord] = []
    audio: list[AudioRecord] = []
    video: VideoRecord | None = None
    statuses: list[str] = []
    recent_lines: collections.deque[str] = collections.deque(maxlen=40)
    first_frame_time: float | None = None
    last_frame_time: float | None = None
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
            # low through RTS for 200 ms, then release EN. This is deliberately
            # different from the ROM-download sequence in esptool.cfg.
            port.dtr = False
            port.rts = True
            time.sleep(0.2)
            port.rts = False
        while len(frames) < args.frames and time.monotonic() < deadline:
            raw = port.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="ignore").strip()
            if line:
                recent_lines.append(line)
            if line.startswith("S,"):
                statuses.append(line)
                continue
            video_record = parse_video(line)
            if video_record is not None:
                video = video_record
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
                received_at = time.monotonic()
                if first_frame_time is None:
                    first_frame_time = received_at
                last_frame_time = received_at
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
        for line in recent_lines:
            if not line.startswith("S,"):
                print(line, file=sys.stderr)
        return 3

    if args.output_csv:
        with open(args.output_csv, "w", newline="", encoding="utf-8") as output:
            writer = csv.writer(output)
            writer.writerow(
                (
                    "frame",
                    "sd_us",
                    "decode_us",
                    "render_us",
                    "work_us",
                    "present_us",
                    "bpv_input_us",
                    "bpv_block_us",
                    "bpv_reference_us",
                    "bpv_input_calls",
                    "bpv_input_bytes",
                )
            )
            for record in frames:
                writer.writerow(
                    (
                        record.frame,
                        record.sd_us,
                        record.decode_us,
                        record.render_us,
                        record.work_us,
                        record.present_us,
                        record.bpv_input_us,
                        record.bpv_block_us,
                        record.bpv_reference_us,
                        record.bpv_input_calls,
                        record.bpv_input_bytes,
                    )
                )

    print(
        f"frames={frames[0].frame}-{frames[-1].frame} "
        f"count={len(frames)} gaps=0 audio_records={len(audio)}"
    )
    if video is None:
        print("no video timing record received", file=sys.stderr)
        return 4
    frame_period_us = 1_000_000.0 * video.fps_den / video.fps_num
    elapsed = (
        last_frame_time - first_frame_time
        if first_frame_time is not None and last_frame_time is not None
        else 0.0
    )
    observed_fps = (len(frames) - 1) / elapsed if elapsed > 0 else 0.0
    print(
        f"video={video.width}x{video.height} "
        f"fps={video.fps_num}/{video.fps_den} "
        f"frame_period_us={frame_period_us:.3f} "
        f"observed_fps={observed_fps:.3f} audio_rate={video.audio_rate}"
    )
    print_metric("SD", [record.sd_us for record in frames])
    print_metric("Decode", [record.decode_us for record in frames])
    print_metric("Render", [record.render_us for record in frames])
    print_metric("Work", [record.work_us for record in frames])
    if any(record.bpv_input_calls for record in frames):
        print_metric(
            "BPV input", [record.bpv_input_us for record in frames]
        )
        print_metric(
            "BPV blocks", [record.bpv_block_us for record in frames]
        )
        print_metric(
            "BPV reference",
            [record.bpv_reference_us for record in frames],
        )
        print(
            "BPV input: "
            f"avg_calls={statistics.fmean(record.bpv_input_calls for record in frames):.2f} "
            f"avg_bytes={statistics.fmean(record.bpv_input_bytes for record in frames):.1f}"
        )
    print(
        "work_over_frame_period="
        f"{sum(record.work_us > frame_period_us for record in frames)} "
        f"display_skips={sum(record.render_us == 0 for record in frames)}"
    )

    if not audio:
        if video.audio_rate == 0:
            print("audio=disabled")
            return 0
        print("no audio telemetry records received", file=sys.stderr)
        return 5
    last = audio[-1]
    print(
        f"audio_last_frame={last.frame} queued={last.queued} "
        f"pending={last.pending} played={last.played} "
        f"rebuffers={last.rebuffers} "
        f"underrun_samples={last.underrun_samples} "
        f"silence_chunks={last.silence_chunks} "
        f"loop_events={last.loop_events} loop_chunks={last.loop_chunks} "
        f"mp2_decode_frames={last.decode_frames} "
        f"mp2_decode_avg_us="
        f"{last.decode_us / last.decode_frames if last.decode_frames else 0:.1f} "
        f"mp2_convert_avg_us="
        f"{last.convert_us / last.decode_frames if last.decode_frames else 0:.1f}"
    )
    if not args.allow_audio_underrun and (
        last.rebuffers or last.underrun_samples or last.silence_chunks
    ):
        print("audio underrun acceptance check failed", file=sys.stderr)
        return 6
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
