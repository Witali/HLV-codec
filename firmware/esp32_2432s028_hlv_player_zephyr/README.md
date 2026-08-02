# Preserved ESP32-2432S028 Zephyr SMP experiment

This directory preserves the Zephyr alternative to the full ESP-IDF player.
It is a scheduler and storage bring-up experiment, not an accepted player
firmware:

- Zephyr SMP is configured for both ESP32 cores at 240 MHz;
- the control/I/O thread is pinned to CPU0;
- the decoder worker is pinned to CPU1;
- kernel semaphore IPC makes cross-core scheduling visible;
- SPI3 microSD uses SCK 18, MOSI 23, MISO 19 and CS 5 at up to 40 MHz;
- the SD card is put into CRC mode with CMD59 and every data block is protected
  with CRC16 by Zephyr's SDHC SPI driver;
- all application and kernel translation units receive `-O3` through
  `CONFIG_COMPILER_OPT`;
- Wi-Fi, Bluetooth, audio, display and UART control services are disabled in
  this first profile.

Zephyr threads are schedulable kernel tasks with affinity, but they share one
address space; they are not memory-isolated processes.

The build wrapper expects Python 3.12.10, Zephyr 4.4.0 and Zephyr SDK 1.0.1
under `local_tools/zephyr-workspace`. It uses ESP32 sysbuild so MCUboot and the
application are kept as separate images:

```powershell
.\build.ps1 -Pristine
```

The current application linker reservation is 32,768 bytes of DRAM and 49,920
bytes of IRAM (80.75 KiB total). This does not include a real decoder,
framebuffer or audio buffers.

## Physical blocker

On 2026-08-02 a stock unicore Zephyr 4.4.0 image booted on the project's
ESP32-D0WD-V3, but both the stock SMP Pi sample and this smaller SMP image
stopped before the Zephyr kernel banner. Simple boot stopped after flash
initialization; MCUboot loaded the application's DRAM and IRAM segments but did
not transfer control successfully. Both flashed regions passed digest
verification. Consequently CPU affinity and SD CRC could not be accepted on
the physical board, and this profile is preserved for future SMP diagnosis
rather than presented as working firmware.
