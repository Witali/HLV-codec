---
name: flash-reset-esp32
description: Build, flash, reset, reboot, or deliberately enter the ROM bootloader on the HLV ESP32-2432S028 board through its CH340C USB-UART bridge. Use when the user asks to flash or upload ESP32 firmware, reset or reboot the board, enter BOOT/download mode, retry an esptool connection, or verify the board after flashing. Use only the repository-local pure ESP-IDF project and its pinned dependencies.
---

# Flash and reset the HLV ESP32 board

Work from the repository root. The firmware project is
`firmware/esp32_2432s028_hlv_player_idf`.

## Choose the operation

- For an ordinary application reset, run:

  ```powershell
  .\.agents\skills\flash-reset-esp32\scripts\reset_board.ps1 -Port COM8
  ```

- For a normal build and flash, run:

  ```powershell
  .\firmware\esp32_2432s028_hlv_player_idf\flash.ps1 -Port COM8
  ```

- Use `-SkipBuild` only when the user explicitly asks to flash the existing
  default `build` directory.
- Enter the ROM bootloader without flashing only after an explicit request:

  ```powershell
  .\.agents\skills\flash-reset-esp32\scripts\reset_board.ps1 -Port COM8 -EnterBootloader
  ```

Never enter the ROM bootloader for an ordinary reset, monitor, build, or status
request. An explicit flash request authorizes automatic bootloader entry as part
of the flash operation.

## Flash workflow

1. Use the port supplied by the user. If none was supplied, enumerate ports
   with `[System.IO.Ports.SerialPort]::GetPortNames()`. Identify the CH340 port;
   do not guess when several ports are present.
2. Ensure that no serial monitor or previous uploader owns the port.
3. Run the repository's `flash.ps1`. It uses the local `idf.ps1`,
   `esptool.cfg`, and `build/flash_args`, verifies the written hashes, and
   performs a hard reset after flashing.
4. Treat `Hash of data verified` for all regions followed by the final reset as
   the acceptance criterion.
5. If the connection is unstable, retry the already-built image at a conservative
   rate:

   ```powershell
   .\firmware\esp32_2432s028_hlv_player_idf\flash.ps1 -Port COM8 -Baud 115200 -SkipBuild
   ```

Do not ask for manual BOOT/RESET unless the automatic sequence actually fails
and the user chooses the manual fallback.

## Flash a named build directory

When the requested image is in a non-default build directory, run the local
wrapper from the firmware project:

```powershell
$env:ESPTOOL_OPEN_PORT_ATTEMPTS = "60"
.\idf.ps1 -EsptoolWorkingDirectory .\build-name -EsptoolArguments @(
    "--chip", "esp32",
    "--port", "COM8",
    "--baud", "460800",
    "--before", "default_reset",
    "--after", "hard_reset",
    "write_flash", "@flash_args"
)
```

Confirm that `build-name/flash_args` exists before running the command.

## Reset behavior

- Ordinary reset keeps DTR inactive, asserts RTS for 500 ms, then releases RTS.
- Explicit boot mode applies `D0/R1` for 500 ms, `D1/R0` for 50 ms, and then
  returns both lines inactive.
- Use `-WhatIf` when validating the reset script without touching the board.
- Read `docs/board/CH340C_AUTO_BOOT_MOD.md` only when diagnosing the hardware
  modification. Treat `firmware/esp32_2432s028_hlv_player_idf/esptool.cfg` as
  the authoritative automatic boot sequence. Do not improvise another sequence.

## Report the result

State the port, operation, build directory, baud rate, and whether verification
succeeded. Do not claim that a reset or flash succeeded unless its command
completed successfully.
