# ESP32 IMA ADPCM physical test, 2026-08-03

The primary C99 player was built with `-O3` and flashed to the
ESP32-2432S028 on COM8 at 460800 baud. Esptool verified the bootloader,
partition table and application hashes before the hard reset.

The source `out/sources/VideoFormatRegression.mkv` was transcoded to HLV v15,
320x240 at 30 fps with mono IMA ADPCM at 32000 Hz. It contains 60 frames and
32340 compressed audio bytes. Its 1066/1067-sample audio intervals produce
539/540-byte blocks, deliberately larger than the 512-byte stdio refill.

The physical run decoded frames 1 through 60 with no telemetry gaps. Audio
telemetry reported zero rebuffers, zero underrun samples and zero inserted
silence chunks. The decoded PCM16 queue held 4012 bytes at the last report.

A 60-frame run of the restored BPV v7 + PCM_U8 selection also completed with
zero gaps, zero display skips, zero rebuffers, zero underrun samples and zero
silence chunks.

Only `Regression_IMAADPCM.hlv` was uploaded for this test. After capture,
the original `play.txt` (CRC32 `a4af2ee3`) was restored, the exact test file
was deleted, and a fresh SD listing confirmed the original 52 files remained,
including `play.txt` and `crc32.txt`.
