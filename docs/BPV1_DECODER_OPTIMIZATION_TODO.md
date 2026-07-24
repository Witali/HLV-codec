# BPV1 decoder optimization TODO

This list covers BPV1 decoding and presentation on the
ESP32-2432S028 player. Optimize against measurements from the physical board,
not only host execution time.

## Current baseline

Test stream: `BigBuckBunny.bpv1`, 320x180, 24 fps, 14,315 frames.
The frame-time budget is 41,667 us.

The retained decoder completed a 549-frame physical-board run with:

- no frame-number gaps and no frames exceeding the time budget;
- SD read: 2,797 us average, 15,570 us maximum;
- BPV decode: 2,113 us average, 9,081 us maximum;
- RGB565 conversion and display: 13,358 us average;
- complete frame work: 18,268 us average, 36,825 us maximum.

The 80 MHz SPI transfer of a 320x180 RGB565 frame has a theoretical lower
bound of 11,520 us. Therefore only about 1.8 ms of the measured rendering time
is not the physical display transfer.

## Completed

- [x] Replace the linear 256-entry dictionary scans with hash-indexed
      dictionaries while preserving bitstream and eviction order.
- [x] Replace the bit-at-a-time 3-bit mode reader with a two-byte window.
- [x] Replace hot 9-byte `memcpy` calls with an inlined fixed-size copy.
- [x] Verify the complete Big Buck Bunny decode against hash
      `89003dd0f74192ff`.
- [x] Measure the optimized path on the physical ESP32. Decode time fell from
      50,537 us to 2,113 us on average, with no missed 24 fps deadlines.

## Remaining work

### 1. Render blocks directly to RGB565

- [ ] Build a 64x16 RGB565 palette table once when the BPV header is opened.
      The table requires 2 KiB.
- [ ] Render all four pixels of a BPV block row together instead of calling
      `pixel_rgb()` separately for every pixel.
- [ ] Preserve the portable RGB24 path used by the Windows player and tests.
- [ ] Measure conversion time separately from SPI waiting so that display
      transfer time is not mistaken for decoder work.

Keep the change only if the full decoded-frame hash is unchanged and the
physical-board render or CPU-conversion measurement improves.

### 2. Hide SD-card latency

- [ ] Add one-packet read-ahead for BPV.
- [ ] Overlap reading the next packet with conversion and SPI submission of
      the current frame.
- [ ] Keep packet memory bounded by the maximum frame size from the BPV
      header.
- [ ] Measure average, p95, p99 and maximum SD/read-plus-decode time, frame
      gaps and missed presentation deadlines.

This is primarily a playback-pipeline optimization, not a reduction in the
BPV decoder's operation count. It targets the observed SD spikes of up to
15.6 ms.

### 3. Test remaining decoder-core micro-optimizations

- [ ] Maintain `block_x` and `block_y` counters in the decode loop instead of
      using division and remainder for motion blocks.
- [ ] Store the hash alongside each dictionary entry so eviction does not
      hash the old value again.
- [ ] Specialize or unroll hashing and equality checks for the fixed 4-byte
      pattern and 9-byte block records.
- [ ] Add per-mode counters and cycle measurements for `SKIP`, `MOTION`,
      block-dictionary, pattern-dictionary and raw modes.
- [ ] Record the operation-count estimate and RAM increase for each tested
      change.

Retain a micro-optimization only when the complete decode remains bit-exact
and physical-board average or tail decode time improves repeatably.

### 4. Optional aligned in-memory records

- [ ] A/B test expanding the internal 9-byte block record to an aligned
      12-byte representation while leaving the BPV file format unchanged.
- [ ] Measure the extra frame and dictionary RAM before testing performance.
- [ ] Reject the experiment if the memory increase reduces player stability
      or interferes with other codec paths.

## Not currently justified

- A BPV-specific dual-core pipeline is not required for 320x180 at 24 fps:
  the measured maximum complete-frame work is 36.8 ms against a 41.7 ms
  budget, with no missed frames.
- Larger memory banks do not remove the main costs. The decoder already fits
  in RAM, and the display transfer is limited primarily by SPI bandwidth.
- Enabling byte-addressable IRAM is not a default optimization: byte accesses
  are exception-emulated on this ESP32 profile and can be slower than DRAM.

