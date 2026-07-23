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
- [x] Restore and flash the best verified normal-player configuration.

## Decoder-only results

Each row below was identical across all three physical runs.

| Variant | Mean cycles/frame | P50 | P95 | Max | Change | Hash | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| Flash bitreader, DIO 40 MHz | 3,764,549 | 2,734,080 | 11,195,450 | 16,609,914 | baseline | `be4876ff1c6b8461` | baseline |
| IRAM bitreader, DIO 40 MHz | 3,209,621 | 2,362,446 | 9,628,145 | 14,814,544 | -14.74% | `be4876ff1c6b8461` | accepted |
| IRAM bitreader, QIO 80 MHz | 2,776,047 | 1,995,995 | 8,673,122 | 14,010,337 | -13.51%; -26.25% cumulative | `be4876ff1c6b8461` | accepted |

At 240 MHz the final mean, P95 and maximum are 11.57, 36.14 and 58.38 ms.
Five keyframes average 34.80 ms; 115 P-frames average 10.56 ms.  The final
benchmark still leaves 306,440 heap bytes free, with a 172,032-byte largest
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
