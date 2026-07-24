# HLV-1 codec package

This package contains the portable HLV-1 implementation:

- `include/` — public C API;
- `src/` — encoder, decoder, container, and Y4M implementation;
- `tools/` — command-line tools and the native Windows player source;
- `tests/` — codec-specific correctness tests;
- `library.properties` — Arduino library metadata.

From the repository root:

```sh
make
make test
```

The same targets can be run directly with `make -C codecs/hlv`. On Windows,
use `.\scripts\build_msvc.ps1`; generated executables remain in
`build\msvc\` so existing device and benchmark scripts keep a stable artifact
location.

The stream format, encoder modes, player, firmware, and benchmark workflow are
documented in the [repository README](../../README.md) and
[`docs/`](../../docs/).
