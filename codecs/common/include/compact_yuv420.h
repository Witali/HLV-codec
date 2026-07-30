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
    /*
     * Motion compensation and display conversion frequently request aligned
     * groups of 8 samples. Decode their byte-aligned representation directly
     * instead of maintaining the generic bit window for every sample.
     */
    if ((count & (COMPACT_YUV420_BLOCK_SIZE - 1)) == 0 &&
        (x & (COMPACT_YUV420_BLOCK_SIZE - 1)) == 0) {
        const uint8_t *aligned = row + ((size_t)x * bits >> 3);
        int remaining = count;
        while (remaining > 0) {
            if (bits == COMPACT_YUV420_LUMA_BITS) {
                output[0] = (uint8_t)((aligned[0] & 0x3fU) << 2);
                output[1] = (uint8_t)(
                    ((aligned[0] >> 6) |
                     ((aligned[1] & 0x0fU) << 2)) << 2);
                output[2] = (uint8_t)(
                    ((aligned[1] >> 4) |
                     ((aligned[2] & 0x03U) << 4)) << 2);
                output[3] = (uint8_t)((aligned[2] >> 2) << 2);
                output[4] = (uint8_t)((aligned[3] & 0x3fU) << 2);
                output[5] = (uint8_t)(
                    ((aligned[3] >> 6) |
                     ((aligned[4] & 0x0fU) << 2)) << 2);
                output[6] = (uint8_t)(
                    ((aligned[4] >> 4) |
                     ((aligned[5] & 0x03U) << 4)) << 2);
                output[7] = (uint8_t)((aligned[5] >> 2) << 2);
                aligned += 6;
            } else {
                output[0] = (uint8_t)((aligned[0] & 0x1fU) << 3);
                output[1] = (uint8_t)(
                    ((aligned[0] >> 5) |
                     ((aligned[1] & 0x03U) << 3)) << 3);
                output[2] =
                    (uint8_t)(((aligned[1] >> 2) & 0x1fU) << 3);
                output[3] = (uint8_t)(
                    ((aligned[1] >> 7) |
                     ((aligned[2] & 0x0fU) << 1)) << 3);
                output[4] = (uint8_t)(
                    ((aligned[2] >> 4) |
                     ((aligned[3] & 0x01U) << 4)) << 3);
                output[5] =
                    (uint8_t)(((aligned[3] >> 1) & 0x1fU) << 3);
                output[6] = (uint8_t)(
                    ((aligned[3] >> 6) |
                     ((aligned[4] & 0x07U) << 2)) << 3);
                output[7] = (uint8_t)((aligned[4] >> 3) << 3);
                aligned += 5;
            }
            output += COMPACT_YUV420_BLOCK_SIZE;
            remaining -= COMPACT_YUV420_BLOCK_SIZE;
        }
        return;
    }
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
static inline int compact_yuv420_pack_luma_8(
    uint8_t *destination, const uint8_t *source) {
    unsigned sample0 = source[0];
    unsigned sample1 = source[1];
    unsigned sample2 = source[2];
    unsigned sample3 = source[3];
    unsigned code0 = (sample0 + 2U) >> 2U;
    unsigned code1 = (sample1 + 2U) >> 2U;
    unsigned code2 = (sample2 + 2U) >> 2U;
    unsigned code3 = (sample3 + 2U) >> 2U;
    unsigned sample4 = source[4];
    unsigned sample5 = source[5];
    unsigned sample6 = source[6];
    unsigned sample7 = source[7];
    unsigned code4 = (sample4 + 2U) >> 2U;
    unsigned code5 = (sample5 + 2U) >> 2U;
    unsigned code6 = (sample6 + 2U) >> 2U;
    unsigned code7 = (sample7 + 2U) >> 2U;
    unsigned sample_sum;
    unsigned code_sum;

    code0 -= code0 >> 6U;
    code1 -= code1 >> 6U;
    code2 -= code2 >> 6U;
    code3 -= code3 >> 6U;
    code4 -= code4 >> 6U;
    code5 -= code5 >> 6U;
    code6 -= code6 >> 6U;
    code7 -= code7 >> 6U;
    destination[0] = (uint8_t)(code0 | code1 << 6U);
    destination[1] = (uint8_t)(code1 >> 2U | code2 << 4U);
    destination[2] = (uint8_t)(code2 >> 4U | code3 << 2U);
    destination[3] = (uint8_t)(code4 | code5 << 6U);
    destination[4] = (uint8_t)(code5 >> 2U | code6 << 4U);
    destination[5] = (uint8_t)(code6 >> 4U | code7 << 2U);
    sample_sum =
        sample0 + sample1 + sample2 + sample3 +
        sample4 + sample5 + sample6 + sample7;
    code_sum =
        code0 + code1 + code2 + code3 +
        code4 + code5 + code6 + code7;
    return (int)(sample_sum - (code_sum << 2U));
}

