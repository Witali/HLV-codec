# Bare-metal-style multi-codec player for ESP32-2432S028

This is a separate, deliberately stripped firmware profile for the two-USB
CYD board. It reuses the primary C99 ESP-IDF player, codecs and hardware
drivers instead of copying them.

"Bare-metal-style" is precise here: ESP-IDF's SPI DMA, LCD, SDSPI, FatFs and
timer drivers still require FreeRTOS. The stripped scheduler is treated as a
microkernel with two pinned application tasks and no memory-isolated process
model. A genuinely RTOS-free image would also require replacing those drivers
and is not ABI-compatible with the normal ESP-IDF application runtime.

Compared with the full C99 player this profile:

- pins the `app_main` control/SD/render superloop to CPU0;
- creates one ordered decoder worker pinned to CPU1;
- polls BOOT from the control superloop instead of creating a button task;
- disables audio and its reader task;
- disables the UART upload/read/control service and frame-timing output;
- excludes BPV v7, whose bounded streaming path requires a concurrent refill
  task; BPV v1-v6 and the other player codecs remain available;
- disables optional FreeRTOS facilities while retaining only the two core idle
  tasks required by the scheduler.

All project and codec translation units are compiled with `-O3`. SD-over-SPI
command CRC7 and data CRC16 checking are mandatory: the mount path explicitly
clears ESP-IDF's `SDMMC_HOST_FLAG_SPI_IGNORE_DATA_CRC`, so initialization must
successfully enable CRC with CMD59 before playback starts.

For physical acceptance the firmware emits short `HLVBARE` records when a
card mounts with CRC enabled, when a file opens, and after frames 1 and 300.
They are outside the hot decode/render measurements and produce no continuous
UART traffic.

This minimizes scheduler activity and application stack allocations while
retaining decode/render overlap. One-time `HLVBARE` records report both actual
task cores so physical acceptance verifies affinity rather than inferring it
from configuration.

## Physical acceptance

Tested on 2026-08-02 on an ESP32-D0WD-V3 revision 3.1 through COM8 at 460800
baud. The image from `build/` was written as three regions; esptool verified
the hash of the bootloader, partition table and application before a final
hard reset. No files were uploaded to or generated on the SD card for this
run, so there were no test-only files to remove.

The application reported two cores, control on CPU0 and the decoder on CPU1.
The card mounted at 40 MHz with CRC enabled, and the selected MJPEG AVI opened
as 352x288 at 30 fps and reached frame 300. At that frame the internal 8-bit
heap reported 51,388 bytes free, a 47,800-byte historical minimum and a
25,600-byte largest free block. Stack high-water marks were 5,120 bytes for
the control task and 1,252 bytes for the decoder task.

The linker map uses 102,447 bytes of IRAM (78.16%) and 43,372 bytes of static
DRAM (24.00%). The application image is 650,672 bytes; these static figures do
not include heap allocations made while a codec is open, which is why the
runtime heap reading above is the useful operating headroom.

The project shares the pinned ESP-IDF 5.5.5 installation and downloaded
component from `../esp32_2432s028_hlv_player_idf_c`; generated files stay under
this project's `build/` directory.

## Build, flash and monitor

```powershell
.\build.ps1
.\flash.ps1 -Port COM8
.\monitor.ps1 -Port COM8
```

The SD card layout is unchanged. Put media below `/HLV` and select a basename
in `/HLV/play.txt`. Short BOOT presses browse files; a long press confirms the
selection.
