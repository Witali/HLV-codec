#!/usr/bin/env python3
"""Generate deterministic benchmark scenes without network access.

The suite isolates smooth motion, dynamic graphics, photo panning, moving UI,
and fine texture so individual codec tools can be evaluated independently.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from skimage import data

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "bench" / "sources"


def run(cmd: list[str], stdin=None) -> None:
    print("+", " ".join(map(str, cmd)), file=sys.stderr)
    subprocess.run(cmd, stdin=stdin, check=True)


def ffmpeg_lavfi(name: str, expr: str, duration: int, fps: int) -> None:
    path = OUT / name
    if path.exists():
        return
    run([
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-f", "lavfi", "-i", expr,
        "-t", str(duration), "-an",
        "-vf", f"fps={fps},format=yuv420p",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "0",
        str(path),
    ])


def ffmpeg_loop(name: str, source: Path, duration: int, fps: int) -> None:
    path = OUT / name
    if path.exists():
        return
    run([
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-stream_loop", "-1", "-i", str(source),
        "-t", str(duration), "-an",
        "-vf", f"scale=320:240:flags=lanczos,fps={fps},format=yuv420p",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "0",
        str(path),
    ])


def to_rgb(image: np.ndarray) -> np.ndarray:
    if image.ndim == 2:
        image = np.repeat(image[..., None], 3, axis=2)
    if image.shape[2] > 3:
        image = image[..., :3]
    if image.dtype == np.bool_:
        image = image.astype(np.uint8) * 255
    elif image.dtype != np.uint8:
        x = image.astype(np.float32)
        x -= x.min()
        if x.max() > 0:
            x *= 255.0 / x.max()
        image = np.clip(x, 0, 255).astype(np.uint8)
    return image


def pan_zoom_frame(image: np.ndarray, phase: float, out_w: int = 320, out_h: int = 240) -> np.ndarray:
    image = to_rgb(image)
    h, w, _ = image.shape
    base_scale = max(out_w / w, out_h / h)
    zoom = 1.08 + 0.22 * (0.5 - 0.5 * np.cos(phase * 2.0 * np.pi))
    rw = max(out_w, int(round(w * base_scale * zoom)))
    rh = max(out_h, int(round(h * base_scale * zoom)))
    resized = np.asarray(Image.fromarray(image).resize((rw, rh), Image.Resampling.LANCZOS))
    max_x = rw - out_w
    max_y = rh - out_h
    x = int(round(max_x * (0.5 + 0.45 * np.sin(phase * 2.0 * np.pi))))
    y = int(round(max_y * (0.5 + 0.45 * np.cos(phase * 2.0 * np.pi * 0.73))))
    x = min(max(x, 0), max_x)
    y = min(max(y, 0), max_y)
    return resized[y:y + out_h, x:x + out_w]


def photo_pan(duration: int, fps: int) -> None:
    path = OUT / "photo_pan_5min.mp4"
    if path.exists():
        return
    images = [
        data.astronaut(), data.coffee(), data.rocket(), data.hubble_deep_field(),
        data.chelsea(), data.grass(), data.gravel(), data.brick(), data.moon(),
        data.camera(), data.coins(), data.page(), data.text(), data.horse(),
    ]
    scene_frames = max(1, 20 * fps)
    total = duration * fps
    cmd = [
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "320x240", "-r", str(fps),
        "-i", "-", "-an", "-c:v", "libx264", "-preset", "veryfast", "-crf", "0",
        "-pix_fmt", "yuv420p", str(path),
    ]
    print("+", " ".join(cmd), file=sys.stderr)
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    assert proc.stdin is not None
    try:
        for i in range(total):
            scene = i // scene_frames
            local = (i % scene_frames) / scene_frames
            a = pan_zoom_frame(images[scene % len(images)], local)
            # Short crossfade to the next scene exercises scene cuts without a single hard pattern.
            fade_len = max(1, fps)
            if i % scene_frames >= scene_frames - fade_len:
                alpha = (i % scene_frames - (scene_frames - fade_len)) / fade_len
                b = pan_zoom_frame(images[(scene + 1) % len(images)], 0.0)
                a = np.clip(a.astype(np.float32) * (1.0 - alpha) + b.astype(np.float32) * alpha,
                            0, 255).astype(np.uint8)
            proc.stdin.write(a.tobytes())
    finally:
        proc.stdin.close()
    if proc.wait() != 0:
        raise subprocess.CalledProcessError(proc.returncode, cmd)




def moving_ui(duration: int, fps: int) -> None:
    """Generate a deterministic moving desktop/UI workload.

    It contains sharp text, scrolling rows, animated charts, moving windows,
    blinking cursors and a mouse pointer.  Unlike the old example-movie based
    screen source, every second contains motion while most of the background
    remains reusable by inter prediction.
    """
    path = OUT / "moving_ui_5min.mp4"
    if path.exists():
        return
    base_seconds = 20
    base = OUT / "moving_ui_base.mp4"
    font = ImageFont.load_default()
    cmd = [
        "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "320x240",
        "-r", str(fps), "-i", "-", "-an", "-c:v", "libx264",
        "-preset", "veryfast", "-crf", "0", "-pix_fmt", "yuv420p",
        str(base),
    ]
    print("+", " ".join(cmd), file=sys.stderr)
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    assert proc.stdin is not None
    try:
        for i in range(base_seconds * fps):
            t = i / fps
            image = Image.new("RGB", (320, 240), (24, 29, 38))
            d = ImageDraw.Draw(image)
            # Desktop grid and top bar.
            for x in range(0, 320, 16):
                d.line((x, 22, x, 239), fill=(31, 37, 48))
            for y in range(22, 240, 16):
                d.line((0, y, 319, y), fill=(31, 37, 48))
            d.rectangle((0, 0, 319, 21), fill=(44, 53, 69))
            d.text((7, 6), "HLV CONTROL DESK", font=font, fill=(230, 235, 245))
            d.text((245, 6), f"{int(t):02d}:{int((t%1)*fps):02d}", font=font,
                   fill=(175, 205, 255))

            # Main window moves by sub-pixel-like slow integer steps after scaling.
            wx = 11 + int(7 * np.sin(t * 0.63))
            wy = 32 + int(4 * np.cos(t * 0.47))
            d.rectangle((wx, wy, wx + 193, wy + 128), fill=(236, 239, 244),
                        outline=(88, 98, 116), width=1)
            d.rectangle((wx, wy, wx + 193, wy + 16), fill=(66, 112, 180))
            d.text((wx + 6, wy + 4), "Telemetry / Channels", font=font,
                   fill=(255, 255, 255))
            # Scrolling table: rows shift continuously and numbers change.
            row_h = 13
            scroll = int((t * 18) % row_h)
            first = int(t * 18 / row_h)
            for row in range(9):
                yy = wy + 20 + row * row_h - scroll
                if yy < wy + 17 or yy + row_h > wy + 126:
                    continue
                idx = first + row
                fill = (247, 249, 252) if idx & 1 else (225, 231, 240)
                d.rectangle((wx + 4, yy, wx + 188, yy + row_h - 1), fill=fill)
                value = 50 + int(45 * np.sin(t * 1.1 + idx * 0.71))
                d.text((wx + 8, yy + 2), f"CH{idx%32:02d}", font=font,
                       fill=(24, 32, 48))
                d.rectangle((wx + 48, yy + 4, wx + 48 + value, yy + 8),
                            fill=(40, 143, 94))
                d.text((wx + 153, yy + 2), f"{value:03d}%", font=font,
                       fill=(20, 28, 42))

            # Right chart panel with a moving waveform.
            px, py = 216, 35
            d.rectangle((px, py, 313, 129), fill=(12, 18, 27),
                        outline=(90, 107, 135))
            d.text((px + 5, py + 4), "LIVE GRAPH", font=font, fill=(184, 218, 255))
            for gy in range(py + 20, py + 90, 14):
                d.line((px + 4, gy, px + 93, gy), fill=(31, 48, 62))
            pts = []
            for xx in range(88):
                phase = t * 2.5 + xx * 0.16
                yy = py + 57 + int(20 * np.sin(phase) + 7 * np.sin(phase * 0.37))
                pts.append((px + 5 + xx, yy))
            d.line(pts, fill=(74, 231, 154), width=1)

            # Sliding notification card and progress animation.
            phase = (t % 5.0) / 5.0
            nx = 320 - int(112 * min(1.0, phase * 5.0))
            if phase > 0.75:
                nx += int(112 * ((phase - 0.75) / 0.25))
            ny = 145
            d.rectangle((nx, ny, nx + 108, ny + 57), fill=(252, 252, 248),
                        outline=(105, 112, 125))
            d.text((nx + 6, ny + 6), "Background task", font=font,
                   fill=(29, 34, 43))
            progress = int((t * 23) % 100)
            d.rectangle((nx + 7, ny + 27, nx + 99, ny + 36), fill=(216, 220, 226))
            d.rectangle((nx + 7, ny + 27, nx + 7 + int(92 * progress / 100), ny + 36),
                        fill=(61, 126, 219))
            d.text((nx + 71, ny + 42), f"{progress:02d}%", font=font,
                   fill=(45, 51, 61))

            # Status line with blinking caret.
            d.rectangle((8, 218, 311, 235), fill=(10, 13, 18),
                        outline=(76, 84, 98))
            command = "build --codec hlv --profile balanced"
            d.text((13, 223), command, font=font, fill=(176, 222, 180))
            if int(t * 2) & 1:
                caret_x = 13 + d.textlength(command, font=font)
                d.rectangle((int(caret_x) + 1, 222, int(caret_x) + 2, 231),
                            fill=(220, 240, 220))

            # Mouse pointer follows a non-repeating-looking Lissajous path.
            mx = 160 + int(135 * np.sin(t * 0.91))
            my = 125 + int(95 * np.sin(t * 1.37 + 0.8))
            pointer = [(mx, my), (mx, my + 14), (mx + 4, my + 10),
                       (mx + 8, my + 17), (mx + 11, my + 15),
                       (mx + 7, my + 8), (mx + 14, my + 8)]
            d.polygon(pointer, fill=(255, 255, 255), outline=(0, 0, 0))
            proc.stdin.write(np.asarray(image, dtype=np.uint8).tobytes())
    finally:
        proc.stdin.close()
    if proc.wait() != 0:
        raise subprocess.CalledProcessError(proc.returncode, cmd)
    ffmpeg_loop("moving_ui_5min.mp4", base, duration, fps)

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--duration", type=int, default=300)
    ap.add_argument("--fps", type=int, default=10)
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    ffmpeg_lavfi(
        "dynamic_testsrc_5min.mp4",
        f"testsrc2=size=320x240:rate={args.fps}:duration={args.duration}",
        args.duration, args.fps,
    )
    ffmpeg_lavfi(
        "fine_texture_5min.mp4",
        f"life=size=320x240:rate={args.fps}:ratio=0.18:seed=1979:mold=10:stitch=1",
        args.duration, args.fps,
    )

    world = Path("/opt/pyvenv/lib/python3.13/site-packages/gradio/media_assets/videos/world.mp4")
    example = Path("/usr/share/texlive/texmf-dist/tex/latex/mwe/example-movie.mp4")
    if world.exists():
        ffmpeg_loop("smooth_space_5min.mp4", world, args.duration, args.fps)
    if example.exists():
        ffmpeg_loop("screen_content_5min.mp4", example, args.duration, args.fps)
    photo_pan(args.duration, args.fps)
    moving_ui(args.duration, args.fps)

    print(f"Generated suite in {OUT}")


if __name__ == "__main__":
    main()