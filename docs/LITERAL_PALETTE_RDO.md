# HLV v13 literal, palette and decode-complexity RDO

HLV stream version 13 bounds the cost of difficult intra material by adding a
byte-copy macroblock representation and by making decoder work part of encoder
mode selection. Older v1-v12 streams remain bitstream-compatible.

## Byte-aligned macroblocks

Every v13 macroblock begins at the next byte boundary and starts with a fixed
four-bit mode number. Padding bits must be zero. The fixed code makes mode
parsing independent of frame type and leaves room for future low-complexity
representations.

Simple image regions still use `FILL`, `GRADIENT` and `PALETTE`. Transform
`INTRA` and motion-compensated modes remain available when their size and
distortion justify the additional decoder work.

## Palette variants

The v13 palette payload starts with a two-bit size code:

| Code | Colours | Index bits |
| ---: | ---: | ---: |
| `00` | 2 | 1 |
| `01` | 4 | 2 |
| `10` | 8 | 3 |
| `11` | reserved | — |

Each entry contains one 8-bit Y/U/V triplet. Luma has one index per 16x16
sample and chroma has one shared index per 8x8 U/V sample. The encoder retains
the strict maximum-error check, so a palette candidate is rejected rather than
merging a rare colour into a visibly distant cluster.

## Literal macroblock

`LITERAL` stores the reconstructed macroblock in the compact ESP32 layout:

- sixteen luma rows, each containing sixteen 6-bit samples in 12 bytes;
- eight U rows, each containing eight 5-bit samples in 5 bytes;
- eight V rows with the same 5-byte layout.

Samples are packed least-significant-bit first inside each byte-aligned row.
The four-bit mode is followed by four zero padding bits, then exactly 272
payload bytes. One literal macroblock therefore occupies 273 bytes including
its mode byte.

The portable decoder expands codes onto an MSB-aligned 8-bit grid (`Y << 2`,
`U/V << 3`). The compact ESP32 decoder instead copies the 12-byte and 5-byte
rows directly into its predictive frame planes. It performs no prediction,
coefficient VLC, residual mask parsing or inverse WHT for the block.

A completely literal 320x180 frame is padded to 320x192 and contains 240
macroblocks:

```text
240 × 272 = 65,280 literal data bytes
```

The ESP32 player therefore uses nine 7,680-byte packet blocks, giving 69,120
bytes for video syntax and the packet's audio tail.

## Decoder-complexity term

The encoder evaluates candidates using:

```text
score = distortion
      + lambda × (payload_bits + cycle_weight × estimated_decode_cycles)
```

`cycle_weight` is expressed as equivalent payload bits per estimated decoder
cycle. Its default is `0`, preserving rate/distortion-only selection. Higher
values are an explicit speed/quality trade-off.
The command-line encoder exposes it as:

```text
--decode-cycle-weight 0.05
```

The estimate is deliberately architecture-independent. It charges payload
traversal, predictor samples and a residual surcharge for coefficient parsing
and inverse transforms. Literal blocks receive a row-copy estimate, while
palette blocks receive indexed-sample lookup cost. It is a mode-selection
heuristic, not a prediction of wall-clock ESP32 cycles.

After an encode, `hlvenc` prints the accumulated estimate as cycles per frame
alongside its mode statistics. The same cumulative value is available as
`HLV1Stats.estimated_decode_cycles`.

## Initial measurement

A 30-frame 320x180 sample was encoded at fixed qsteps Y=24 and UV=32. Both
files used v13 and GOP 15; only the cycle weight changed.

| RDO cycle weight | Payload | Average PSNR | `hlvbenchdec` conservative cycles/frame | Compact host simulator |
| ---: | ---: | ---: | ---: | ---: |
| 0.00 (default) | 0.075 MiB | 53.86 dB | 496,155 | 201.06 µs/frame |
| 0.05 | 0.083 MiB | 50.72 dB | 374,323 | 165.43 µs/frame |
| 0.20 | 0.086 MiB | 43.97 dB | 328,260 | 143.68 µs/frame |

On this short sample, `0.05` trades roughly 11% more payload and 3.14 dB for a
25% lower conservative work estimate. The aggressive `0.20` setting reduces
the estimate by 34%, but its larger quality cost illustrates why all nonzero
weights are opt-in. Mode choices affect later predictive references, so quality
and complexity do not have to vary monotonically on every clip.
The encoder's candidate-level RDO model reports 453,702 estimated cycles/frame
for the default case and 319,218 for `0.05`; its scale differs from the
independent benchmark model.
Host timings are not cycle-accurate ESP32 results; physical-board timing
remains the acceptance test.
