# ESP32 seek optimization TODO

This list tracks removal of avoidable file repositioning from the ESP32
player hot paths. Preserve seeks used for file opening, container probing,
explicit user seeking, loop restart and recovery after an SD-card error.

Treat every item as an independent A/B experiment. Retain it only when C99
and C++ firmware builds pass, decoded output remains unchanged in host/QEMU
checks, and the physical ESP32 shows no speed, frame-order, audio or memory
regression on a heavy representative clip. Commit each retained item
separately and remove rejected candidate code.

## Source audit

| Decoder/path | Avoidable seek candidate |
| --- | --- |
| H.263 / MPEG-4 Simple Profile AVI | Up to approximately four seeks per frame across the separate video and audio readers: unconditional positioning before packet lookup plus forward skipping over packets belonging to the other stream in `h263_3gp.c`. |
| MJPEG / DivX3 AVI | Approximately three seeks per frame across both readers: video packet completion always seeks to `next_offset`, while each independent reader also seeks over packets belonging to the other stream in `avi_demux.c`. |
| DivX3 catch-up | The player reads one compressed prefix byte to classify an I/P frame, seeks backward, then supplies the same byte to the decoder again. |
| HLV catch-up | The keyframe-search path parses a frame header, seeks backward to the header, then the normal decoder parses it again. |
| HLV / BPV audio | The independent audio reader performs a forward seek across the compressed video portion of every frame. |
| MPEG-1 / MP2 | No seek is present in the ordinary decode hot path. File-size probing and rewind after stream probing or loop restart are intentional and must remain. |

## Execution order

### 1. Cursor-aware AVI packet completion (retained)

- [x] Make `avi_demux_finish_packet()` a no-op when `ftell()` already equals
      `packet->next_offset`.
- [x] When the complete odd-sized payload was consumed, read the one-byte RIFF
      padding sequentially instead of seeking over it.
- [x] Retain absolute seek as a fallback when a decoder did not consume the
      complete payload or the cursor is otherwise different.
- [x] Count no-op completions, padding reads and fallback seeks in the A/B
      test build.
- [x] Test even and odd packets, partial payload consumption and truncated
      padding.
- [x] Regress MJPEG and DivX3 packet/frame checksums in QEMU.
- [x] Test heavy MJPEG and DivX3 clips on the physical ESP32.

Result, 2026-08-04: retained. The profiled host test exercises and counts all
three completion paths and passes the complete 20-file AVI demux corpus. All
seven generated video formats retain their expected frame and audio checksums;
the C99 and C++ firmware builds and DivX3 QEMU regressions pass.

On the physical ESP32, a 600-frame 320x240 24 fps heavy MJPEG A/B measured
25.232 ms average work and 33.541 ms p95 with unconditional seek, versus
25.243 ms and 33.556 ms with cursor-aware completion. Both runs had one frame
over budget, no display skips, no audio underruns and no silence insertion.
The 0.04% average difference is measurement noise: this removes the redundant
filesystem operation but does not claim a measurable decode-speed gain. The
optimized build also played 600 frames of the 320x240 heavy DivX3 clip with no
frame-number gaps, display skips, audio rebuffers or underruns. The pre-existing
MJPEG selection was restored in `play.txt`; no test asset was added to the SD
card.

### 2. Sequential H.263 / MPEG-4SP AVI positioning

- [ ] Make `nextAviPayload()` preserve the current cursor when it already
      equals the tracked next packet offset.
- [ ] Consume a one-byte RIFF alignment gap sequentially.
- [ ] Retain absolute seek for genuine gaps, recovery and non-AVI sample-table
      positioning.
- [ ] Compare H.263 and MPEG-4SP frame checksums with the existing path.
- [ ] Test large packets that exceed the compressed refill buffer.
- [ ] Run heavy H.263 and MPEG-4SP physical-board A/B tests and reject any
      decode-speed regression.

### 3. DivX3 prefix replay

- [ ] Replace the one-byte read/backward-seek probe with a stream context that
      returns the saved prefix byte before continuing from the `FILE`.
- [ ] Apply the same behavior to sequential and dual-core decode paths in C99
      and C++.
- [ ] Preserve I/P classification, compressed skip behavior and exact frame
      checksums.
- [ ] Regress normal playback and large catch-up-to-keyframe sequences on the
      physical ESP32.

### 4. HLV parsed-header decode entry

- [ ] Add a bounded streaming decoder entry that accepts an already parsed
      HLV packet header and expected CRC.
- [ ] Reuse that entry when catch-up encounters a keyframe instead of seeking
      backward and parsing the header twice.
- [ ] Keep forward seek when intentionally dropping a predictive frame unless
      the later common AV-reader work makes it unnecessary.
- [ ] Preserve CRC verification, all frame types and exact decoded checksums.
- [ ] Test long predictive sequences and catch-up on the physical ESP32.

### 5. Single AVI AV demuxer

- [ ] Replace independent video/audio scans with one sequential `movi` walker.
- [ ] Dispatch bounded compressed video bytes to the selected decoder and
      audio payloads to the selected audio decoder/queue.
- [ ] Keep compressed-input capacity independent of maximum packet size and
      never copy a complete packet between tasks.
- [ ] Preserve audio preroll, sample rate, sample count, presentation order,
      explicit seeking, loop restart and SD error reporting.
- [ ] Test MJPEG, DivX3, baseline H.263 and MPEG-4SP AVI, including a valid
      packet larger than every refill/ring buffer and compare checksums with
      contiguous input.
- [ ] Run physical heavy-clip A/B tests for every AVI video codec.

## Measurements to record

- compressed packets, bytes and actual `fseek()` calls per path;
- no-op cursor matches, sequential padding bytes and fallback seeks;
- SD/read wait, decode, render and complete-work average, p50, p95 and maximum;
- displayed/skipped frames, frame-number gaps and observed frame rate;
- audio queued/played samples, rebuffers, underruns and inserted silence;
- free heap, minimum heap and largest DMA-capable block;
- RGB565/frame checksums and decoded audio sample checksums where available.
