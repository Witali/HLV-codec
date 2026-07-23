#!/usr/bin/env python3
"""Verify that parallel GOP encoding is byte-exact and enabled by default."""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import tempfile
from pathlib import Path


def write_fixture(video: Path, audio: Path) -> None:
    width, height, frames = 64, 48, 73
    chroma_width, chroma_height = width // 2, height // 2
    with video.open("wb") as stream:
        stream.write(
            f"YUV4MPEG2 W{width} H{height} F24:1 Ip A0:0 C420jpeg\n".encode()
        )
        for frame in range(frames):
            scene = 80 if 19 <= frame < 43 else 0
            y = bytes(
                (x * 3 + row * 5 + frame * 7 + scene) & 0xFF
                for row in range(height)
                for x in range(width)
            )
            u = bytes(
                (96 + x * 2 - row + frame * 3 + scene // 4) & 0xFF
                for row in range(chroma_height)
                for x in range(chroma_width)
            )
            v = bytes(
                (160 - x + row * 2 - frame * 2 - scene // 4) & 0xFF
                for row in range(chroma_height)
                for x in range(chroma_width)
            )
            stream.write(b"FRAME\n")
            stream.write(y)
            stream.write(u)
            stream.write(v)

    sample_count = (frames * 16000 + 23) // 24
    audio.write_bytes(bytes((128 + (sample % 97) - 48) & 0xFF
                            for sample in range(sample_count)))


def encode(encoder: Path, video: Path, audio: Path, output: Path,
           reconstruction: Path, threads: int | None) -> None:
    command = [
        str(encoder),
        str(video),
        str(output),
        "--preset", "balanced",
        "--quality", "45",
        "--gop", "12",
        "--syntax", "13",
        "--adaptive-gop",
        "--audio-u8", str(audio),
        "--audio-rate", "16000",
        "--recon", str(reconstruction),
    ]
    if threads is not None:
        command.extend(["--threads", str(threads)])
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(
            f"{' '.join(command)} failed\n{result.stdout}\n{result.stderr}"
        )


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--encoder", type=Path, default=Path("hlvenc"))
    args = parser.parse_args()
    encoder = args.encoder.resolve()

    with tempfile.TemporaryDirectory(prefix="hlv-thread-test-") as directory:
        root = Path(directory)
        video, audio = root / "input.y4m", root / "audio.u8"
        write_fixture(video, audio)

        outputs: dict[str, tuple[Path, Path]] = {}
        for name, threads in (("one", 1), ("four", 4), ("default", None)):
            hlv, recon = root / f"{name}.hlv", root / f"{name}.y4m"
            encode(encoder, video, audio, hlv, recon, threads)
            outputs[name] = hlv, recon

        one_hlv, one_recon = outputs["one"]
        for name in ("four", "default"):
            hlv, recon = outputs[name]
            if hlv.read_bytes() != one_hlv.read_bytes():
                raise AssertionError(
                    f"{name} HLV differs: {digest(hlv)} != {digest(one_hlv)}"
                )
            if recon.read_bytes() != one_recon.read_bytes():
                raise AssertionError(
                    f"{name} reconstruction differs: "
                    f"{digest(recon)} != {digest(one_recon)}"
                )

        print(
            "Threaded encoder: PASS "
            f"(SHA-256 {digest(one_hlv)}, default=4 threads)"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
