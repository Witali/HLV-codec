#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compact_yuv420.h"
#include "divx3.h"
#include "divx3_avi.h"

enum { MAX_GOP_DISTANCE = 255 };

typedef struct ErrorStats {
    uint64_t squared_error;
    int64_t signed_error;
    uint64_t samples;
    unsigned maximum_error;
} ErrorStats;

static void accumulate_plane(
    ErrorStats *stats, const uint8_t *exact, unsigned exact_stride,
    const uint8_t *compact, unsigned compact_stride,
    const int8_t *correction, unsigned correction_stride,
    unsigned bits, unsigned width, unsigned height, uint8_t *scratch) {
    unsigned y;
    for (y = 0; y < height; ++y) {
        const uint8_t *exact_row = exact + (size_t)y * exact_stride;
        const uint8_t *compact_row =
            compact + (size_t)y * compact_stride;
        unsigned x;
        compact_yuv420_unpack_corrected_samples(
            compact_row, 0, (int)y, bits, correction,
            (int)correction_stride, scratch, (int)width);
        for (x = 0; x < width; ++x) {
            int difference = (int)scratch[x] - exact_row[x];
            unsigned magnitude =
                (unsigned)(difference < 0 ? -difference : difference);
            stats->squared_error +=
                (uint64_t)(difference * difference);
            stats->signed_error += difference;
            ++stats->samples;
            if (magnitude > stats->maximum_error)
                stats->maximum_error = magnitude;
        }
    }
}

static void merge_stats(ErrorStats *destination,
                        const ErrorStats *source) {
    destination->squared_error += source->squared_error;
    destination->signed_error += source->signed_error;
    destination->samples += source->samples;
    if (source->maximum_error > destination->maximum_error)
        destination->maximum_error = source->maximum_error;
}

static double stats_psnr(const ErrorStats *stats) {
    double mse;
    if (!stats->samples || !stats->squared_error) return 99.0;
    mse = (double)stats->squared_error / (double)stats->samples;
    return 10.0 * log10((255.0 * 255.0) / mse);
}

