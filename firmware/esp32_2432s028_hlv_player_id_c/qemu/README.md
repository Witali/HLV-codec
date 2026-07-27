# ESP32 SDSPI QEMU sources

Original repository: [Espressif QEMU](https://github.com/espressif/qemu)

- Upstream tag:
  [`esp-develop-9.2.2-20260417`](https://github.com/espressif/qemu/tree/esp-develop-9.2.2-20260417)
- Upstream commit:
  [`40edccac415693c5130f91c01d84176ae6008566`](https://github.com/espressif/qemu/commit/40edccac415693c5130f91c01d84176ae6008566)
- Local patch: `patches/0001-esp32-sdspi.patch`
- Full patched files: `modified_sources/`

`modified_sources/` preserves complete copies of every upstream file changed
by the local patch, using the same paths as the original source tree:

```text
hw/gpio/esp32_gpio.c
hw/sd/sd.c
hw/sd/ssi-sd.c
hw/ssi/esp32_spi.c
hw/xtensa/Kconfig
hw/xtensa/esp32.c
hw/xtensa/esp32_intc.c
include/hw/gpio/esp32_gpio.h
include/hw/ssi/esp32_spi.h
include/hw/xtensa/esp32_intc.h
```

The patch remains the canonical installation method used by
`../setup-qemu-sdspi.ps1`; the full files are retained for inspection and
recovery. These files remain subject to their upstream copyright and license
terms. See the [QEMU license documentation](https://www.qemu.org/docs/master/about/license.html).
