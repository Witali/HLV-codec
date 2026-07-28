# Native Windows ESP32 QEMU runtime

This directory contains the minimal native Windows runtime built from the
official [Espressif QEMU](https://github.com/espressif/qemu) fork plus the
repository's ST7789/SDSPI patches.

- QEMU version: `9.2.2`
- Upstream tag: `esp-develop-9.2.2-20260417`
- Upstream commit: `40edccac415693c5130f91c01d84176ae6008566`
- Build host: Windows x86-64, MSYS2/MinGW64
- Target: `xtensa-softmmu`
- Package version: `HLV ST7789 SDSPI Windows`
- EXE SHA-256:
  `6DF0AE95C7AA6D08A181D43179DA1F60ACD85355FA92756E585C93154E6A32C1`

The executable is statically linked. It imports only standard Windows system
DLLs; no MinGW or MSYS2 DLL needs to be distributed with it. The two files
under `share/qemu/` are the upstream ESP32 ROM blobs required to boot the
ESP32 machine.

Build or refresh this runtime with:

```powershell
.\firmware\esp32_2432s028_hlv_player_idf_c\setup-qemu-sdspi-windows.ps1
```

Run it with:

```powershell
.\firmware\esp32_2432s028_hlv_player_idf_c\run-qemu-sdspi-windows.ps1 `
    -FlashImage <flash.bin>
```

This uses the repository's five-minute Big Buck Bunny H.263/AVI SD-card demo
by default. Pass `-SdImage <sd.img>` to override it.

The executable and default demo image are stored with Git LFS. QEMU sources,
MSYS2, build outputs, other disk images and UART logs are intentionally not
tracked.
