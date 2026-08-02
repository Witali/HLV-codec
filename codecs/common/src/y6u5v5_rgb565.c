#include "y6u5v5_rgb565.h"

#include <string.h>

#ifndef COMPACT_YUV_RGB565_CLAMP_TABLES
#define COMPACT_YUV_RGB565_CLAMP_TABLES 0
#endif

#ifndef COMPACT_YUV_RGB565_HOT_IRAM
#define COMPACT_YUV_RGB565_HOT_IRAM 0
#endif

#ifndef COMPACT_YUV_Q4_LUT
#define COMPACT_YUV_Q4_LUT 0
#endif

#if COMPACT_YUV_RGB565_HOT_IRAM
#include "esp_attr.h"
#define Y6U5V5_RGB565_ATTR IRAM_ATTR __attribute__((noinline))
#else
#define Y6U5V5_RGB565_ATTR
#endif

enum {
    kQ4Minimum = -8,
    kQ4Maximum = 14,
    kQ4Entries = kQ4Maximum - kQ4Minimum + 1,
    /* Exact extrema of the fixed-point BT.601 conversion below. */
    kRgbMinimum = -258,
    kRgbMaximum = 534,
    kRgbTableEntries = kRgbMaximum - kRgbMinimum + 1
};

static const uint8_t kThreshold[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5}
};
static const int8_t kNoCorrection[4] = {0, 0, 0, 0};

#if COMPACT_YUV_Q4_LUT
static int8_t q4_lut[4][kQ4Entries][4];
typedef char q4_lut_must_remain_368_bytes[
    sizeof q4_lut == 368 ? 1 : -1];
#endif

#if COMPACT_YUV_RGB565_CLAMP_TABLES
static uint16_t red_565[kRgbTableEntries];
static uint16_t green_565[kRgbTableEntries];
static uint8_t blue_565[kRgbTableEntries];
#endif

void y6u5v5_rgb565_initialize(void) {
#if COMPACT_YUV_Q4_LUT
    for (int phase = 0; phase < 4; ++phase) {
        for (int q4 = kQ4Minimum; q4 <= kQ4Maximum; ++q4) {
            const unsigned biased = (unsigned)(q4 + 128);
            const int whole = (int)(biased >> 4) - 8;
            const int fraction = (int)(biased & 15U);
            int8_t *correction = q4_lut[phase][q4 - kQ4Minimum];
            for (int sample = 0; sample < 4; ++sample) {
                correction[sample] = (int8_t)(
                    whole + (kThreshold[phase][sample] < fraction));
            }
        }
    }
#endif
#if COMPACT_YUV_RGB565_CLAMP_TABLES
    for (int value = kRgbMinimum; value <= kRgbMaximum; ++value) {
        const int clamped = value < 0 ? 0 : value > 255 ? 255 : value;
        const int index = value - kRgbMinimum;
        red_565[index] = (uint16_t)((clamped & 0xf8) << 8);
        green_565[index] = (uint16_t)((clamped & 0xfc) << 3);
        blue_565[index] = (uint8_t)(clamped >> 3);
    }
#endif
}

int y6u5v5_rgb565_can_convert_rows2(
    const y6u5v5_frame_t *frame, int source_y, int first_source_x,
    int output_width) {
    if (!frame) return 0;
    const int chroma_x = first_source_x >> 1;
    const int chroma_y = source_y >> 1;
    const int chroma_width = output_width >> 1;
    return frame->y.data && frame->u.data && frame->v.data &&
           source_y >= 0 && first_source_x >= 0 && output_width > 0 &&
           (source_y & 1) == 0 && (first_source_x & 15) == 0 &&
           (output_width & 15) == 0 &&
           source_y + 1 < (int)(frame->y.height) &&
           first_source_x + output_width <= (int)(frame->y.width) &&
           chroma_y < (int)(frame->u.height) &&
           chroma_y < (int)(frame->v.height) &&
           chroma_x + chroma_width <= (int)(frame->u.width) &&
           chroma_x + chroma_width <= (int)(frame->v.width);
}

