# PacketVideo AMR-NB decoder

The `common` and `dec` directories are imported from Android Open Source
Project `platform/frameworks/av` at commit
`e2f098935447ca4945946de5cb69db843fe3f003`:

<https://android.googlesource.com/platform/frameworks/av/+/e2f098935447ca4945946de5cb69db843fe3f003/media/module/codecs/amrnb/>

The original `NOTICE` files are retained in both directories. The source is
compiled unchanged. `include/log/log.h` is a project-local portability shim
for the single Android logging call used by the decoder.

Only the source files listed by the upstream `common/Android.bp` and
`dec/Android.bp` library targets are part of project builds.
