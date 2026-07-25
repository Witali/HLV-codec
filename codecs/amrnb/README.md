# AMR-NB audio in 3GP

This package adds the audio half of the embedded 3GP profile used with the
project's QCIF H.263 decoder.

Supported input is deliberately constrained:

- MP4/3GP sample entry: `samr`;
- one mono channel at 8000 Hz;
- one AMR-NB frame per container sample;
- 160 decoded signed 16-bit PCM samples per 20 ms frame;
- speech modes 4.75 through 12.2 kbit/s, plus SID and no-data frames.

The bounded-table demultiplexer reads the audio track through a separate
`FILE` cursor, so video and audio prefetch do not disturb each other. Windows
converts decoded PCM16 to unsigned PCM8 for `waveOut`; ESP32 performs the same
conversion into its existing DAC stream buffer.

`third_party/pv` contains the pinned AOSP PacketVideo decoder and retained
license notices. See its README for provenance.
