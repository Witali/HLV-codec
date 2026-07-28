# Project rules

## Local development tools

- Reserve the repository-root `tools/` directory for tracked, first-party
  command-line application source code.
- Never download or store third-party executables, SDKs, archives, package
  caches or other generated tool installations in `tools/`.
- Store repository-local downloaded tools under `local_tools/<tool-name>/`
  and update scripts and documentation to use that location.

## H.263 encoding profile

- Encode new H.263 assets only as baseline H.263 in an AVI container.
- Use only the standard QCIF `176x144` or CIF `352x288` picture size.
- Preserve the full source frame rate. Never add an H.263 preset that halves
  or otherwise reduces it; reject an unsupported source rate instead.
- Do not add H.263+, custom-size, or 3GP encoding presets. Legacy decoder
  support for those files does not make them valid encoding targets.

## ESP32 compressed input buffering

- In the ESP32 player, consume compressed video and audio through a reusable
  fixed-size refill, ring, or stream buffer whenever the codec can decode
  sequential input.
- Keep compressed-input buffer capacity independent of the maximum encoded
  packet or frame size. Do not silently grow a streaming buffer until it holds
  a complete packet.
- Retain a complete compressed packet only when the decoder requires
  contiguous or random-access input. Document that exception next to the
  decoder, enforce a strict packet-size limit, and do not copy the packet
  between tasks.
- Test every streaming input path with a valid packet larger than its refill
  buffer and compare decoded-frame checksums with the contiguous-input path.

## ESP32 firmware language

- Treat `firmware/esp32_2432s028_hlv_player_idf_c` as the primary ESP32 player
  firmware and keep it compatible with C99.
- Treat `firmware/esp32_2432s028_hlv_player_idf_cpp` only as the preserved C++
  reference implementation. Do not leave an active firmware feature or fix
  available only in the C++ variant; port it to the C firmware.
- Update both ESP32 firmware variants together whenever changing player
  behavior, codec or container support, performance or memory handling,
  hardware integration, build/setup scripts, tests, or documentation. Treat
  the change as incomplete until equivalent behavior is implemented and the
  applicable builds and regression tests pass for both variants.
- Translate required C++ code into C instead of compiling C++ sources or
  adding a C++ runtime dependency to the C firmware. Do not add `.cpp`, `.cc`,
  `.cxx` or `.hpp` files under the C firmware project.

## ESP32 physical SD test cleanup

- Record which files are uploaded or generated specifically for each physical
  ESP32 test run.
- After the test run finishes, delete those test-only files from the board's
  SD card and verify their removal with a fresh directory listing.
- Preserve files that existed before the run, user and demo assets,
  `play.txt`, and the persistent `crc32.txt` checksum index. Never reformat the
  card or use a broad filename pattern as test cleanup.

## Permission review timeouts

- If an automatic permission review times out before the requested command
  starts, retry the same permission request up to three total attempts.
- Stop and report the problem only after three consecutive permission-review
  timeouts.
- Do not apply this retry rule when the command may already have started or
  when it failed for any reason other than the permission review timing out.
  This prevents duplicate state-changing operations.
