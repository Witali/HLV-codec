#!/usr/bin/env python3
"""Collect normal-player video and optional audio telemetry from an ESP32 UART."""

from __future__ import annotations

import argparse
import collections
import csv
import math
import re
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


@dataclass(frozen=True)
class HlvProfileRecord:
    total_cycles: int
    input_cycles: int
    input_bytes: int
    input_refills: int
    crc_cycles: int
    prediction_cycles: int
    residual_cycles: int
    inverse_wht_cycles: int
    packing_cycles: int
    reference_commit_cycles: int
    row_guard_wait_us: int


def percentile(values: list[int], percent: int) -> int:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * percent / 100) - 1)]


def print_metric(name: str, values: list[int]) -> None:
    print(
        f"{name}: avg={statistics.fmean(values):.1f} us "
        f"min={min(values)} p50={percentile(values, 50)} "
        f"p95={percentile(values, 95)} max={max(values)}"
    )


FRAME_RECORD_PATTERN = re.compile(
    r"F,(?:\d+,){10}\d+|F,(?:\d+,){5}\d+"
)


def parse_embedded_frames(line: str) -> list[FrameRecord]:
    """Recover frame records even when another core interleaves UART text."""
    records: list[FrameRecord] = []
    for match in FRAME_RECORD_PATTERN.finditer(line):
        record = parse_frame(match.group(0))
        if record is not None:
            records.append(record)
    return records


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


def parse_hlv_profile(line: str) -> HlvProfileRecord | None:
    fields = line.split(",")
    if len(fields) != 12 or fields[0] != "H":
        return None
    try:
        values = [int(value) for value in fields[1:]]
    except ValueError:
        return None
    return HlvProfileRecord(*values)


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
        "--seek-ms",
        type=int,
        help="seek to this playback position before collecting frames",
    )
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
    if (args.frames <= 0 or args.timeout <= 0 or
            (args.seek_ms is not None and args.seek_ms < 0)):
        parser.error("--frames/--timeout must be positive and --seek-ms non-negative")

    frames: list[FrameRecord] = []
    audio: list[AudioRecord] = []
    hlv_profiles: list[HlvProfileRecord] = []
    video: VideoRecord | None = None
    statuses: list[str] = []
    recent_lines: collections.deque[str] = collections.deque(maxlen=40)
    first_frame_time: float | None = None
    last_frame_time: float | None = None
    deadline = time.monotonic() + args.timeout

    port = serial.Serial()
    port.port = args.port
    port.baudrate = args.baud
    port.bytesize = serial.EIGHTBITS
    port.parity = serial.PARITY_NONE
    port.stopbits = serial.STOPBITS_TWO
    port.timeout = 0.5
    port.dsrdtr = False
    port.rtscts = False
    # Leave the modified CH340C auto-boot circuit idle. Merely collecting
    # telemetry must not reset the board or enter the ROM downloader.
    port.dtr = False
    port.rts = False
    port.open()
    try:
        try:
            port.set_buffer_size(rx_size=1024 * 1024, tx_size=64 * 1024)
        except (AttributeError, NotImplementedError, OSError):
            pass
        if args.reset:
            port.reset_input_buffer()
            # Normal application reset: keep GPIO0 released (DTR=0), hold EN
            # low through RTS for 200 ms, then release EN. This is deliberately
            # different from the ROM-download sequence in esptool.cfg.
            port.dtr = False
            port.rts = True
            time.sleep(0.2)
            port.rts = False
            # Bytes already queued in the CH340/driver can arrive after the
            # pre-reset flush while EN is still asserted. The ESP32 boot and
            # SD mount take much longer than this settling interval, so a
            # second flush removes only stale pre-reset telemetry.
            time.sleep(0.05)
            port.reset_input_buffer()
        if args.seek_ms is not None:
            port.reset_input_buffer()
            port.write(f"HLVSEEK 1 {args.seek_ms}\n".encode("ascii"))
            port.flush()
            seek_done = False
            while time.monotonic() < deadline:
                raw = port.readline()
                if not raw:
                    continue
                line = raw.decode("ascii", errors="ignore").strip()
                if line:
                    recent_lines.append(line)
                video_record = parse_video(line)
                if video_record is not None:
                    video = video_record
                if "HLVSEEKERR " in line:
                    print(line, file=sys.stderr)
                    return 5
                marker = line.find("HLVSEEKDONE 1 ")
                if marker >= 0:
                    fields = line[marker:].split()
                    if len(fields) >= 5:
                        print(
                            f"seek_requested_ms={fields[2]} "
                            f"seek_actual_ms={fields[3]} "
                            f"seek_frame={fields[4]}"
                        )
                    seek_done = True
                    break
            if not seek_done:
                print("seek did not complete before timeout", file=sys.stderr)
                for line in recent_lines:
                    print(line, file=sys.stderr)
                return 5
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
            embedded_frames = parse_embedded_frames(line)
            for frame in embedded_frames:
                if frames and frame.frame != frames[-1].frame + 1:
                    print(
                        f"frame sequence gap: {frames[-1].frame} -> "
                        f"{frame.frame}",
                        file=sys.stderr,
                    )
                    print("recent UART lines:", file=sys.stderr)
                    for recent in recent_lines:
                        print(f"  {recent}", file=sys.stderr)
                    return 2
                received_at = time.monotonic()
                if first_frame_time is None:
                    first_frame_time = received_at
                last_frame_time = received_at
                frames.append(frame)
                if len(frames) >= args.frames:
                    break
            if embedded_frames:
                continue
            audio_record = parse_audio(line)
            if audio_record is not None:
                audio.append(audio_record)
                continue
            hlv_profile = parse_hlv_profile(line)
            if hlv_profile is not None:
                hlv_profiles.append(hlv_profile)
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
    if hlv_profiles:
        selected_profiles = hlv_profiles[: len(frames)]

        def profile_us(field: str) -> list[int]:
            return [
                round(getattr(record, field) / 240.0)
                for record in selected_profiles
            ]

        print(f"hlv_profile_records={len(selected_profiles)} cpu_mhz=240")
        print_metric("HLV total", profile_us("total_cycles"))
        print_metric("HLV input", profile_us("input_cycles"))
        print(
            "HLV input: "
            f"avg_refills={statistics.fmean(record.input_refills for record in selected_profiles):.2f} "
            f"avg_bytes={statistics.fmean(record.input_bytes for record in selected_profiles):.1f}"
        )
        print_metric("HLV CRC", profile_us("crc_cycles"))
        print_metric("HLV prediction", profile_us("prediction_cycles"))
        print_metric("HLV residual", profile_us("residual_cycles"))
        print_metric("HLV inverse-WHT", profile_us("inverse_wht_cycles"))
        print_metric("HLV packing", profile_us("packing_cycles"))
        print_metric(
            "HLV reference commit",
            profile_us("reference_commit_cycles"),
        )
        print_metric(
            "HLV row guard",
            [record.row_guard_wait_us for record in selected_profiles],
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
