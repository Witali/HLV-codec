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

- [ ] Record a fresh decoder-only physical baseline for the current v13-capable
      decoder and the representative v12 stream.
- [ ] Measure selective IRAM placement for the bitreader refill, slow
      extraction and Exp-Golomb paths.
- [ ] Compare the current `9 x 7,680` packet pool with `3 x 23,040` blocks in
      normal SD playback.  This changes fragmentation and read spans, not total
      packet capacity.
- [ ] Compare 40 MHz and 80 MHz DIO flash execution on the physical board.
- [ ] If code-side experiments leave difficult frames above the desired
      budget, encode short v13 samples with decoder-cycle RDO weights 0.01,
      0.02 and 0.05 and measure the quality/size/decode trade-off.
- [ ] Restore and flash the best verified normal-player configuration.

## Results

| Variant | Runs, cycles/frame | Median | Change | Hash | Decision |
| --- | --- | ---: | ---: | --- | --- |
| Current decoder, DIO 40 MHz | pending | pending | baseline | pending | pending |

Normal-player packet-pool results will be recorded separately because SD and
display peripherals are intentionally absent from the decoder-only benchmark.
