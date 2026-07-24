#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pl_mpeg.h"

static uint8_t plane_sample(const plm_plane_t *plane, unsigned bits,
                            unsigned x, unsigned y) {
    const uint8_t *row = plane->data + (size_t)y * plane->stride;
    if (bits == 8) return row[x];
    unsigned bit = x * bits;
    unsigned byte = bit >> 3;
    unsigned shift = bit & 7U;
    unsigned value = row[byte];
    if (shift + bits > 8U) value |= (unsigned)row[byte + 1] << 8;
    value = (value >> shift) & ((1U << bits) - 1U);
    return (uint8_t)(value << (8U - bits));
}

static uint64_t checksum_byte(uint64_t checksum, uint8_t value) {
    return (checksum ^ value) * UINT64_C(1099511628211);
}

static uint64_t checksum_plane(uint64_t checksum,
                               const plm_plane_t *plane,
                               unsigned bits,
                               unsigned width,
                               unsigned height) {
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            checksum = checksum_byte(
                checksum, plane_sample(plane, bits, x, y));
        }
    }
    return checksum;
}

int main(int argc, char **argv) {
    if (argc != 3 ||
        (strcmp(argv[2], "plain") && strcmp(argv[2], "compact"))) {
        fprintf(stderr, "usage: test_decode INPUT.mpg plain|compact\n");
        return 2;
    }
    const int expected_storage =
        !strcmp(argv[2], "compact")
            ? PLM_FRAME_STORAGE_Y6_U5_V5
            : PLM_FRAME_STORAGE_YUV420;
    plm_t *mpeg = plm_create_with_filename(argv[1]);
    if (!mpeg) {
        fprintf(stderr, "cannot open MPEG input\n");
        return 1;
    }
    plm_set_audio_enabled(mpeg, 0);
    const int width = plm_get_width(mpeg);
    const int height = plm_get_height(mpeg);
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "invalid MPEG dimensions\n");
        plm_destroy(mpeg);
        return 1;
    }

    unsigned frames = 0;
    uint64_t checksum = UINT64_C(1469598103934665603);
    uint8_t *first_y = NULL;
    uint8_t *second_y = NULL;
    plm_frame_t *frame;
    while ((frame = plm_decode_video(mpeg)) != NULL) {
        if ((int)frame->width != width || (int)frame->height != height ||
            frame->storage_mode != expected_storage) {
            fprintf(stderr, "inconsistent frame metadata at %u\n", frames);
            plm_destroy(mpeg);
            return 1;
        }
        if (expected_storage == PLM_FRAME_STORAGE_Y6_U5_V5) {
            const unsigned padded_width = ((unsigned)width + 15U) & ~15U;
            if (frame->y.stride != padded_width * 6U / 8U ||
                frame->cb.stride != padded_width * 5U / 16U ||
                frame->cr.stride != padded_width * 5U / 16U) {
                fprintf(stderr, "invalid compact strides at %u\n", frames);
                plm_destroy(mpeg);
                return 1;
            }
            if (frames == 0) first_y = frame->y.data;
            if (frames == 1) second_y = frame->y.data;
            if (frames >= 2) {
                uint8_t *expected =
                    (frames & 1U) ? second_y : first_y;
                if (!first_y || !second_y || first_y == second_y ||
                    frame->y.data != expected) {
                    fprintf(stderr,
                            "compact frame buffers did not alternate at %u\n",
                            frames);
                    plm_destroy(mpeg);
                    return 1;
                }
            }
        }
        const unsigned y_bits =
            frame->storage_mode == PLM_FRAME_STORAGE_Y6_U5_V5 ? 6U : 8U;
        const unsigned c_bits =
            frame->storage_mode == PLM_FRAME_STORAGE_Y6_U5_V5 ? 5U : 8U;
        const unsigned chroma_width = ((unsigned)width + 1U) >> 1;
        const unsigned chroma_height = ((unsigned)height + 1U) >> 1;
        checksum = checksum_plane(
            checksum, &frame->y, y_bits, (unsigned)width, (unsigned)height);
        checksum = checksum_plane(
            checksum, &frame->cb, c_bits, chroma_width, chroma_height);
        checksum = checksum_plane(
            checksum, &frame->cr, c_bits, chroma_width, chroma_height);
        ++frames;
    }
    plm_destroy(mpeg);
    if (!frames) {
        fprintf(stderr, "no MPEG video frames decoded\n");
        return 1;
    }
    printf("mode=%s width=%d height=%d frames=%u checksum=%016" PRIx64 "\n",
           argv[2], width, height, frames, checksum);
    return 0;
}
