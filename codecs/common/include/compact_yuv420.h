#ifndef HLV_COMPACT_YUV420_H
#define HLV_COMPACT_YUV420_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    COMPACT_YUV420_LUMA_BITS = 6,
    COMPACT_YUV420_CHROMA_BITS = 5,
    COMPACT_YUV420_BLOCK_SIZE = 8
};

typedef struct CompactYuv420Plane {
    uint8_t *data;
    int width;
    int height;
    int stride;
    int8_t *correction;
    int correction_stride;
    uint8_t bits;
} CompactYuv420Plane;

typedef struct CompactYuv420Frame {
    int width;
    int height;
    CompactYuv420Plane y;
    CompactYuv420Plane u;
    CompactYuv420Plane v;
} CompactYuv420Frame;

static inline size_t compact_yuv420_packed_stride(int width, unsigned bits) {
    return width > 0 && bits > 0 && bits <= 8
               ? ((size_t)width * bits + 7U) / 8U
               : 0;
}

static inline size_t compact_yuv420_correction_stride(int width) {
    return width > 0
               ? ((size_t)width + COMPACT_YUV420_BLOCK_SIZE - 1U) /
                     COMPACT_YUV420_BLOCK_SIZE
               : 0;
}

static inline size_t compact_yuv420_plane_storage_bytes(
    int width, int height, unsigned bits) {
    return height > 0
               ? compact_yuv420_packed_stride(width, bits) * (size_t)height
               : 0;
}

static inline size_t compact_yuv420_plane_correction_bytes(
    int width, int height) {
    return height > 0
               ? compact_yuv420_correction_stride(width) *
                     (((size_t)height +
                       COMPACT_YUV420_BLOCK_SIZE - 1U) /
                      COMPACT_YUV420_BLOCK_SIZE)
               : 0;
}

static inline size_t compact_yuv420_frame_storage_bytes(
    int width, int height) {
    int chroma_width = (width + 1) / 2;
    int chroma_height = (height + 1) / 2;
    return compact_yuv420_plane_storage_bytes(
               width, height, COMPACT_YUV420_LUMA_BITS) +
           2U * compact_yuv420_plane_storage_bytes(
                    chroma_width, chroma_height,
                    COMPACT_YUV420_CHROMA_BITS) +
           compact_yuv420_plane_correction_bytes(width, height) +
           2U * compact_yuv420_plane_correction_bytes(
                    chroma_width, chroma_height);
}

static inline uint8_t compact_yuv420_quantize_code(uint8_t value,
                                                    unsigned bits) {
    unsigned shift = 8U - bits;
    unsigned maximum = (1U << bits) - 1U;
    unsigned code = ((unsigned)value + (1U << (shift - 1U))) >> shift;
    return (uint8_t)(code > maximum ? maximum : code);
}

static inline int8_t compact_yuv420_error_q4(int residual_sum) {
    return (int8_t)(residual_sum >= 0
                        ? (residual_sum + 2) / 4
                        : -((-residual_sum + 2) / 4));
}

static inline uint8_t compact_yuv420_packed_sample(
    const uint8_t *row, int x, unsigned bits) {
    unsigned bit = (unsigned)x * bits;
    unsigned byte = bit >> 3;
    unsigned shift = bit & 7U;
    unsigned value = row[byte];
    if (shift + bits > 8U) value |= (unsigned)row[byte + 1] << 8;
    value = (value >> shift) & ((1U << bits) - 1U);
    return (uint8_t)(value << (8U - bits));
}

/*
 * Return the signed integer correction for one sample. The Q4 average is
 * distributed through a fixed 4x4 threshold map, repeated over each 8x8
 * correction block. Its 16 phases preserve the block average to 1/16 sample.
 */
static inline int compact_yuv420_correction(
    const int8_t *correction, int correction_stride, int x, int y) {
    static const uint8_t threshold[16] = {
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5
    };
    int q4;
    int whole;
    int fraction;
    unsigned phase;
    if (!correction) return 0;
    q4 = correction[(y >> 3) * correction_stride + (x >> 3)];
    whole = q4 >= 0 ? q4 / 16 : -((-q4 + 15) / 16);
    fraction = q4 - whole * 16;
    phase = ((unsigned)y & 3U) * 4U + ((unsigned)x & 3U);
    return whole + (threshold[phase] < fraction);
}

