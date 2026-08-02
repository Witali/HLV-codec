#include "compact_yuv420.h"
#include "y6u5v5_rgb565.h"

#include <stdio.h>
#include <string.h>

enum {
    kWidth = 32,
    kHeight = 16,
    kChromaWidth = kWidth / 2,
    kChromaHeight = kHeight / 2
};

static int32_t luma[256];
static int32_t red_add[256];
static int32_t green_u_add[256];
static int32_t green_v_add[256];
static int32_t blue_add[256];

static int fail(const char *message, int line, int x, int y) {
    fprintf(stderr,
            "y6u5v5_rgb565 test failed at line %d (%d,%d): %s\n",
            line, x, y, message);
    return 1;
}

#define CHECK_AT(condition, message, x, y) \
    do { \
        if (!(condition)) return fail((message), __LINE__, (x), (y)); \
    } while (0)

static uint16_t reference_rgb565(
    const CompactYuv420Plane *y_plane,
    const CompactYuv420Plane *u_plane,
    const CompactYuv420Plane *v_plane, int x, int y) {
    const int chroma_x = x >> 1;
    const int chroma_y = y >> 1;
    const uint8_t y_sample = compact_yuv420_corrected_sample(
        y_plane->data + (size_t)y * y_plane->stride, x, y,
        y_plane->bits, y_plane->correction, y_plane->correction_stride);
    const uint8_t u_sample = compact_yuv420_corrected_sample(
        u_plane->data + (size_t)chroma_y * u_plane->stride,
        chroma_x, chroma_y, u_plane->bits,
        u_plane->correction, u_plane->correction_stride);
    const uint8_t v_sample = compact_yuv420_corrected_sample(
        v_plane->data + (size_t)chroma_y * v_plane->stride,
        chroma_x, chroma_y, v_plane->bits,
        v_plane->correction, v_plane->correction_stride);
    const int y_value = luma[y_sample];
    int red = (y_value + red_add[v_sample]) >> 8;
    int green =
        (y_value + green_u_add[u_sample] + green_v_add[v_sample]) >> 8;
    int blue = (y_value + blue_add[u_sample]) >> 8;
    red = red < 0 ? 0 : red > 255 ? 255 : red;
    green = green < 0 ? 0 : green > 255 ? 255 : green;
    blue = blue < 0 ? 0 : blue > 255 ? 255 : blue;
    return (uint16_t)(((red & 0xf8) << 8) |
                      ((green & 0xfc) << 3) | (blue >> 3));
}

static int compare_area(
    const y6u5v5_frame_t *frame,
    const y6u5v5_rgb565_color_tables_t *color,
    const CompactYuv420Plane *y_plane,
    const CompactYuv420Plane *u_plane,
    const CompactYuv420Plane *v_plane,
    int first_x, int width) {
    uint16_t output0[kWidth];
    uint16_t output1[kWidth];
    int source_y;
    for (source_y = 0; source_y < kHeight; source_y += 2) {
        int x;
        memset(output0, 0xa5, sizeof output0);
        memset(output1, 0xa5, sizeof output1);
        CHECK_AT(y6u5v5_rgb565_can_convert_rows2(
                     frame, source_y, first_x, width),
                 "valid tile geometry rejected", first_x, source_y);
        y6u5v5_rgb565_convert_rows2(
            frame, color, source_y, first_x, output0, output1, width);
        for (x = 0; x < width; ++x) {
            CHECK_AT(output0[x] == reference_rgb565(
                         y_plane, u_plane, v_plane, first_x + x, source_y),
                     "first row differs from reference", first_x + x,
                     source_y);
            CHECK_AT(output1[x] == reference_rgb565(
                         y_plane, u_plane, v_plane, first_x + x,
                         source_y + 1),
                     "second row differs from reference", first_x + x,
                     source_y + 1);
        }
    }
    return 0;
}