static inline uint8_t correct_sample(unsigned sample, int correction) {
    const int value = (int)(sample) + correction;
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static inline const int8_t *correction4(
    const y6u5v5_plane_t *plane, int x, int y, int8_t fallback[4]) {
    if (!plane->correction) return kNoCorrection;
    const int q4 = plane->correction[
        (size_t)(y >> 3) * plane->correction_stride + (size_t)(x >> 3)];
#if COMPACT_YUV_Q4_LUT
    if (q4 >= kQ4Minimum && q4 <= kQ4Maximum) {
        return q4_lut[(unsigned)y & 3U][q4 - kQ4Minimum];
    }
#endif
    const unsigned biased = (unsigned)(q4 + 128);
    const int whole = (int)(biased >> 4) - 8;
    const int fraction = (int)(biased & 15U);
    const uint8_t *row = kThreshold[(unsigned)y & 3U];
    fallback[0] = (int8_t)(whole + (row[0] < fraction));
    fallback[1] = (int8_t)(whole + (row[1] < fraction));
    fallback[2] = (int8_t)(whole + (row[2] < fraction));
    fallback[3] = (int8_t)(whole + (row[3] < fraction));
    return fallback;
}

static inline void unpack_luma8(
    const y6u5v5_plane_t *plane, const uint8_t *packed, int x, int y,
    uint8_t output[8]) {
    int8_t fallback[4];
    const int8_t *correction = correction4(plane, x, y, fallback);
    output[0] = correct_sample((packed[0] & 0x3fU) << 2, correction[0]);
    output[1] = correct_sample(
        ((packed[0] >> 6) | ((packed[1] & 0x0fU) << 2)) << 2,
        correction[1]);
    output[2] = correct_sample(
        ((packed[1] >> 4) | ((packed[2] & 0x03U) << 4)) << 2,
        correction[2]);
    output[3] = correct_sample((packed[2] >> 2) << 2, correction[3]);
    output[4] = correct_sample((packed[3] & 0x3fU) << 2, correction[0]);
    output[5] = correct_sample(
        ((packed[3] >> 6) | ((packed[4] & 0x0fU) << 2)) << 2,
        correction[1]);
    output[6] = correct_sample(
        ((packed[4] >> 4) | ((packed[5] & 0x03U) << 4)) << 2,
        correction[2]);
    output[7] = correct_sample((packed[5] >> 2) << 2, correction[3]);
}

static inline void unpack_chroma8(
    const y6u5v5_plane_t *plane, const uint8_t *packed, int x, int y,
    uint8_t output[8]) {
    int8_t fallback[4];
    const int8_t *correction = correction4(plane, x, y, fallback);
    output[0] = correct_sample((packed[0] & 0x1fU) << 3, correction[0]);
    output[1] = correct_sample(
        ((packed[0] >> 5) | ((packed[1] & 0x03U) << 3)) << 3,
        correction[1]);
    output[2] = correct_sample(
        ((packed[1] >> 2) & 0x1fU) << 3, correction[2]);
    output[3] = correct_sample(
        ((packed[1] >> 7) | ((packed[2] & 0x0fU) << 1)) << 3,
        correction[3]);
    output[4] = correct_sample(
        ((packed[2] >> 4) | ((packed[3] & 0x01U) << 4)) << 3,
        correction[0]);
    output[5] = correct_sample(
        ((packed[3] >> 1) & 0x1fU) << 3, correction[1]);
    output[6] = correct_sample(
        ((packed[3] >> 6) | ((packed[4] & 0x07U) << 2)) << 3,
        correction[2]);
    output[7] = correct_sample((packed[4] >> 3) << 3, correction[3]);
}

static inline void store_pair(
    uint16_t *output, uint16_t first, uint16_t second) {
    const uint32_t pair = (uint32_t)first | ((uint32_t)second << 16);
    memcpy(output, &pair, sizeof pair);
}

static inline int clamp8(int value) {
    return value < 0 ? 0 : value > 255 ? 255 : value;
}

static inline uint16_t to_rgb565(
    const y6u5v5_rgb565_color_tables_t *color,
    int y, int red_add, int green_add, int blue_add) {
    const int luma = color->luma[(uint8_t)(y)];
#if COMPACT_YUV_RGB565_CLAMP_TABLES
    const int red = ((luma + red_add) >> 8) - kRgbMinimum;
    const int green = ((luma + green_add) >> 8) - kRgbMinimum;
    const int blue = ((luma + blue_add) >> 8) - kRgbMinimum;
    return (uint16_t)(red_565[red] | green_565[green] | blue_565[blue]);
#else
    const int red = clamp8((luma + red_add) >> 8);
    const int green = clamp8((luma + green_add) >> 8);
    const int blue = clamp8((luma + blue_add) >> 8);
    return (uint16_t)(((red & 0xf8) << 8) |
                      ((green & 0xfc) << 3) | (blue >> 3));
#endif
}

void Y6U5V5_RGB565_ATTR y6u5v5_rgb565_convert_rows2(
    const y6u5v5_frame_t *frame,
    const y6u5v5_rgb565_color_tables_t *color,
    int source_y, int first_source_x,
    uint16_t *output0, uint16_t *output1, int output_width) {
    const int chroma_y = source_y >> 1;
    const uint8_t *y0_row =
        frame->y.data + (size_t)source_y * frame->y.stride;
    const uint8_t *y1_row = y0_row + frame->y.stride;
    const uint8_t *u_row =
        frame->u.data + (size_t)chroma_y * frame->u.stride;
    const uint8_t *v_row =
        frame->v.data + (size_t)chroma_y * frame->v.stride;

    for (int output_x = 0; output_x < output_width; output_x += 16) {
        const int source_x = first_source_x + output_x;
        const int chroma_x = source_x >> 1;
        const size_t y_byte = (size_t)source_x * 6U >> 3;
        const size_t chroma_byte = (size_t)chroma_x * 5U >> 3;
        uint8_t y0[16];
        uint8_t y1[16];
        uint8_t u[8];
        uint8_t v[8];

        unpack_luma8(&frame->y, y0_row + y_byte,
                     source_x, source_y, y0);
        unpack_luma8(&frame->y, y0_row + y_byte + 6,
                     source_x + 8, source_y, y0 + 8);
        unpack_luma8(&frame->y, y1_row + y_byte,
                     source_x, source_y + 1, y1);
        unpack_luma8(&frame->y, y1_row + y_byte + 6,
                     source_x + 8, source_y + 1, y1 + 8);
        unpack_chroma8(&frame->u, u_row + chroma_byte,
                       chroma_x, chroma_y, u);
        unpack_chroma8(&frame->v, v_row + chroma_byte,
                       chroma_x, chroma_y, v);

        for (int sample = 0; sample < 8; ++sample) {
            const int luma_x = sample << 1;
            const int red_add = color->red_add[v[sample]];
            const int green_add =
                color->green_u_add[u[sample]] +
                color->green_v_add[v[sample]];
            const int blue_add = color->blue_add[u[sample]];
            store_pair(
                output0 + output_x + luma_x,
                to_rgb565(color, y0[luma_x],
                          red_add, green_add, blue_add),
                to_rgb565(color, y0[luma_x + 1],
                          red_add, green_add, blue_add));
            store_pair(
                output1 + output_x + luma_x,
                to_rgb565(color, y1[luma_x],
                          red_add, green_add, blue_add),
                to_rgb565(color, y1[luma_x + 1],
                          red_add, green_add, blue_add));
        }
    }
}

#undef Y6U5V5_RGB565_ATTR
