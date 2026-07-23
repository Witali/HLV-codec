# ESP32 physical decoder optimization TODO

This checklist tracks decoder and input-path experiments measured on the
connected ESP32-D0WD-V3 board.  A change is retained only when the physical
result improves and the decoded-frame hash remains unchanged.

## Measurement method

- Decoder-only tests use the deterministic 120-frame clip prepared from
  `out/video.hlv`: four complete 30-frame GOP windows beginning at frames
  0, 2236, 4500 and 6720.
- The CPU runs at 240 MHz.  Decoder time is read from `CCOUNT`; packet reads,
  frame hashing and UART output are outside the interval.
- Run each decoder variant at least three times and compare the median
  cycles/frame.  The complete 120-frame reconstruction hash must remain
  `be4876ff1c6b8461`.
- End-to-end player tests reset the board and collect at least 120 consecutive
  `F,...` records from COM8.  Compare SD, decode, render and presentation
  distributions separately.
- Audio-enabled acceptance runs collect at least 900 consecutive frames and
  require zero frame-sequence gaps, rebuffers, missing audio samples and
  silence DMA chunks.
- A result below 0.5% is treated as neutral unless repeated runs show a stable
  improvement.  Rejected experiments are reverted but remain documented here.

## Checklist

- [x] Record a fresh decoder-only physical baseline for the current v13-capable
      decoder and the representative v12 stream.
- [x] Measure selective IRAM placement for the bitreader refill, slow
      extraction and Exp-Golomb paths.
- [x] Compare the current `9 x 7,680` packet pool with `3 x 23,040` blocks in
      normal SD playback.  This changes fragmentation and read spans, not total
      packet capacity.
- [x] Compare 40 MHz DIO, 80 MHz DIO and 80 MHz QIO flash execution on the
      physical board.
- [x] Evaluate whether decoder-cycle RDO samples are needed.  They are deferred:
      the verified QIO/IRAM maximum is 58.38 ms, below the 66.67 ms frame
      interval, so changing the encoded picture is not justified yet.
- [x] Measure placing the inverse-WHT/add kernel in IRAM, retain it only if the
      physical decoder hash remains unchanged and the cycle count improves.
- [x] Verify the retained normal player for 900 frames with audio enabled and
      no frame-sequence gaps or missing audio samples.
- [x] Restore and flash the best verified normal-player configuration.

## Decoder-only results

Each row below was identical across all three physical runs.

| Variant | Mean cycles/frame | P50 | P95 | Max | Change | Hash | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| Flash bitreader, DIO 40 MHz | 3,764,549 | 2,734,080 | 11,195,450 | 16,609,914 | baseline | `be4876ff1c6b8461` | baseline |
| IRAM bitreader, DIO 40 MHz | 3,209,621 | 2,362,446 | 9,628,145 | 14,814,544 | -14.74% | `be4876ff1c6b8461` | accepted |
| IRAM bitreader, QIO 80 MHz | 2,776,047 | 1,995,995 | 8,673,122 | 14,010,337 | -13.51%; -26.25% cumulative | `be4876ff1c6b8461` | accepted |
| Fresh QIO/IRAM baseline, 2026-07-23 | 2,765,473 | 2,005,460 | 8,643,899 | 13,863,124 | fresh baseline | `be4876ff1c6b8461` | baseline |
| Inverse WHT/add in IRAM | 2,727,498 | 1,953,391 | 8,598,743 | 13,783,993 | -1.37% | `be4876ff1c6b8461` | accepted |

At 240 MHz the retained inverse-WHT build has mean, P95 and maximum times of
11.36, 35.83 and 57.43 ms. Five keyframes average 34.03 ms; 115 P-frames
average 10.38 ms. Moving the kernel uses 1,498 additional IRAM bytes and leaves
the runtime heap unchanged at 306,440 bytes free, with a 172,032-byte largest
block.

## Normal-player results

All averages are in microseconds.  Each synchronized run covers frames 1-125
and discards the first five records.  `Present` is paced to 15 fps, so it is a
health check rather than decoder work.

| Packet pool / flash | Stable runs | SD | Decode | Render | Work | Present | Work change | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `9 x 7,680`, DIO 40 MHz | 3 | 5,092.1 | 23,342.8 | 27,796.9 | 56,231.8 | 61,452.5 | baseline | baseline |
| `3 x 23,040`, DIO 40 MHz | 2 | 5,086.1 | 23,342.2 | 27,794.8 | 56,223.1 | 61,460.2 | -0.02% | rejected |
| `9 x 7,680`, DIO 80 MHz | 3 | 5,053.6 | 21,675.6 | 27,455.3 | 54,184.3 | 61,495.8 | -3.64% | accepted |
| `9 x 7,680`, QIO 80 MHz | 2 after one warm-up run | 5,022.7 | 20,942.0 | 27,421.4 | 53,386.1 | 61,529.2 | -5.06% | accepted |

The larger packet banks changed SD time by only -0.12% and decoder time by
less than 0.01%.  They were reverted because the smaller allocations are safer
under heap fragmentation.

The installed 4 MiB flash reports JEDEC ID `5e 40 16`, corresponding to a
Zbit ZB25VQ32B-family part.  QIO 80 MHz completed 1,194 sequential normal-player
records over 80 seconds with zero sequence gaps and zero malformed UART lines.
The separate decoder-only test additionally verifies reconstructed pixels.

The 2026-07-23 audio-enabled regression collected frames 1 through 900 with no
decode-sequence gaps. Average SD, decode, render and total work times were
4,035.3, 17,673.2, 29,569.2 and 51,277.7 microseconds. Of those frames, 103
exceeded that file's 66,667-microsecond work budget. This historical run used
the frame-preserving audio-loop mode. At frame 870 the counters reported
927,232 played samples, zero rebuffers, zero missing samples, zero silence
chunks and zero audio-loop events. The current player instead selects the
real-time mode: the same late frame is decoded for prediction but its display
transfer is omitted so playback continues at the `fps_num/fps_den` stored in
the file.

A separate frame-rate regression used the same player binary with two HLV
headers. Over 200 decoded frames, the `24/1` test measured 23.985 frames/s and
the normal `15/1` file measured 14.984 frames/s. Both runs reported zero
decode-sequence gaps, rebuffers, missing audio samples and silence chunks. The
24-fps test required no display omissions; the 15-fps run likewise required
none despite isolated frames whose work exceeded one frame interval.
