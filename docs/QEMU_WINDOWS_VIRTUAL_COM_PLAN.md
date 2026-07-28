# Windows virtual COM port for ESP32 QEMU

Status: planned, not implemented.

## Goal

Expose emulated ESP32 UART0 from the native Windows QEMU build as an ordinary
Windows COM port. Existing host tools such as `monitor.ps1`,
`upload-video.ps1`, `list-files.ps1` and pyserial-based tests should then work
with QEMU and a physical CH340C board through the same byte-stream interface.

The initial implementation covers application UART traffic. Flashing the QEMU
guest through `esptool` and mapping DTR/RTS to the emulated RESET and BOOT
buttons are later stages.

## Constraints

- Keep `-serial stdio` as the default, so current launchers and CI do not
  change behavior.
- Use UART0 at the firmware's normal 460800 baud. The virtual path must also
  accept the upload tool's switch to 2000000 baud; host baud changes configure
  compatibility state but must not throttle the in-memory transport.
- Preserve bytes exactly in both directions. Do not add line conversion,
  terminal echo or text encoding.
- Only one program may own either COM endpoint at a time. Failure to open an
  occupied port must be immediate and explicit.
- Do not download or silently install a third-party kernel driver from project
  setup scripts. A virtual-port provider must be selected and installed
  explicitly by the user or system administrator.
- Do not commit driver packages, installers or generated certificates.
- Support COM numbers above 9 by using Win32 device paths such as
  `\\.\COM40`.

## Recommended architecture

Use a Windows virtual null-modem pair:

```text
ESP32 UART0
    |
QEMU chardev "serial"
    |
private endpoint COM40  <== virtual null-modem pair ==>  public endpoint COM41
                                                        |
                                              monitor/upload/test tool
```

QEMU opens the private endpoint for the lifetime of the emulator. Users and
project scripts receive only the public endpoint. The port numbers are
parameters and must never be hard-coded.

This is the preferred first implementation because the pinned native QEMU
binary already reports `serial`, `pipe` and `socket` chardev backends. A
virtual pair lets QEMU use its existing `serial` backend and avoids a custom
byte-forwarding process in the normal path.

A named-pipe or loopback-TCP backend remains useful for automated tests that
do not require a COM name. It does not by itself satisfy applications that
only enumerate COM ports.

## Implementation checklist

- [ ] Confirm direct COM and named-pipe syntax with the pinned Windows QEMU.
- [ ] Add selectable serial backends to both native Windows launchers.
- [ ] Document manual creation and assignment of a signed virtual COM pair.
- [ ] Validate monitor, list, CRC and large upload operations through COM.
- [ ] Add a driver-free socket or named-pipe integration test for CI.
- [ ] Map DTR/RTS to BOOT/RESET only after the byte stream is stable.
- [ ] Decide whether a repository-owned signed KMDF driver is still needed.

## Launcher changes

Extend `run-qemu-sdspi-windows.ps1` with:

```powershell
-SerialBackend Stdio|ComPort|NamedPipe
-QemuComPort COM40
-SerialPipeName hlv-esp32-uart0
```

Required behavior:

1. `Stdio` remains the default and keeps the current
   `-serial stdio` arguments.
2. `ComPort` requires `-QemuComPort`, normalizes it to a Win32 device path,
   verifies that the device exists, and checks that QEMU can open it
   exclusively.
3. Pass the endpoint to QEMU as a named UART0 chardev, expected to be
   equivalent to:

   ```text
   -chardev serial,id=uart0,path=\\.\COM40
   -serial chardev:uart0
   ```

   Confirm the exact accepted path syntax against the pinned Windows QEMU
   build before retaining it in the script.
4. `NamedPipe` provides a driver-free development backend. Its exact Windows
   pipe naming and server/client behavior must also be verified against the
   pinned build rather than assumed from Unix FIFO behavior.
5. Print the selected backend and endpoint before starting QEMU. Never fall
   back silently to stdio after a requested COM or pipe backend fails.
6. Add the same options to `run-qemu-demo-windows.ps1` and forward them without
   duplicating validation.

## Delivery stages

