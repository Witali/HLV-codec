# ESP_NEW_JPEG 1.0.2 decoder reverse engineering

This directory contains annotated Ghidra analysis output for the ESP32 decoder
part of Espressif's prebuilt `esp_new_jpeg` 1.0.2 library. It does not contain
the encoder objects.

The input archive is:

```text
firmware/esp32_2432s028_hlv_player_idf_c/managed_components/
    espressif__esp_new_jpeg/lib/esp32/libesp_new_jpeg.a
SHA-256: 42F226866A13580988D9EED2F907DA92955EC40F107F47F64460C72310C65D6F
```

Espressif publishes the component package and its headers in
[esp-adf-libs](https://github.com/espressif/esp-adf-libs/tree/master/esp_new_jpeg),
but the decoder implementation in this version is distributed as an Xtensa
static library. The derived files here are covered by
[LICENSE.ESPRESSIF](LICENSE.ESPRESSIF).

## Saved output

The nine files under [assembly](assembly) are Xtensa little-endian
disassemblies. The matching files under [pseudo_c](pseudo_c) are Ghidra
pseudo-C. Together they cover 116 retained decoder and support functions:

| Object | Functions | Main responsibility |
| --- | ---: | --- |
| `esp_jpeg_dec.c.obj` | 6 | Public open, header, process, sizing and close API |
| `jpeg_dec_parse_header.c.obj` | 4 | SOI, DQT, SOF0 and SOS marker parsing |
| `jpeg_dec_huff.c.obj` | 2 | Huffman table construction and entropy decoding |
| `jpeg_dec_idct.S.obj` | 12 | Hand-written integer IDCT variants |
| `jpeg_dec_color.c.obj` | 16 | Full-size grayscale/YUV color conversion |
| `jpeg_dec_color_scale.c.obj` | 16 | Scaled and rotated color conversion |
| `jpeg_dec_process.c.obj` | 53 | MCU loops, edge paths and output dispatch |
| `esp_jpeg_memory.c.obj` | 6 | Capability-aware heap allocation wrappers |
| `esp_jpeg_version.c.obj` | 1 | Version-string query; not in the decode path |

Every function starts with a `Purpose` comment. In particular, the comments
mark the current Player hot path:

- `jpeg_dec_huffman`
- `jpeg_dec_proc_yuv420_0_block`
- `idct_block_8_8`
- `yuv420_to_rgb565le`
- `jpeg_dec_process_0`

See [OPTIMIZATION_ANALYSIS.md](OPTIMIZATION_ANALYSIS.md) for the findings and
the proposed A/B sequence.

## Method

The archive members were extracted with the pinned ESP32 Xtensa `ar` from the
repository's ESP-IDF environment and imported as relocatable ELF objects into
Ghidra 12.1.2 with:

```text
Processor: Xtensa:LE:32:default
Compiler specification: default
```

DWARF identifies the original compiler as GNU C23 15.2.0 and retains useful
source types and names. It also records `-O2`, `-mlongcalls`, DWARF 4 and source
paths such as `esp_new_jpeg/src/jpeg_dec_huff.c`.

The repository script
[ExportAnnotatedDecompilation.java](../../../scripts/ghidra/ExportAnnotatedDecompilation.java)
produces both forms. A representative headless invocation is:

```powershell
& C:\Work\HLV-codec\local_tools\ghidra\ghidra_12.1.2_PUBLIC\support\analyzeHeadless.bat `
    C:\Work\HLV-codec\out\esp_new_jpeg_1.0.2_xtensa_analysis\ghidra_project `
    esp_new_jpeg `
    -process jpeg_dec_huff.c.obj -noanalysis `
    -scriptPath .\scripts\ghidra `
    -postScript ExportAnnotatedDecompilation.java `
        .\docs\reverse_engineering\esp_new_jpeg_1.0.2\assembly\jpeg_dec_huff.S `
        .\docs\reverse_engineering\esp_new_jpeg_1.0.2\pseudo_c\jpeg_dec_huff.c
```

Downloaded Ghidra installations and projects remain untracked under
`local_tools/` and `out/`.

## Accuracy limitations

- The assembly contains real decoded instructions and bytes, but section
  addresses belong to relocatable objects and are not final firmware
  addresses.
- Ghidra 12.1.2 reports some unhandled `R_XTENSA_SLOT0_OP` relocations for
  these objects. Calls and literals represented by those relocations can
  therefore appear as `_DAT_...` globals in pseudo-C.
- Pseudo-C is a reconstruction, not Espressif's original source. Optimizer
  transformations, macro boundaries and some local names cannot be recovered.
- The IDCT object has minimal DWARF. Its signatures are inferred and less
  reliable than the Huffman and process signatures.
- Any replacement must be checked against final linked disassembly and
  frame-exact decoder tests before use.