int main(int argc, char **argv) {
    FILE *input;
    Divx3AviInfo info;
    Divx3Decoder *exact_decoder;
    Divx3Decoder *compact_decoder;
    uint8_t *packet;
    uint8_t *scratch;
    ErrorStats overall = {0};
    ErrorStats luma = {0};
    ErrorStats chroma = {0};
    ErrorStats by_distance[MAX_GOP_DISTANCE + 1] = {{0}};
    unsigned frames_by_distance[MAX_GOP_DISTANCE + 1] = {0};
    unsigned frame_number = 0;
    unsigned gop_distance = 0;
    double minimum_frame_psnr = 99.0;
    unsigned minimum_frame_number = 0;
    int result;
    if (argc != 2) {
        fprintf(stderr, "usage: compare_compact INPUT.avi\n");
        return 2;
    }
    input = fopen(argv[1], "rb");
    if (!input) {
        fprintf(stderr, "cannot open input AVI\n");
        return 1;
    }
    result = divx3_avi_read_info(input, &info);
    if (result != DIVX3_AVI_OK) {
        fprintf(stderr, "AVI probe failed: %s\n",
                divx3_avi_strerror(result));
        fclose(input);
        return 1;
    }
    exact_decoder = divx3_decoder_create(info.width, info.height);
    compact_decoder =
        divx3_decoder_create_y6_u5_v5(info.width, info.height);
    packet = (uint8_t *)malloc(info.max_video_packet_size);
    scratch = (uint8_t *)malloc(info.width);
    if (!exact_decoder || !compact_decoder || !packet || !scratch) {
        fprintf(stderr, "comparison allocation failed\n");
        return 1;
    }

    for (;;) {
        Divx3Frame exact_frame = {0};
        Divx3Frame compact_frame = {0};
        ErrorStats frame_stats = {0};
        ErrorStats frame_luma = {0};
        ErrorStats frame_chroma = {0};
        size_t packet_size = 0;
        unsigned chroma_width = (info.width + 1U) / 2U;
        unsigned chroma_height = (info.height + 1U) / 2U;
        result = divx3_avi_read_video_packet(
            input, &info, packet, info.max_video_packet_size,
            &packet_size);
        if (result == DIVX3_AVI_EOF) break;
        if (result != DIVX3_AVI_OK) {
            fprintf(stderr, "packet %u failed: %s\n", frame_number,
                    divx3_avi_strerror(result));
            return 1;
        }
        result = divx3_decoder_decode(
            exact_decoder, packet, packet_size, &exact_frame);
        if (result == DIVX3_OK)
            result = divx3_decoder_decode(
                compact_decoder, packet, packet_size, &compact_frame);
        if (result != DIVX3_OK) {
            fprintf(stderr, "frame %u failed: %s\n", frame_number,
                    divx3_strerror(result));
            return 1;
        }
        gop_distance = exact_frame.intra ? 0U : gop_distance + 1U;
        if (gop_distance > MAX_GOP_DISTANCE)
            gop_distance = MAX_GOP_DISTANCE;
        accumulate_plane(
            &frame_luma, exact_frame.y, exact_frame.y_stride,
            compact_frame.y, compact_frame.y_stride,
            compact_frame.correction_y,
            compact_frame.correction_stride_y,
            COMPACT_YUV420_LUMA_BITS, info.width, info.height, scratch);
        accumulate_plane(
            &frame_chroma, exact_frame.cb, exact_frame.c_stride,
            compact_frame.cb, compact_frame.c_stride,
            compact_frame.correction_cb,
            compact_frame.correction_stride_c,
            COMPACT_YUV420_CHROMA_BITS,
            chroma_width, chroma_height, scratch);
        accumulate_plane(
            &frame_chroma, exact_frame.cr, exact_frame.c_stride,
            compact_frame.cr, compact_frame.c_stride,
            compact_frame.correction_cr,
            compact_frame.correction_stride_c,
            COMPACT_YUV420_CHROMA_BITS,
            chroma_width, chroma_height, scratch);
        merge_stats(&frame_stats, &frame_luma);
        merge_stats(&frame_stats, &frame_chroma);
        merge_stats(&luma, &frame_luma);
        merge_stats(&chroma, &frame_chroma);
        merge_stats(&overall, &frame_stats);
        merge_stats(&by_distance[gop_distance], &frame_stats);
        ++frames_by_distance[gop_distance];
        if (stats_psnr(&frame_stats) < minimum_frame_psnr) {
            minimum_frame_psnr = stats_psnr(&frame_stats);
            minimum_frame_number = frame_number;
        }
        ++frame_number;
    }

    printf("width=%u height=%u frames=%u exact_memory=%zu "
           "compact_memory=%zu saved=%zu\n",
           info.width, info.height, frame_number,
           divx3_decoder_memory_bytes(exact_decoder),
           divx3_decoder_memory_bytes(compact_decoder),
           divx3_decoder_memory_bytes(exact_decoder) -
               divx3_decoder_memory_bytes(compact_decoder));
    printf("overall_psnr=%.4f luma_psnr=%.4f chroma_psnr=%.4f "
           "bias=%.6f max_error=%u min_frame_psnr=%.4f "
           "min_frame=%u\n",
           stats_psnr(&overall), stats_psnr(&luma), stats_psnr(&chroma),
           overall.samples
               ? (double)overall.signed_error / (double)overall.samples
               : 0.0,
           overall.maximum_error, minimum_frame_psnr,
           minimum_frame_number);
    for (gop_distance = 0;
         gop_distance <= MAX_GOP_DISTANCE; ++gop_distance) {
        if (frames_by_distance[gop_distance]) {
            printf("gop_distance=%u frames=%u psnr=%.4f bias=%.6f "
                   "max_error=%u\n",
                   gop_distance, frames_by_distance[gop_distance],
                   stats_psnr(&by_distance[gop_distance]),
                   by_distance[gop_distance].samples
                       ? (double)by_distance[gop_distance].signed_error /
                             (double)by_distance[gop_distance].samples
                       : 0.0,
                   by_distance[gop_distance].maximum_error);
        }
    }

    free(scratch);
    free(packet);
    divx3_decoder_destroy(compact_decoder);
    divx3_decoder_destroy(exact_decoder);
    fclose(input);
    return frame_number ? 0 : 1;
}
