# BPV1 decoder optimization TODO

This list covers BPV1 decoding and presentation on the
ESP32-2432S028 player. Optimize against measurements from the physical board,
not only host execution time.

## Current physical-board result

Test stream selected by `/sdcard/HLV/play.txt`:
`VID_20260522_181611_v4.bpv1`, 320x240, 30 fps. The frame-time budget is
33,333 us. The display uses 80 MHz SPI and the SD card uses 40 MHz SPI.

A 900-frame run of the retained dual-core pipeline completed with:

- 30.014 observed fps, no frame-number gaps and no skipped presentations;
- SD read: 17,313 us average, 19,283 us p95, 22,624 us maximum;
- BPV decode: 5,223 us average, 5,357 us p95, 5,395 us maximum;
- RGB565 conversion and display: 16,899 us average, 17,381 us p95;
- no audio rebuffer, underrun, silence insertion or DMA-loop events.

The individual read, decode and render measurements sum to more than one
frame period, but they are no longer serial. CPU1 decodes a packet and
prefetches the next packet while CPU0 presents the preceding decoded frame.

The experimental v7 direct-pixel path was separately measured with a
300-frame 320x240 30 fps stream containing 389,840 pixel-motion blocks. It
completed all frames without gaps at 29.716 observed fps. Sequential refill
input plus decoding averaged 20,593 us, the interleaved SPI/DMA callbacks
averaged 6,430 us, and total work averaged 27,023 us. The preceding
nearest-color v7 implementation reached only 23.083 fps and averaged 58,272
us of work. v7 now uses a 4 KiB refill buffer, a paged previous RGB565 frame
and two alternating eight-row display buffers; v6 retains its faster
dual-core compact-frame pipeline.

Caching all 1024 active palette colors as RGB565 once per keyframe reduced
median sequential refill/decode time over three identical physical-board runs
from 20,593 to 19,014 us (7.7%). Median total work fell from 27,023 to
26,228 us (2.9%), and frames exceeding the 33,333-us period fell from 51 to
36 out of 300. All three runs decoded 300/300 frames without gaps or display
skips. Display-callback time rose because the faster CPU path reaches the two
fixed-rate SPI/DMA buffers earlier and waits for an outstanding transfer;
that wait shifts between timing categories rather than representing extra
RGB565 conversion.

The retained per-frame profiler separates the v7 `Decode` category without
putting a timer around every block. On the same physical 300-frame stream,
18,936 us average decode time consists of 6,695 us compressed input (35.4%),
11,773 us block parsing/reconstruction (62.2%), and 468 us progressive
RGB565-reference commit (2.5%). An average frame reads 17,303 bytes through
6.45 input calls. The reconstructed-frame hash remains
`93682c11462696bc`.

Before the pipeline change, the same file ran at 15.881 fps with the active
SD setting accidentally left at 10 MHz. Moving BPV prefetch to CPU1 raised
that to 19.368 fps at the same SD clock. Restoring the intended 40 MHz SD
clock then reached the native 30 fps.

## Completed

- [x] Replace the linear 256-entry dictionary scans with hash-indexed
      dictionaries while preserving bitstream and eviction order.
- [x] Replace the bit-at-a-time 3-bit mode reader with a two-byte window.
- [x] Replace hot 9-byte `memcpy` calls with an inlined fixed-size copy.
- [x] Verify the complete Big Buck Bunny decode against hash
      `89003dd0f74192ff`.
- [x] Measure the optimized path on the physical ESP32. Decode time fell from
      50,537 us to 2,113 us on average, with no missed 24 fps deadlines.
- [x] Render four adjacent pixels from one converted BPV block palette instead
      of converting every pixel independently.
- [x] Add bounded one-packet BPV read-ahead without adding a second payload
      buffer. CPU1 reads the next packet only after it has finished consuming
      the current packet.
- [x] Overlap CPU1 decode/read-ahead with CPU0 RGB565 conversion and display.
- [x] Restore both the tracked default and active test configuration to
      40 MHz SD SPI. Use lower clocks only for SD reliability diagnostics.
- [x] Allocate the BPV decoder, packet storage, read-ahead, worker queues and
      worker stack while opening the video. The close path now stops the
      worker and deletes its queues before freeing codec and file buffers.
      No BPV-frame path performs heap allocation.
- [x] Validate 900 consecutive 320x240 frames on the physical board at native
      30 fps with no video gaps or audio underruns.
