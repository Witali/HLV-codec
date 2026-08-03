/*
 * hlvdec: sequential HLV-1 to YUV4MPEG2 decoder.
 *
 * The program writes only video bytes to stdout and diagnostics to stderr, so
 * it composes safely with FFmpeg and ffplay pipes.
 */
#include "hlv1.h"
#include "ima_adpcm.h"

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
            "Usage: %s input.hlv|- output.y4m|- [--audio-out audio.raw|-]\n"
            "Audio output is PCM_U8 for codec 1 and PCM_S16LE for codec 2.\n"
            "Example: %s input.hlv - | ffplay -f yuv4mpegpipe -i -\n",
            p, p);
}
static double now_sec(void) { return (double)clock() / CLOCKS_PER_SEC; }

static int write_audio_packet(FILE *output, uint8_t codec,
                              const uint8_t *data, size_t size) {
    if (!size) return 0;
    if (codec == HLV1_AUDIO_PCM_U8)
        return fwrite(data, 1, size, output) == size ? 0 : -1;
    if (codec == HLV1_AUDIO_IMA_ADPCM) {
        int16_t samples[IMA_ADPCM_MAX_BLOCK_SAMPLES];
        uint8_t pcm[IMA_ADPCM_MAX_BLOCK_SAMPLES * 2U];
        size_t sample_count = 0;
        size_t sample;
        if (ima_adpcm_decode_block(data, size, samples,
                                   IMA_ADPCM_MAX_BLOCK_SAMPLES,
                                   &sample_count))
            return -2;
        for (sample = 0; sample < sample_count; ++sample) {
            const uint16_t value = (uint16_t)samples[sample];
            pcm[sample * 2U] = (uint8_t)value;
            pcm[sample * 2U + 1U] = (uint8_t)(value >> 8);
        }
        return fwrite(pcm, 2U, sample_count, output) == sample_count ? 0 : -1;
    }
    return -2;
}

int main(int argc, char **argv) {
    binary_stdio();
    if (argc != 3 && argc != 5) { usage(argv[0]); return 2; }
    const char *audio_path = NULL;
    if (argc == 5) {
        if (strcmp(argv[3], "--audio-out")) { usage(argv[0]); return 2; }
        audio_path = argv[4];
        if (!strcmp(argv[2], "-") && !strcmp(audio_path, "-")) {
            fprintf(stderr, "Video and audio cannot both use stdout\n");
            return 2;
        }
    }
    FILE *in = open_input(argv[1]);
    if (!in) { perror(argv[1]); return 1; }
    FILE *out = open_output(argv[2]);
    if (!out) { perror(argv[2]); close_if_file(in, stdin); return 1; }

    HLV1Header h; int r = hlv1_header_read(in, &h);
    if (r < 0) { fprintf(stderr, "Header: %s\n", hlv1_strerror(r)); return 1; }
    if (audio_path && !(h.flags & HLV1_FLAG_AUDIO)) {
        fprintf(stderr, "The HLV stream has no audio track\n");
        return 1;
    }
    FILE *audio = audio_path ? open_output(audio_path) : NULL;
    if (audio_path && !audio) { perror(audio_path); return 1; }
    HLV1Y4M y4m; r = hlv1_y4m_open_write(&y4m, out, h.width, h.height, h.fps_num, h.fps_den);
    if (r < 0) { fprintf(stderr, "Y4M: %s\n", hlv1_strerror(r)); return 1; }
    HLV1Decoder *dec = hlv1_decoder_create(&h);
    if (!dec) { fprintf(stderr, "Cannot allocate decoder\n"); return 1; }

    uint32_t frames = 0; double start = now_sec();
    for (;;) {
        HLV1Packet p; r = hlv1_packet_read(in, &p);
        if (r == HLV1_EOF) break;
        if (r < 0) { fprintf(stderr, "Packet: %s\n", hlv1_strerror(r)); return 1; }
        if (audio) {
            size_t audio_size = hlv1_packet_audio_size(&p);
            const uint8_t *audio_data = hlv1_packet_audio_data(&p);
            const int audio_result = write_audio_packet(
                audio, h.audio_codec, audio_data, audio_size);
            if (audio_result) {
                fprintf(stderr, "%s\n", audio_result == -2
                    ? "Invalid IMA ADPCM audio block" : "Audio write failed");
                hlv1_packet_free(&p);
                return 1;
            }
        }
        const HLV1Frame *f;
        r = hlv1_decoder_decode(dec, &p, &f); hlv1_packet_free(&p);
        if (r < 0) { fprintf(stderr, "Decode: %s at frame %u\n", hlv1_strerror(r), frames); return 1; }
        if ((r = hlv1_y4m_write_frame(&y4m, f)) < 0) {
            fprintf(stderr, "Write: %s\n", hlv1_strerror(r)); return 1;
        }
        ++frames;
    }
    if (fflush(out)) { fprintf(stderr, "Output flush failed\n"); return 1; }
    if (audio && fflush(audio)) { fprintf(stderr, "Audio flush failed\n"); return 1; }

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
    close_if_file(audio, stdout);
    return 0;
}
