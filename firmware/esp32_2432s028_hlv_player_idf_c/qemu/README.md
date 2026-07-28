# ESP32 SDSPI/ST7789 QEMU sources

Original repository: [Espressif QEMU](https://github.com/espressif/qemu)

- Upstream tag:
  [`esp-develop-9.2.2-20260417`](https://github.com/espressif/qemu/tree/esp-develop-9.2.2-20260417)
- Upstream commit:
  [`40edccac415693c5130f91c01d84176ae6008566`](https://github.com/espressif/qemu/commit/40edccac415693c5130f91c01d84176ae6008566)
- Device patch: `patches/0001-esp32-sdspi.patch`
- Runtime GPIO-input patch: `patches/0003-esp32-gpio-input.patch`
- Windows build fallback: `patches/0002-windows-symlink-fallback.patch`
- Full patched files: `modified_sources/`

The local ST7789 device is an SSI peripheral connected like the physical
ESP32-2432S028 board:

- SPI2
- CS on GPIO15
- command/data on GPIO2
- backlight on GPIO21
- 320x240 RGB565 framebuffer

The ST7789 window has a control strip below the 320x240 LCD:

- `RESET` and `BOOT` are momentary buttons. Their emulated electrical level
  follows mouse-down and mouse-up, like the physical board buttons. BOOT also
  drives the emulated GPIO0 input while the application is running.
- `HOLD` is an optional BOOT hold checkbox. It is off by default; when
  checked, GPIO0 remains asserted low after the mouse button is released.
- To enter the ROM UART downloader with a mouse, check `HOLD`, click `RESET`,
  then clear `HOLD` when it is no longer needed.

It implements the command subset used by the pinned ESP-IDF ST7789 driver,
including its RAMCTRL little-endian pixel mode. Raw SPI behavior was
cross-checked against the MIT-licensed
[Wokwi ST7789 custom-chip example](https://wokwi.com/projects/453755839909287937).
The implementation itself uses QEMU's native
[SSI device interface](https://www.qemu.org/docs/master/devel/ssi.html).

`modified_sources/` preserves complete copies of every upstream file changed
by the local patch, using the same paths as the original source tree:

```text
hw/audio/esp32_analog_i2c.c
hw/audio/esp32_i2s_dac.c
hw/audio/meson.build
hw/display/Kconfig
hw/display/esp_rgb.c
hw/display/meson.build
hw/display/st7789.c
hw/gpio/esp32_gpio.c
hw/sd/sd.c
hw/sd/ssi-sd.c
hw/ssi/esp32_spi.c
hw/xtensa/Kconfig
hw/xtensa/esp32.c
hw/xtensa/esp32_intc.c
include/hw/audio/esp32_analog_i2c.h
include/hw/audio/esp32_i2s_dac.h
include/hw/display/esp_rgb.h
include/hw/gpio/esp32_gpio.h
include/hw/ssi/esp32_spi.h
include/hw/xtensa/esp32.h
include/hw/xtensa/esp32_intc.h
```

The device patch remains the canonical installation method used by the WSL
and native Windows setup scripts. The second patch lets a MinGW build complete
without enabling Windows Developer Mode: when file symlink creation is denied,
existing build-tree files are copied and not-yet-built files are skipped. The
finished executable and required ESP32 ROM blobs are copied to a normal,
relocatable runtime directory.

The full device files are retained for inspection and recovery. These files
remain subject to their upstream copyright and license terms. See the
[QEMU license documentation](https://www.qemu.org/docs/master/about/license.html).

Run a flash image and an SPI SD-card image with a visible ST7789 window:

```powershell
.\run-qemu-sdspi.ps1 -FlashImage <flash.bin>
```

When `-SdImage` is omitted, both launchers use the Git LFS demo image
`qemu/hlv-big-buck-bunny-5min-h263-avi.img`. It contains the first five
minutes of Big Buck Bunny as baseline CIF H.263/AVI at 24 FPS with PCM S16LE
mono 8 kHz audio. Pass `-SdImage <sd.img>` to override it.

Use `-Headless` to keep the LCD active without opening an SDL window.
Automated tests can capture console zero through the QEMU monitor with
`screendump <path>.ppm`.

For a native Windows build and launch (no WSL runtime), use:

```powershell
..\setup-qemu-sdspi-windows.ps1
..\run-qemu-demo-windows.ps1
```

The planned Windows virtual-COM integration, including a direct null-modem
pair, driver-free CI backend and later RESET/BOOT control-line support, is
specified in
[`docs/QEMU_WINDOWS_VIRTUAL_COM_PLAN.md`](../../../docs/QEMU_WINDOWS_VIRTUAL_COM_PLAN.md).

The dedicated demo launcher builds a production flash image when it is
missing and opens the ST7789 window with the default demo card. Use
`-Rebuild` to rebuild the flash image or `-Headless` to suppress the window.
It uses DirectSound for the ESP32 GPIO26 DAC and accepts `-Volume 0..100`
(default 70) as a QEMU-side volume control independent of the guest.

The ESP32 audio model implements the I2S0 TX registers, linked DMA descriptors
and interrupt subset exercised by ESP-IDF `dac_continuous`. Samples are the
high byte of each 16-bit DMA slot, matching
`CONFIG_DAC_DMA_AUTO_16BIT_ALIGN`. A small analog-I2C bridge retains APLL
register writes and returns `APLL CAL_END`, allowing the same 8 kHz APLL
configuration used on the physical board. `dac-rate` defaults to 8000 and
`dac-volume` defaults to 70 when the machine is invoked directly.

The minimal `local_tools/qemu-sdspi-windows/` runtime is stored in Git, with
its executable managed by Git LFS. The demo SD image is also managed by Git
LFS. MSYS2, QEMU sources, build outputs and other disk images remain ignored
local artifacts.
