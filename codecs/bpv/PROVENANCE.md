# BPV1 provenance

BPV1 was added from materials supplied by the repository owner on 2026-07-24.

Sources:

- public ChatGPT share, “Видеокодек на BPAL”:
  <https://chatgpt.com/share/6a636606-1cd4-83eb-acbb-8758b1312593>;
- `bpal-video-64-auto-rd.zip`, SHA-256
  `1F340CD3209A69C3592031500081FB4D0B461D8117C7286A6308BB2089FE5765`;
- `bpv1_synthetic_64pal_rd_60s_bundle.zip`, SHA-256
  `0D95A8381AF543EC3BC62824816DCD99CAAA6612B0C1592BD2C53773783D47C6`.

The first archive supplied the three JavaScript reference modules, their
tests, and the Russian format/RD documentation. Project-local Y4M adapters,
streaming validation and CLI tests were added during integration.
The bounded-memory, multi-threaded C11 encoder and its compatibility test are
project-local implementations of the documented BPV1 v2 bitstream.

The second archive supplied a 60-second synthetic experiment. Only its compact
reports and measurements are retained. Generated BPV1 streams, MP4 previews,
Python bytecode and caches are excluded. Its Python experiment driver is also
excluded because it refers to
`generate_bpv1_test_64pal_auto.py`, which was not present in either supplied
archive and therefore was not independently reproducible as delivered.

No explicit license or copyright notice was present in either archive. The
files are stored here at the repository owner's direction; this record does
not assert a separate redistribution license.
