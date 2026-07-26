# Project rules

## Local development tools

- Reserve the repository-root `tools/` directory for tracked, first-party
  command-line application source code.
- Never download or store third-party executables, SDKs, archives, package
  caches or other generated tool installations in `tools/`.
- Store repository-local downloaded tools under `local_tools/<tool-name>/`
  and update scripts and documentation to use that location.

## Permission review timeouts

- If an automatic permission review times out before the requested command
  starts, retry the same permission request up to three total attempts.
- Stop and report the problem only after three consecutive permission-review
  timeouts.
- Do not apply this retry rule when the command may already have started or
  when it failed for any reason other than the permission review timing out.
  This prevents duplicate state-changing operations.
