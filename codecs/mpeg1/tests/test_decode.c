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

static unsigned bounded_height;
static unsigned bounded_next_y;
static unsigned bounded_b_frames;
static int bounded_callback_error;
static uint64_t bounded_b_checksum = UINT64_C(1469598103934665603);

static void consume_b_rows(plm_video_t *decoder, const plm_frame_t *frame,
                           unsigned first_y, unsigned row_count, void *user) {
    (void)decoder;
    (void)user;
    if (!frame || frame->storage_mode != PLM_FRAME_STORAGE_YUV420_ROWS ||
        frame->picture_type != PLM_VIDEO_PICTURE_TYPE_B || !row_count ||
        first_y != bounded_next_y || first_y + row_count > bounded_height) {
        fprintf(stderr,
                "invalid B rows: frame=%p storage=%d type=%d "
                "first=%u expected=%u rows=%u height=%u\n",
                (void *)frame, frame ? frame->storage_mode : -1,
                frame ? frame->picture_type : -1, first_y, bounded_next_y,
                row_count, bounded_height);
        bounded_callback_error = 1;
        return;
    }
    bounded_b_checksum = checksum_plane(
        bounded_b_checksum, &frame->y, 8, frame->width, row_count);
    bounded_b_checksum = checksum_plane(
        bounded_b_checksum, &frame->cb, 8, (frame->width + 1U) >> 1,
        (row_count + 1U) >> 1);
    bounded_b_checksum = checksum_plane(
        bounded_b_checksum, &frame->cr, 8, (frame->width + 1U) >> 1,
        (row_count + 1U) >> 1);
    bounded_next_y += row_count;
    if (bounded_next_y == bounded_height) {
        bounded_next_y = 0;
        bounded_b_frames++;
    }
}

static int test_keyframe_catchup(const char *path) {
    plm_t *mpeg = plm_create_with_filename(path);
    unsigned skipped = 0;
    plm_frame_t *frame;
    if (!mpeg) return 0;
    plm_set_audio_enabled(mpeg, 0);
    frame = plm_decode_video(mpeg);
    if (!frame ||
        frame->picture_type != PLM_VIDEO_PICTURE_TYPE_INTRA) {
        plm_destroy(mpeg);
        return 0;
    }
    frame = plm_decode_video_keyframe(mpeg, &skipped);
    if (!frame || !skipped ||
        frame->picture_type != PLM_VIDEO_PICTURE_TYPE_INTRA) {
        plm_destroy(mpeg);
        return 0;
    }
    frame = plm_decode_video(mpeg);
    if (!frame) {
        plm_destroy(mpeg);
        return 0;
    }
    plm_destroy(mpeg);
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 3 ||
        (strcmp(argv[2], "plain") && strcmp(argv[2], "compact") &&
         strcmp(argv[2], "bounded"))) {
        fprintf(stderr,
                "usage: test_decode INPUT.mpg plain|compact|bounded\n");
        return 2;
    }
    const int bounded = !strcmp(argv[2], "bounded");
    const int expected_storage =
        strcmp(argv[2], "plain")
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
    const double duration = plm_get_duration(mpeg);
    if (width <= 0 || height <= 0 || !(duration > 0.0)) {
        fprintf(stderr,
                "invalid MPEG metadata: width=%d height=%d duration=%.6f\n",
                width, height, duration);
        plm_destroy(mpeg);
        return 1;
    }
    if (bounded) {
        bounded_height = (unsigned)height;
        plm_set_video_b_frame_row_callback(mpeg, consume_b_rows, NULL);
    }
    if (!test_keyframe_catchup(argv[1])) {
        fprintf(stderr, "MPEG keyframe catch-up regression failed\n");
        plm_destroy(mpeg);
        return 1;
    }

    unsigned frames = 0;
    unsigned picture_types[4] = {0, 0, 0, 0};
    uint64_t checksum = UINT64_C(1469598103934665603);
    uint8_t *compact_y_buffers[3] = {NULL, NULL, NULL};
    unsigned compact_y_buffer_count = 0;
    plm_frame_t *frame;
    while ((frame = plm_decode_video(mpeg)) != NULL) {
        const int frame_storage =
            bounded && frame->picture_type == PLM_VIDEO_PICTURE_TYPE_B
                ? PLM_FRAME_STORAGE_YUV420_ROWS
                : expected_storage;
        if ((int)frame->width != width || (int)frame->height != height ||
            frame->storage_mode != frame_storage) {
            fprintf(stderr, "inconsistent frame metadata at %u\n", frames);
            plm_destroy(mpeg);
            return 1;
        }
        if (frame->picture_type < PLM_VIDEO_PICTURE_TYPE_INTRA ||
            frame->picture_type > PLM_VIDEO_PICTURE_TYPE_B) {
            fprintf(stderr, "invalid picture type at %u\n", frames);
            plm_destroy(mpeg);
            return 1;
        }
        picture_types[frame->picture_type]++;
        if (frame_storage == PLM_FRAME_STORAGE_Y6_U5_V5) {
            const unsigned padded_width = ((unsigned)width + 15U) & ~15U;
            if (frame->y.stride != padded_width * 6U / 8U ||
                frame->cb.stride != padded_width * 5U / 16U ||
                frame->cr.stride != padded_width * 5U / 16U) {
                fprintf(stderr, "invalid compact strides at %u\n", frames);
                plm_destroy(mpeg);
                return 1;
            }
            unsigned buffer_index = 0;
            while (buffer_index < compact_y_buffer_count &&
                   compact_y_buffers[buffer_index] != frame->y.data) {
                buffer_index++;
            }
            if (buffer_index == compact_y_buffer_count) {
                if (compact_y_buffer_count == 3) {
                    fprintf(stderr,
                            "more than three compact frame buffers at %u\n",
                            frames);
                    plm_destroy(mpeg);
                    return 1;
                }
                compact_y_buffers[compact_y_buffer_count++] = frame->y.data;
            }
        }
        if (frame_storage == PLM_FRAME_STORAGE_YUV420_ROWS) {
            if (bounded_callback_error ||
                bounded_b_frames !=
                    picture_types[PLM_VIDEO_PICTURE_TYPE_B]) {
                fprintf(stderr, "incomplete bounded B frame at %u\n", frames);
                plm_destroy(mpeg);
                return 1;
            }
            ++frames;
            continue;
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
    if (bounded &&
        (bounded_callback_error || bounded_next_y ||
         bounded_b_frames != picture_types[PLM_VIDEO_PICTURE_TYPE_B])) {
        fprintf(stderr, "bounded B-row callback regression\n");
        return 1;
    }
    printf("mode=%s width=%d height=%d duration=%.6f frames=%u "
           "i=%u p=%u b=%u buffers=%u checksum=%016" PRIx64
           " bchecksum=%016" PRIx64 "\n",
           argv[2], width, height, duration, frames,
           picture_types[PLM_VIDEO_PICTURE_TYPE_INTRA],
           picture_types[PLM_VIDEO_PICTURE_TYPE_PREDICTIVE],
           picture_types[PLM_VIDEO_PICTURE_TYPE_B],
           compact_y_buffer_count, checksum, bounded_b_checksum);
    return 0;
}