int main(void) {
    uint8_t source_y[kWidth * kHeight];
    uint8_t source_u[kChromaWidth * kChromaHeight];
    uint8_t source_v[kChromaWidth * kChromaHeight];
    uint8_t packed_y[kWidth * 6 / 8 * kHeight];
    uint8_t packed_u[kChromaWidth * 5 / 8 * kChromaHeight];
    uint8_t packed_v[kChromaWidth * 5 / 8 * kChromaHeight];
    int8_t correction_y[(kWidth / 8) * (kHeight / 8)];
    int8_t correction_u[(kChromaWidth / 8) * (kChromaHeight / 8)];
    int8_t correction_v[(kChromaWidth / 8) * (kChromaHeight / 8)];
    CompactYuv420Plane y_plane = {
        packed_y, kWidth, kHeight, kWidth * 6 / 8,
        correction_y, kWidth / 8, 6};
    CompactYuv420Plane u_plane = {
        packed_u, kChromaWidth, kChromaHeight, kChromaWidth * 5 / 8,
        correction_u, kChromaWidth / 8, 5};
    CompactYuv420Plane v_plane = {
        packed_v, kChromaWidth, kChromaHeight, kChromaWidth * 5 / 8,
        correction_v, kChromaWidth / 8, 5};
    y6u5v5_frame_t frame;
    y6u5v5_rgb565_color_tables_t color;
    int sample;

    for (sample = 0; sample < 256; ++sample) {
        luma[sample] = 298 * (sample > 16 ? sample - 16 : 0);
        red_add[sample] = 409 * (sample - 128) + 128;
        green_u_add[sample] = -100 * (sample - 128);
        green_v_add[sample] = -208 * (sample - 128) + 128;
        blue_add[sample] = 516 * (sample - 128) + 128;
    }
    for (sample = 0; sample < (int)sizeof source_y; ++sample)
        source_y[sample] = (uint8_t)(sample * 37 + sample / 3);
    for (sample = 0; sample < (int)sizeof source_u; ++sample) {
        source_u[sample] = (uint8_t)(sample * 53 + 17);
        source_v[sample] = (uint8_t)(255 - sample * 29);
    }
    compact_yuv420_pack_plane(&y_plane, source_y, kWidth);
    compact_yuv420_pack_plane(&u_plane, source_u, kChromaWidth);
    compact_yuv420_pack_plane(&v_plane, source_v, kChromaWidth);

    frame.y = (y6u5v5_plane_t){
        kWidth, kHeight, y_plane.stride, y_plane.data,
        y_plane.correction_stride, y_plane.correction};
    frame.u = (y6u5v5_plane_t){
        kChromaWidth, kChromaHeight, u_plane.stride, u_plane.data,
        u_plane.correction_stride, u_plane.correction};
    frame.v = (y6u5v5_plane_t){
        kChromaWidth, kChromaHeight, v_plane.stride, v_plane.data,
        v_plane.correction_stride, v_plane.correction};
    color = (y6u5v5_rgb565_color_tables_t){
        luma, red_add, green_u_add, green_v_add, blue_add};

    y6u5v5_rgb565_initialize();
    y6u5v5_rgb565_initialize();
    if (compare_area(
            &frame, &color, &y_plane, &u_plane, &v_plane, 0, kWidth))
        return 1;
    if (compare_area(
            &frame, &color, &y_plane, &u_plane, &v_plane, 16, 16))
        return 1;

    correction_y[0] = 127;
    correction_u[0] = -128;
    correction_v[0] = 64;
    if (compare_area(
            &frame, &color, &y_plane, &u_plane, &v_plane, 0, kWidth))
        return 1;

    CHECK_AT(!y6u5v5_rgb565_can_convert_rows2(&frame, 1, 0, 16),
             "odd source row accepted", 0, 1);
    CHECK_AT(!y6u5v5_rgb565_can_convert_rows2(&frame, 0, 8, 16),
             "unaligned source column accepted", 8, 0);
    CHECK_AT(!y6u5v5_rgb565_can_convert_rows2(&frame, 0, 0, 8),
             "short output width accepted", 0, 0);
    CHECK_AT(!y6u5v5_rgb565_can_convert_rows2(&frame, 0, 16, 32),
             "out-of-range output accepted", 16, 0);
    CHECK_AT(!y6u5v5_rgb565_can_convert_rows2(NULL, 0, 0, 16),
             "null frame accepted", 0, 0);

    puts("y6u5v5_rgb565 tests passed");
    return 0;
}
