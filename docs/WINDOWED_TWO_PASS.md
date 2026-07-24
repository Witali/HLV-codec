# Bounded local two-pass bitrate control

## Goal

Support bitrate-oriented offline encoding from a non-seekable FFmpeg pipe
without reading or storing the complete source twice.

## Operation

For each local fragment, normally 10 seconds:

1. `hlvenc` reads raw YUV420 frames from stdin into a temporary file.
2. The current predictive encoder state is cloned.
3. The clone encodes the complete fragment at several candidate qsteps.
4. A bracketed multiplicative/binary search selects the qstep whose measured
   fragment size is closest to the local byte budget.
5. The original encoder encodes the same temporary fragment once at that
   qstep and writes packets to the real HLV stream.
6. The temporary file is closed and the next fragment is read.

The first and output stages start from identical predictive state and use the
same constant qstep within the fragment. Therefore the output stage reproduces
the selected probe size exactly. This avoids relying on a fragile analytical
size model.

## CLI

```sh
ffmpeg -i input.mp4 -vf "scale=320:240,fps=25,format=yuv420p" \
  -f yuv4mpegpipe - \
| ./codecs/hlv/hlvenc - output.hlv \
    --bitrate 400 \
    --two-pass-window 10 \
    --two-pass-trials 5 \
    --two-pass-log windows.csv
```

- `--two-pass-window X`: duration of each independently analysed fragment.
- `--two-pass-trials N`: maximum whole-fragment qstep probes, 2 through 10.
- `--two-pass-log FILE`: CSV diagnostics for each fragment.

## Memory and storage

The complete source is never retained. At QVGA/25 fps, a 10-second raw-YUV420
fragment is about 28.8 MB. `tmpfile()` allows this to spill to local storage
rather than consuming an equally large permanent RAM buffer. Only one decoded
frame and the encoder states remain in memory.

## Initial validation

- Smooth 12-second input, 10 fps, target 400 kbit/s, windows 10+2 seconds:
  409.4 kbit/s overall.
- Mixed 8-second input, 25 fps, target 750 kbit/s, fast preset:
  742.4 kbit/s overall.
- A 2-second mixed fragment at target 750 kbit/s:
  760.6 kbit/s.

The current version assigns one qstep to the complete fragment. Future work
will evaluate cautious within-window allocation, sparse probe frames for slow
presets, and adaptive K/P decisions, while retaining exact predictive-state
reproducibility.
