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

## Permission review timeouts

- If an automatic permission review times out before the requested command
  starts, retry the same permission request up to three total attempts.
- Stop and report the problem only after three consecutive permission-review
  timeouts.
- Do not apply this retry rule when the command may already have started or
  when it failed for any reason other than the permission review timing out.
  This prevents duplicate state-changing operations.