- [x] Force a close/reopen through the UART CRC command and validate another
      300 frames at 29.993 fps with no gaps or audio errors.

## Remaining work

### 1. Separate RGB conversion from display waiting

- [x] Measure CPU conversion separately from SPI/DMA buffer waiting.
- [x] A/B test a 64x16 RGB565 table. BPV v4-v6 rebuild the 2 KiB table only
      when a keyframe replaces the active palette.
- [x] Preserve the portable RGB24 path used by the Windows player and tests.

Keep the change only if the full decoded-frame hash is unchanged and the
physical-board render or CPU-conversion measurement improves.

The retained table reduces the isolated 320x240 QEMU RGB565 conversion by
18.31%, from 314,621 to 256,999 guest cycles, with unchanged output hash
`d761ba3e770d64eb`. On the physical player, where the render timer also includes
display-buffer and SPI/DMA waiting, the median of three 300-frame runs improves
from 16,827.5 to 16,665.4 us (0.96%). Total work improves by 0.40%; decode time
is unchanged. All 900 frames play without gaps or audio errors. The cost is
2,056 DRAM bytes and 388 Flash-code bytes.

### 2. Validate repeated allocation lifetimes

- [ ] Record free heap and largest free block before open, after open and
      after close.
- [ ] Run repeated EOF loops and multiple UART-driven close/reopen cycles.
- [ ] Confirm that free heap and the largest block return to the same values
      after every close.

### 3. Optional decoder-core micro-optimizations

- [ ] Maintain `block_x` and `block_y` counters in the decode loop instead of
      using division and remainder for motion blocks.
- [ ] Store the hash alongside each dictionary entry so eviction does not
      hash the old value again.
- [ ] Specialize or unroll hashing and equality checks for the fixed 4-byte
      pattern and 9-byte block records.
- [ ] Add per-mode counters and cycle measurements for v6 `SKIP`, `MOTION`,
      block-dictionary and unified raw modes.
- [ ] Record the operation-count estimate and RAM increase for each tested
      change.

Retain a micro-optimization only when the complete decode remains bit-exact
and physical-board average or tail decode time improves repeatably. These
changes are lower priority now that 320x240 native 30 fps is sustained.

### 4. Optional aligned in-memory records

- [ ] A/B test expanding the internal 9-byte block record to an aligned
      12-byte representation while leaving the BPV file format unchanged.
- [ ] Measure the extra frame and dictionary RAM before testing performance.
- [ ] Reject the experiment if the memory increase reduces player stability
      or interferes with other codec paths.

## Tested and rejected

- A dedicated single-file BPV audio/video demux experiment did not reduce the
  measured SD time at 10 MHz and failed audio initialization. It was reverted;
  the working independent audio reader remains.
- Increasing the stdio video read-ahead buffer from 16 KiB to 48 KiB changed
  observed playback from 15.881 to 15.789 fps at 10 MHz. It was reverted.
- A two-packet ping-pong experiment let CPU1 fill the next packet while CPU0
  decoded and rendered the current packet. Two 48,606-byte packet buffers did
  not fit with the retained 16 KiB stdio read-ahead; unbuffered and 512-byte
  read-ahead variants were much slower. An 8 KiB variant completed 900 frames
  at 30.014 fps without gaps or audio errors, but that is the same native-rate
  result as the retained one-packet pipeline. SD time was unchanged
  (17,322 versus 17,313 us average), while decode time regressed from 5,223 to
  6,156 us average and from 5,357 to 6,643 us p95 because decode moved onto
  the display/audio core. The extra 48.6 KiB packet allocation and halved SD
  cache therefore had no measured benefit, so the experiment was reverted.
  A UART CRC close/reopen followed by another clean 300-frame run confirmed
  that the experimental buffers had no lifetime leak.

## Not currently justified

- A second maximum-size BPV payload buffer costs about 48 KiB at 320x240.
  The measured ping-pong experiment above did not improve playback, while the
  retained pipeline reuses the decoder-owned packet buffer after decode and
  already sustains 30 fps.
- Larger memory banks do not remove the main costs. The decoder already fits
  in RAM, and the display transfer is limited primarily by SPI bandwidth.
- Enabling byte-addressable IRAM is not a default optimization: byte accesses
  are exception-emulated on this ESP32 profile and can be slower than DRAM.
- Changing the BPV file format is outside this optimization work; all retained
  changes preserve the existing bitstream.
