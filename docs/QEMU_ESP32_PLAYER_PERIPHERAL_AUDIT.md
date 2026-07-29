# QEMU ESP32 player peripheral audit

Audit date: 2026-07-29.

This audit covers the hardware paths exercised by
`firmware/esp32_2432s028_hlv_player_idf_c` and the preserved C++ reference
firmware. The QEMU device sources are based on Espressif QEMU commit
`40edccac415693c5130f91c01d84176ae6008566`; repository patches add the board
devices that are not present in that upstream revision.

| Firmware path | QEMU model | Status for the player |
| --- | --- | --- |
| Two Xtensa LX6 cores, DPORT and cross-core interrupts | Espressif ESP32 SoC | Present; MTTCG must be selected with `-accel tcg,thread=multi` |
| SPI1 flash and ROM boot | Espressif SPI flash and ESP32 ROM | Present; ROM reports a harmless inability to enable QIO for the raw DIO image |
| Timer groups, FreeRTOS tick and `esp_timer` latch reads | `esp32.timg` and Xtensa cycle counter | Present; timer `UPDATE` correctly latches `LACT` before LO/HI reads |
| GPIO0 BOOT, GPIO5 SD CS, display CS/DC/backlight | ESP32 GPIO plus runtime input patch | Present for the pins used by the player |
| SPI2 display DMA and interrupt | `ssi.esp32.spi` plus DMA descriptor support | Present for the ESP-IDF transactions used by ST7789 |
| ST7789 320x240 RGB565 display | Repository ST7789 SSI device | Present; RGB565 DMA uses a batch callback rather than one QOM callback per byte |
| SPI3 SDSPI DMA, GPIO5 CS and SD protocol | `ssi.esp32.spi`, `ssi-sd`, `sd-card-spi` and block backend | Present; standard CMD18/CMD12 reads and CMD25 writes pass checksum tests |
| I2S0 TX linked DMA, interrupt matrix and built-in DAC | Repository I2S/DAC device | Present for `dac_continuous`; DMA deadlines remain on a continuous sample clock |
| Analog I2C APLL calibration registers | Repository analog-I2C bridge | Present for the calibration sequence used by ESP-IDF |
| UART0 at 460800 baud | Espressif ESP32 UART | Present |
| RTCIO/SENS | Unimplemented placeholder | Not used by normal playback; direct RTCIO features would require a model |
| Wi-Fi, Bluetooth, Ethernet, USB and SDMMC host | Mixed or unimplemented | Not used by this SDSPI player configuration |

## SDSPI slowdown and fix

ESP-IDF calls `poll_busy()` before every SD command while CS is deasserted.
The physical ESP32-2432S028 board has a pull-up on SD MISO, so the controller
samples `0xff` and declares the bus ready after two bytes.

The generic QEMU SSI bus previously returned zero when no peripheral was
selected. Consequently, `poll_busy()` exhausted its timeout and issued
roughly 1,350 to 1,530 one-byte SPI transactions before each useful sector
transfer. This was an emulated board electrical-state error, not a firmware
buffering problem.

The repository QEMU now gives SSI buses a configurable idle value. Only the
ESP32 SPI3 bus receives the board-specific `0xff` pull-up value; flash and
display buses retain the upstream zero default. Between steady-state SD
sector payloads the observed one-byte transaction count falls to six.

The final saved Windows runtime reads 65,536 bytes with CMD18 in 31,265 us,
or 2.10 MB/s, compared with approximately 0.05 MB/s before the fix. It also
passes the CMD25 write checksum:

```text
SDSPI_QEMU_MULTIBLOCK_READ,16,65536,31265,4e1b1d51
SDSPI_QEMU_MULTIBLOCK_WRITE,4096,edb6ddc5
SDSPI_QEMU_DONE,0
```

Adding another firmware SDSPI buffer would not address this failure. QEMU's
block backend and the host operating system already cache the image, while
the firmware uses DMA and sequential compressed-input refill buffers.

## Accuracy limits

The emulator is functional rather than cycle-accurate. Full CIF H.263
playback is substantially improved and no longer stalls after its first
frame, but native Windows MTTCG throughput still depends on the host and does
not establish a physical-board FPS result. Physical timing and quality claims
must continue to come from tests on the ESP32.