### 1. Direct COM data path

- Select a maintained Windows virtual null-modem provider with a valid driver
  signature for the supported Windows versions.
- Document manual creation of a pair, for example private `COM40` and public
  `COM41`.
- Add `ComPort` launcher support while keeping stdio unchanged.
- Verify bidirectional raw bytes before running the firmware protocol.

Acceptance:

- QEMU opens `COM40`.
- pyserial opens `COM41`.
- bytes sent in either direction arrive unchanged and in order.
- closing and restarting either endpoint does not require rebooting Windows.

### 2. Project tool compatibility

Run the public endpoint through:

- a 60-second UART monitor at 460800 baud;
- `list-files.ps1`;
- the UART CRC command;
- `upload-video.ps1` with a valid packet larger than the firmware's refill
  buffer;
- a transfer that switches from 460800 to 2000000 baud and back;
- repeated close/reopen cycles.

Compare device responses and transferred-file CRC32 values with a physical
ESP32 on CH340C. The COM bridge must not alter the UART protocol or require a
QEMU-specific branch in the Python clients.

### 3. Automated test backend

- Add a bounded integration test that launches QEMU headless.
- Prefer the socket or verified Windows named-pipe backend for CI, since CI
  should not require a kernel driver.
- Exercise the same parser and upload code used for COM ports.
- Capture QEMU stderr separately from UART0 so diagnostic output cannot corrupt
  the guest byte stream.

Acceptance:

- the test waits for `HLVUART 1 READY 460800`;
- upload and file CRCs match;
- QEMU exits cleanly on timeout or test completion;
- no QEMU or bridge process remains after failure.

### 4. RESET and BOOT control lines

Data transport comes first. After it is stable, decide whether physical-board
reset semantics are needed:

- DTR represents the ESP32 BOOT/GPIO0 input;
- RTS represents EN/RESET;
- use the repository's established 200/100 ms reset profile;
- opening a monitor with DTR and RTS inactive must not reset the guest;
- ordinary application reset and ROM-download entry must remain distinct.

The direct QEMU serial backend may not expose host modem-line changes to the
machine model. If it does not, add a small bridge process with two channels:
the byte stream and explicit line-state messages. Do not infer RESET/BOOT from
pauses or special byte sequences.

Acceptance:

- ordinary reset restarts the application without entering the ROM loader;
- the BOOT state is observable while asserted and released afterward;
- repeated monitor open/close does not reset the guest;
- four consecutive automatic reset cycles succeed.

### 5. Optional repository-owned driver

Only consider a custom driver if an external signed virtual-port provider is
unacceptable. The implementation would be a KMDF root-enumerated virtual
null-modem pair plus an installer:

- expose two `GUID_DEVCLASS_PORTS` devices with stable `PortName` values;
- implement read/write queues, cancellation, timeouts, purge, event masks and
  the serial IOCTL subset used by pyserial and QEMU;
- forward baud, parity and modem-control state without using baud to throttle
  the virtual transport;
- provide deterministic endpoint naming and removal;
- build, sign and test for every supported Windows architecture and release.

This stage requires a driver-signing and update policy. Test-signed drivers,
disabling Secure Boot or asking users to enable Windows test mode are not
acceptable release procedures.

## Test matrix

| Area | Cases |
|---|---|
| Port ownership | QEMU first, client first, endpoint already occupied |
| COM names | below and above COM9, missing endpoint, renamed pair |
| Data | empty, one byte, binary zeroes, random blocks, blocks above 32 KiB |
| Rates | 460800, 2000000, unsupported rate with clear failure |
| Lifetime | client reconnect, QEMU restart, forced client termination |
| Protocol | READY, list, CRC, upload, cumulative ACK/NAK retry |
| Control lines | inactive open, normal reset, BOOT hold/release |
| Failure cleanup | timeout, QEMU crash, bridge crash, SD error |

## Completion criteria

The feature is complete when a fresh native Windows QEMU launch exposes a
documented public COM endpoint, all existing UART application tools operate on
it without code forks, CRC-verified large transfers pass, stdio remains the
default, and the automated driver-free backend covers the same data protocol.
