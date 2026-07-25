# PacketVideo H.263 decoder provenance

The files in `include/` and `src/` are copied from the Android Open Source
Project `platform/frameworks/av` repository at commit
`e2f098935447ca4945946de5cb69db843fe3f003`:

`media/module/codecs/m4v_h263/dec`

They are distributed under Apache License 2.0; the upstream `NOTICE` file is
included unchanged. `include/log/log.h` is a project-local portability shim
for Android-only diagnostic calls. `include/m4vh263_decoder_pv_types.h` adds
one MSVC-only no-op definition for GCC function attributes.
