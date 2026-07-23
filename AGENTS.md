# Project rules

## Big Buck Bunny transcoding source

- For every Big Buck Bunny transcode, use only
  `out/sources/big_buck_bunny_1080p_h264/big_buck_bunny_1080p_h264.mov`.
- Do not use the 320x180 download or any other lower-resolution copy as the
  transcoding source.

## Local development tools

- Reserve the repository-root `tools/` directory for tracked, first-party
  command-line application source code.
- Never download or store third-party executables, SDKs, archives, package
  caches or other generated tool installations in `tools/`.
- Store repository-local downloaded tools under `local_tools/<tool-name>/`
  and update scripts and documentation to use that location.
