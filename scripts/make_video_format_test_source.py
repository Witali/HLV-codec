#!/usr/bin/env python3
"""Create a short deterministic picture-rich source for codec regressions."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from skimage import data


def to_rgb(image: np.ndarray) -> np.ndarray:
    if image.ndim == 2:
        image = np.repeat(image[..., None], 3, axis=2)
    if image.shape[2] > 3:
        image = image[..., :3]
    if image.dtype != np.uint8:
        values = image.astype(np.float32)
        values -= values.min()
        maximum = values.max()
        if maximum > 0:
            values *= 255.0 / maximum
        image = np.clip(values, 0, 255).astype(np.uint8)
    return image


def pan_zoom(
    image: np.ndarray,
    phase: float,
    output_width: int,
    output_height: int,
) -> np.ndarray:
    image = to_rgb(image)
    height, width, _ = image.shape
    scale = max(output_width / width, output_height / height)
    zoom = 1.10 + 0.20 * (0.5 - 0.5 * np.cos(phase * 2.0 * np.pi))
    resized_width = max(output_width, round(width * scale * zoom))
    resized_height = max(output_height, round(height * scale * zoom))
    resized = np.asarray(
        Image.fromarray(image).resize(
            (resized_width, resized_height),
            Image.Resampling.LANCZOS,
        )
    )
    maximum_x = resized_width - output_width
    maximum_y = resized_height - output_height
    x = round(maximum_x * (0.5 + 0.45 * np.sin(phase * 2.0 * np.pi)))
    y = round(
        maximum_y *
        (0.5 + 0.45 * np.cos(phase * 2.0 * np.pi * 0.71))
    )
    x = min(max(x, 0), maximum_x)
    y = min(max(y, 0), maximum_y)
    return resized[y:y + output_height, x:x + output_width].copy()


def decorate(frame: np.ndarray, frame_index: int, frame_count: int) -> bytes:
    image = Image.fromarray(frame)
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()
    phase = frame_index / max(1, frame_count - 1)

    # Moving high-contrast geometry exercises edges, residuals and motion.
    box_x = round(12 + (image.width - 76) * phase)
    box_y = round(28 + 34 * np.sin(phase * np.pi * 6.0))
    draw.rounded_rectangle(
        (box_x, box_y, box_x + 62, box_y + 34),
        radius=5,
        fill=(245, 188, 36),
        outline=(22, 28, 36),
        width=2,
    )
    draw.ellipse(
        (
            image.width - 76 - box_x // 3,
            image.height - 64 - box_y // 5,
            image.width - 40 - box_x // 3,
            image.height - 28 - box_y // 5,
        ),
        fill=(30, 210, 170),
        outline=(255, 255, 255),
        width=2,
    )

    # A changing status strip makes every frame visibly unique.
    draw.rectangle((0, image.height - 20, image.width - 1, image.height - 1),
                   fill=(12, 18, 28))
    progress = round((image.width - 116) * (frame_index + 1) / frame_count)
    draw.rectangle((105, image.height - 14, 105 + progress, image.height - 7),
                   fill=(74, 155, 255))
    draw.text(
        (6, image.height - 16),
        f"FORMAT TEST {frame_index + 1:03d}/{frame_count:03d}",
        font=font,
        fill=(238, 242, 250),
    )
    return np.asarray(image, dtype=np.uint8).tobytes()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--frames", type=int, default=60)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    args = parser.parse_args()
    if args.frames < 8:
        parser.error("--frames must be at least 8")
    if not 1 <= args.fps <= 30:
        parser.error("--fps must be in the range 1..30")
    if args.width < 16 or args.height < 16:
        parser.error("picture dimensions must be at least 16x16")

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    duration = args.frames / args.fps
    command = [
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-f", "rawvideo", "-pix_fmt", "rgb24",
        "-s", f"{args.width}x{args.height}", "-r", str(args.fps),
        "-i", "-",
        "-f", "lavfi", "-i",
        f"sine=frequency=523.25:sample_rate=48000:duration={duration:.9f}",
        "-map", "0:v:0", "-map", "1:a:0",
        "-frames:v", str(args.frames),
        "-c:v", "ffv1", "-level", "3", "-pix_fmt", "yuv420p",
        "-c:a", "pcm_s16le", "-shortest", str(output),
    ]
    pictures = [
        data.astronaut(),
        data.coffee(),
        data.rocket(),
        data.hubble_deep_field(),
        data.chelsea(),
        data.camera(),
    ]
    process = subprocess.Popen(command, stdin=subprocess.PIPE)
    assert process.stdin is not None
    try:
        scene_frames = max(4, args.frames // len(pictures))
        fade_frames = max(2, min(args.fps // 5, scene_frames // 3))
        for frame_index in range(args.frames):
            scene = frame_index // scene_frames
            scene_offset = frame_index % scene_frames
            phase = scene_offset / max(1, scene_frames - 1)
            frame = pan_zoom(
                pictures[scene % len(pictures)],
                phase,
                args.width,
                args.height,
            )
            if scene_offset >= scene_frames - fade_frames:
                alpha = (
                    scene_offset - (scene_frames - fade_frames)
                ) / fade_frames
                following = pan_zoom(
                    pictures[(scene + 1) % len(pictures)],
                    0.0,
                    args.width,
                    args.height,
                )
                frame = np.clip(
                    frame.astype(np.float32) * (1.0 - alpha) +
                    following.astype(np.float32) * alpha,
                    0,
                    255,
                ).astype(np.uint8)
            process.stdin.write(
                decorate(frame, frame_index, args.frames)
            )
    finally:
        process.stdin.close()
    return process.wait()


if __name__ == "__main__":
    raise SystemExit(main())