static inline uint8_t compact_yuv420_corrected_sample(
    const uint8_t *row, int x, int y, unsigned bits,
    const int8_t *correction, int correction_stride) {
    int value = compact_yuv420_packed_sample(row, x, bits) +
                compact_yuv420_correction(
                    correction, correction_stride, x, y);
    return (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
}

static inline void compact_yuv420_unpack_packed_samples(
    const uint8_t *row, int x, unsigned bits, uint8_t *output, int count) {
    unsigned bit;
    const uint8_t *input;
    unsigned cached;
    unsigned window;
    unsigned mask;
    unsigned output_shift;
    int i;
    if (count <= 0) return;
    bit = (unsigned)x * bits;
    input = row + (bit >> 3);
    cached = 8U - (bit & 7U);
    window = (unsigned)*input++ >> (bit & 7U);
    mask = (1U << bits) - 1U;
    output_shift = 8U - bits;
    for (i = 0; i < count; ++i) {
        if (cached < bits) {
            window |= (unsigned)*input++ << cached;
            cached += 8U;
        }
        output[i] = (uint8_t)((window & mask) << output_shift);
        window >>= bits;
        cached -= bits;
    }
}

static inline void compact_yuv420_unpack_corrected_samples(
    const uint8_t *row, int x, int y, unsigned bits,
    const int8_t *correction, int correction_stride,
    uint8_t *output, int count) {
    static const uint8_t threshold[16] = {
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5
    };
    const int8_t *correction_row;
    unsigned phase_row;
    int i = 0;
    compact_yuv420_unpack_packed_samples(row, x, bits, output, count);
    if (!correction || count <= 0) return;
    correction_row =
        correction + (y >> 3) * correction_stride;
    phase_row = ((unsigned)y & 3U) * 4U;
    while (i < count) {
        int sample_x = x + i;
        int q4 = correction_row[sample_x >> 3];
        int whole =
            q4 >= 0 ? q4 / 16 : -((-q4 + 15) / 16);
        int fraction = q4 - whole * 16;
        int span = 8 - (sample_x & 7);
        int end = i + span < count ? i + span : count;
        for (; i < end; ++i) {
            int current_x = x + i;
            unsigned phase =
                phase_row + ((unsigned)current_x & 3U);
            int value =
                output[i] + whole +
                (threshold[phase] < fraction);
            output[i] = (uint8_t)(
                value < 0 ? 0 : (value > 255 ? 255 : value));
        }
    }
}

/*
 * Pack a byte-aligned span. Current codec users pass 8 or 16 samples, for
 * which both the six- and five-bit representations end on a byte boundary.
 * residual_sum accumulates original-minus-quantized values. reconstructed
 * may alias source when the codec needs its row workspace quantized in place.
 */
static inline void compact_yuv420_pack_aligned_samples(
    uint8_t *destination, const uint8_t *source, int count, unsigned bits,
    int *residual_sum, uint8_t *reconstructed) {
    size_t bytes;
    unsigned shift;
    int i;
    if (count <= 0) return;
    if (count == 8 &&
        (bits == COMPACT_YUV420_LUMA_BITS ||
         bits == COMPACT_YUV420_CHROMA_BITS)) {
        unsigned code[8];
        shift = 8U - bits;
        for (i = 0; i < 8; ++i) {
            uint8_t expanded;
            code[i] = compact_yuv420_quantize_code(source[i], bits);
            expanded = (uint8_t)(code[i] << shift);
            if (residual_sum)
                *residual_sum += (int)source[i] - expanded;
            if (reconstructed) reconstructed[i] = expanded;
        }
        if (bits == COMPACT_YUV420_LUMA_BITS) {
            destination[0] =
                (uint8_t)(code[0] | code[1] << 6U);
            destination[1] =
                (uint8_t)(code[1] >> 2U | code[2] << 4U);
            destination[2] =
                (uint8_t)(code[2] >> 4U | code[3] << 2U);
            destination[3] =
                (uint8_t)(code[4] | code[5] << 6U);
            destination[4] =
                (uint8_t)(code[5] >> 2U | code[6] << 4U);
            destination[5] =
                (uint8_t)(code[6] >> 4U | code[7] << 2U);
        } else {
            destination[0] =
                (uint8_t)(code[0] | code[1] << 5U);
            destination[1] =
                (uint8_t)(code[1] >> 3U | code[2] << 2U |
                          code[3] << 7U);
            destination[2] =
                (uint8_t)(code[3] >> 1U | code[4] << 4U);
            destination[3] =
                (uint8_t)(code[4] >> 4U | code[5] << 1U |
                          code[6] << 6U);
            destination[4] =
                (uint8_t)(code[6] >> 2U | code[7] << 3U);
        }
        return;
    }
    bytes = ((size_t)count * bits + 7U) / 8U;
    memset(destination, 0, bytes);
    shift = 8U - bits;
    for (i = 0; i < count; ++i) {
        unsigned code = compact_yuv420_quantize_code(source[i], bits);
        unsigned bit = (unsigned)i * bits;
        unsigned byte = bit >> 3;
        unsigned bit_shift = bit & 7U;
        uint8_t expanded = (uint8_t)(code << shift);
        destination[byte] |= (uint8_t)(code << bit_shift);
        if (bit_shift + bits > 8U)
            destination[byte + 1] |= (uint8_t)(code >> (8U - bit_shift));
        if (residual_sum) *residual_sum += (int)source[i] - expanded;
        if (reconstructed) reconstructed[i] = expanded;
    }
}

#ifdef __cplusplus
}
#endif

#endif
