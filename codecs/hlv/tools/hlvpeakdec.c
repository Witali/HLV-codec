/*
 * hlvpeakdec: rank frames and GOPs by the decoder's conservative work model.
 *
 * The estimate is architecture-independent. It selects difficult source
 * windows for the real Xtensa QEMU benchmark; it is not a timing result.
 */
#include "hlv1.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct FrameScore {
    uint32_t frame;
    uint32_t gop_start;
    uint64_t cycles;
    uint32_t video_bytes;
    uint8_t frame_type;
} FrameScore;

static uint64_t conservative_cycles(const HLV1Stats *stats) {
    uint64_t cycles = 0;
    cycles += stats->copied_samples * 2;
    cycles += stats->interpolated_hv_samples * 8;
    cycles += stats->interpolated_bilinear_samples * 14;
    cycles += stats->intra_samples * 4;
    cycles += stats->fill_samples * 2;
    cycles += stats->palette_samples * 3;
    cycles += stats->gradient_samples * 4;
    cycles += stats->literal_samples * 2;
    cycles += stats->coefficient_symbols * 25;
    cycles += stats->dc_only_blocks * 45;
    cycles += stats->inverse_wht_blocks * 180;
    cycles += stats->macroblocks * 100;
    cycles += stats->motion_predictor_blocks * 20;
    cycles += (stats->decoded_bits + 7) / 8 * 8;
    return cycles;
}

static int compare_scores(const void *left, const void *right) {
    const FrameScore *a = (const FrameScore *)left;
    const FrameScore *b = (const FrameScore *)right;
    if (a->cycles < b->cycles) return 1;
    if (a->cycles > b->cycles) return -1;
    if (a->frame < b->frame) return -1;
    if (a->frame > b->frame) return 1;
    return 0;
}

static void usage(const char *program) {
    fprintf(stderr, "Usage: %s input.hlv\n", program);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        usage(argv[0]);
        return 2;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        perror(argv[1]);
        return 1;
    }

    HLV1Header header;
    int result = hlv1_header_read(file, &header);
    if (result < 0) {
        fprintf(stderr, "Header: %s\n", hlv1_strerror(result));
        fclose(file);
        return 1;
    }

    size_t capacity = header.frame_count ? header.frame_count : 1024;
    FrameScore *scores = calloc(capacity, sizeof *scores);
    HLV1Decoder *decoder = hlv1_decoder_create(&header);
    if (!scores || !decoder) {
        fprintf(stderr, "Out of memory\n");
        free(scores);
        hlv1_decoder_destroy(decoder);
        fclose(file);
        return 1;
    }

    uint32_t frame = 0;
    uint32_t gop_start = 0;
    for (;;) {
        HLV1Packet packet = {0};
        result = hlv1_packet_read(file, &packet);
        if (result == HLV1_EOF) break;
        if (result < 0) {
            fprintf(stderr, "Packet %" PRIu32 ": %s\n", frame,
                    hlv1_strerror(result));
            hlv1_packet_free(&packet);
            free(scores);
            hlv1_decoder_destroy(decoder);
            fclose(file);
            return 1;
        }

        if (frame == capacity) {
            size_t next_capacity = capacity * 2;
            FrameScore *next = realloc(scores, next_capacity * sizeof *scores);
            if (!next) {
                fprintf(stderr, "Out of memory\n");
                hlv1_packet_free(&packet);
                free(scores);
                hlv1_decoder_destroy(decoder);
                fclose(file);
                return 1;
            }
            scores = next;
            capacity = next_capacity;
        }

        if (packet.frame_type == HLV1_FRAME_KEY) gop_start = frame;
        const HLV1Stats *before_stats = hlv1_decoder_stats(decoder);
        uint64_t before = conservative_cycles(before_stats);
        uint32_t video_bytes =
            (uint32_t)hlv1_packet_video_payload_size(&packet);
        uint8_t frame_type = packet.frame_type;
        const HLV1Frame *decoded = NULL;
        result = hlv1_decoder_decode(decoder, &packet, &decoded);
        hlv1_packet_free(&packet);
        if (result < 0 || !decoded) {
            fprintf(stderr, "Decode frame %" PRIu32 ": %s\n", frame,
                    hlv1_strerror(result));
            free(scores);
            hlv1_decoder_destroy(decoder);
            fclose(file);
            return 1;
        }

        uint64_t after = conservative_cycles(hlv1_decoder_stats(decoder));
        scores[frame].frame = frame;
        scores[frame].gop_start = gop_start;
        scores[frame].cycles = after - before;
        scores[frame].video_bytes = video_bytes;
        scores[frame].frame_type = frame_type;
        ++frame;
    }

    fclose(file);
    hlv1_decoder_destroy(decoder);
    qsort(scores, frame, sizeof *scores, compare_scores);

    puts("rank,frame,gop_start,type,video_bytes,host_cycles");
    uint32_t limit = frame < 128 ? frame : 128;
    for (uint32_t i = 0; i < limit; ++i) {
        printf("%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%c,%" PRIu32
               ",%" PRIu64 "\n",
               i + 1, scores[i].frame, scores[i].gop_start,
               scores[i].frame_type == HLV1_FRAME_KEY ? 'K' : 'P',
               scores[i].video_bytes, scores[i].cycles);
    }

    free(scores);
    return 0;
}