static inline int compact_yuv420_pack_chroma_8(
    uint8_t *destination, const uint8_t *source) {
    unsigned sample0 = source[0];
    unsigned sample1 = source[1];
    unsigned sample2 = source[2];
    unsigned sample3 = source[3];
    unsigned code0 = (sample0 + 4U) >> 3U;
    unsigned code1 = (sample1 + 4U) >> 3U;
    unsigned code2 = (sample2 + 4U) >> 3U;
    unsigned code3 = (sample3 + 4U) >> 3U;
    unsigned sample4 = source[4];
    unsigned sample5 = source[5];
    unsigned sample6 = source[6];
    unsigned sample7 = source[7];
    unsigned code4 = (sample4 + 4U) >> 3U;
    unsigned code5 = (sample5 + 4U) >> 3U;
    unsigned code6 = (sample6 + 4U) >> 3U;
    unsigned code7 = (sample7 + 4U) >> 3U;
    unsigned sample_sum;
    unsigned code_sum;

    code0 -= code0 >> 5U;
    code1 -= code1 >> 5U;
    code2 -= code2 >> 5U;
    code3 -= code3 >> 5U;
    code4 -= code4 >> 5U;
    code5 -= code5 >> 5U;
    code6 -= code6 >> 5U;
    code7 -= code7 >> 5U;
    destination[0] = (uint8_t)(code0 | code1 << 5U);
    destination[1] = (uint8_t)(
        code1 >> 3U | code2 << 2U | code3 << 7U);
    destination[2] = (uint8_t)(code3 >> 1U | code4 << 4U);
    destination[3] = (uint8_t)(
        code4 >> 4U | code5 << 1U | code6 << 6U);
    destination[4] = (uint8_t)(code6 >> 2U | code7 << 3U);
    sample_sum =
        sample0 + sample1 + sample2 + sample3 +
        sample4 + sample5 + sample6 + sample7;
    code_sum =
        code0 + code1 + code2 + code3 +
        code4 + code5 + code6 + code7;
    return (int)(sample_sum - (code_sum << 3U));
}

static inline void compact_yuv420_pack_aligned_samples(
    uint8_t *destination, const uint8_t *source, int count, unsigned bits,
    int *residual_sum, uint8_t *reconstructed) {
    size_t bytes;
    unsigned shift;
    int i;
    if (count <= 0) return;
    if (count == 8 && !reconstructed) {
        int residual;
        if (bits == COMPACT_YUV420_LUMA_BITS) {
            residual = compact_yuv420_pack_luma_8(
                destination, source);
        } else if (bits == COMPACT_YUV420_CHROMA_BITS) {
            residual = compact_yuv420_pack_chroma_8(
                destination, source);
        } else {
            residual = 0;
            goto generic_pack;
        }
        if (residual_sum) *residual_sum += residual;
        return;
    }
generic_pack:
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

static inline void compact_yuv420_pack_plane_rows(
    CompactYuv420Plane *destination, int destination_y,
    const uint8_t *source, int source_stride, int row_count) {
    int block_y;
    if (!destination || !destination->data || !destination->correction ||
        !source || destination->width <= 0 || destination->height <= 0 ||
        destination->stride <= 0 ||
        destination->correction_stride <= 0 ||
        source_stride < destination->width || destination_y < 0 ||
        destination_y >= destination->height || row_count <= 0 ||
        destination_y % COMPACT_YUV420_BLOCK_SIZE != 0 ||
        destination_y + row_count > destination->height ||
        (row_count % COMPACT_YUV420_BLOCK_SIZE != 0 &&
         destination_y + row_count != destination->height)) {
        return;
    }
    for (block_y = destination_y;
         block_y < destination_y + row_count;
         block_y += COMPACT_YUV420_BLOCK_SIZE) {
        int block_x;
        int block_height =
            destination_y + row_count - block_y <
                    COMPACT_YUV420_BLOCK_SIZE
                ? destination_y + row_count - block_y
                : COMPACT_YUV420_BLOCK_SIZE;
        for (block_x = 0; block_x < destination->width;
             block_x += COMPACT_YUV420_BLOCK_SIZE) {
            int residual_sum = 0;
            int block_width =
                destination->width - block_x <
                        COMPACT_YUV420_BLOCK_SIZE
                    ? destination->width - block_x
                    : COMPACT_YUV420_BLOCK_SIZE;
            int row;
            for (row = 0; row < block_height; ++row) {
                uint8_t *packed_row =
                    destination->data +
                    (size_t)(block_y + row) * destination->stride +
                    ((size_t)block_x * destination->bits >> 3);
                const uint8_t *source_row =
                    source +
                    (size_t)(block_y - destination_y + row) *
                        source_stride +
                    block_x;
                compact_yuv420_pack_aligned_samples(
                    packed_row, source_row, block_width,
                    destination->bits, &residual_sum, NULL);
            }
            destination->correction[
                (block_y / COMPACT_YUV420_BLOCK_SIZE) *
                    destination->correction_stride +
                block_x / COMPACT_YUV420_BLOCK_SIZE] =
                compact_yuv420_error_q4(residual_sum);
        }
    }
}

static inline void compact_yuv420_pack_plane(
    CompactYuv420Plane *destination, const uint8_t *source,
    int source_stride) {
    if (!destination) return;
    compact_yuv420_pack_plane_rows(
        destination, 0, source, source_stride, destination->height);
}

static inline void compact_yuv420_unpack_plane(
    const CompactYuv420Plane *source, uint8_t *destination,
    int destination_stride) {
    int y;
    if (!source || !source->data || !destination ||
        source->width <= 0 || source->height <= 0 ||
        source->stride <= 0 ||
        destination_stride < source->width) {
        return;
    }
    for (y = 0; y < source->height; ++y) {
        compact_yuv420_unpack_corrected_samples(
            source->data + (size_t)y * source->stride,
            0, y, source->bits, source->correction,
            source->correction_stride,
            destination + (size_t)y * destination_stride,
            source->width);
    }
}

#ifdef __cplusplus
}
#endif

#endif
