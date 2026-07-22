/*
 * hlvdec: sequential HLV-1 to YUV4MPEG2 decoder.
 *
 * The program writes only video bytes to stdout and diagnostics to stderr, so
 * it composes safely with FFmpeg and ffplay pipes.
 */
#include "hlv1.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/* Keep the Windows CRT from translating binary stdin/stdout bytes. */
static void binary_stdio(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

static FILE *open_input(const char *path) {
    return !strcmp(path, "-") ? stdin : fopen(path, "rb");
}
static FILE *open_output(const char *path) {
    return !strcmp(path, "-") ? stdout : fopen(path, "wb");
}
static void close_if_file(FILE *f, FILE *standard) {
    if (f && f != standard) fclose(f);
}

static void usage(const char *p) {
    fprintf(stderr,
            "Usage: %s input.hlv|- output.y4m|-\n"
            "Example: %s input.hlv - | ffplay -f yuv4mpegpipe -i -\n",
            p, p);
}
static double now_sec(void) { return (double)clock() / CLOCKS_PER_SEC; }

int main(int argc, char **argv) {
    binary_stdio();
    if (argc != 3) { usage(argv[0]); return 2; }
    FILE *in = open_input(argv[1]);
    if (!in) { perror(argv[1]); return 1; }
    FILE *out = open_output(argv[2]);
    if (!out) { perror(argv[2]); close_if_file(in, stdin); return 1; }

    HLV1Header h; int r = hlv1_header_read(in, &h);
    if (r < 0) { fprintf(stderr, "Header: %s\n", hlv1_strerror(r)); return 1; }
    HLV1Y4M y4m; r = hlv1_y4m_open_write(&y4m, out, h.width, h.height, h.fps_num, h.fps_den);
    if (r < 0) { fprintf(stderr, "Y4M: %s\n", hlv1_strerror(r)); return 1; }
    HLV1Decoder *dec = hlv1_decoder_create(&h);
    if (!dec) { fprintf(stderr, "Cannot allocate decoder\n"); return 1; }

    uint32_t frames = 0; double start = now_sec();
    for (;;) {
        HLV1Packet p; r = hlv1_packet_read(in, &p);
        if (r == HLV1_EOF) break;
        if (r < 0) { fprintf(stderr, "Packet: %s\n", hlv1_strerror(r)); return 1; }
        const HLV1Frame *f;
        r = hlv1_decoder_decode(dec, &p, &f); hlv1_packet_free(&p);
        if (r < 0) { fprintf(stderr, "Decode: %s at frame %u\n", hlv1_strerror(r), frames); return 1; }
        if ((r = hlv1_y4m_write_frame(&y4m, f)) < 0) {
            fprintf(stderr, "Write: %s\n", hlv1_strerror(r)); return 1;
        }
        ++frames;
    }
    if (fflush(out)) { fprintf(stderr, "Output flush failed\n"); return 1; }

    double elapsed = now_sec() - start;
    const HLV1Stats *s = hlv1_decoder_stats(dec);
    fprintf(stderr, "Decoded %u frames in %.3f s (%.2f fps)\n", frames, elapsed,
            elapsed > 0 ? frames / elapsed : 0);
    if (s && s->residual_blocks) fprintf(stderr,
        "Residual blocks: zero %.1f%%, DC-only %.1f%%\n",
        100.0*s->zero_residual_blocks/s->residual_blocks,
        100.0*s->dc_only_blocks/s->residual_blocks);

    hlv1_decoder_destroy(dec);
    close_if_file(in, stdin); close_if_file(out, stdout);
    return 0;
}
