# Standalone ESP32 UART SWEEP firmware

This is a deliberately small, C99-only ESP-IDF application for measuring the
onboard crystal-less CH340C at nominal 2 and 3 Mbaud. It is a separate project:
none of its candidate tables, binary probe protocol, or selection logic is
linked into the video player.

The firmware owns the sweep. For every distinct ESP32 APB-divider candidate it
tests both directions with deterministic CRC32-protected blocks, combines the
host TX result with its own RX result, and selects the candidate with the fewest
errors. A result is marked clean only when both directions have zero errors.
Changing the UART divider while the program is running is intentional here and
is limited to this calibration firmware.

The PC is still required as the other end of the measurement. An ESP32 internal
loopback would not include the CH340C, USB link, or Windows driver and therefore
could not calibrate the actual application connection.

Run the complete build, one-time flash, and automatic sweep from the repository
root with the Python environment pinned by the production C firmware:

```powershell
.\firmware\esp32_2432s028_hlv_player_idf_c\.tools\espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe `
    .\scripts\sweep_esp32_uart_baud.py --port COM8
```

Useful options:

```powershell
# Only 3 Mbaud, with a short smoke test.
...\python.exe .\scripts\sweep_esp32_uart_baud.py --port COM8 `
    --baud 3000000 --blocks 16 --payload 256

# Re-run a resident SWEEP image without building or flashing.
...\python.exe .\scripts\sweep_esp32_uart_baud.py --port COM8 `
    --skip-build --skip-flash
```

The control connection is the established nominal 1 Mbaud, 8N2 link (ESP32
divider rate 978593). Probe frames are at most 524 bytes, including their
header, and results are written to `.tmp/uart-baud-sweep.csv` by default.

After calibration, restore the player with:

```powershell
.\firmware\esp32_2432s028_hlv_player_idf_c\flash.ps1 -Port COM8
```

On the crystal-less CH340C board tested on 2026-08-01, every enumerated 2 and
3 Mbaud divider passed all eight 256-byte blocks from the PC to the ESP32, but
failed all eight blocks from the ESP32 to the PC. Consequently there was no
clean high-speed candidate; the nominal rate shown as `selected` is only the
closest-rate tie-breaker, not a recommendation for the player.
