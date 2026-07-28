#include <stddef.h>
#include <stdint.h>

#include "esp_attr.h"

typedef void (*color_function_t)(
    int16_t *, int16_t *, int16_t *, int16_t *, uint8_t *);

enum {
    k_yuv420_rgb565le_index = 13,
    k_output_stride = 320,
    k_block_width = 16,
    k_block_height = 16
};

extern uint8_t lips[];
extern void *mjpeg_original_color_array[16];

#ifdef MJPEG_COLOR_TABLES
#define PACK_CONTRIBUTION(green_product, color) \
    (((uint32_t)(green_product) & 0xfffffU) | \
     (((uint32_t)(color) & 0xfffU) << 20))
#define U_CONTRIBUTION(value) \
    PACK_CONTRIBUTION( \
        ((value) - 128) * 0x581, \
        (((value) - 128) * 0x1c5b) >> 12)
#define V_CONTRIBUTION(value) \
    PACK_CONTRIBUTION( \
        ((value) - 128) * 0xb6d, \
        (((value) - 128) * 0x166f) >> 12)
#define CONTRIBUTIONS_16(macro, base) \
    macro((base) + 0), macro((base) + 1), \
    macro((base) + 2), macro((base) + 3), \
    macro((base) + 4), macro((base) + 5), \
    macro((base) + 6), macro((base) + 7), \
    macro((base) + 8), macro((base) + 9), \
    macro((base) + 10), macro((base) + 11), \
    macro((base) + 12), macro((base) + 13), \
    macro((base) + 14), macro((base) + 15)

static const uint32_t k_u_contributions[256] = {
    CONTRIBUTIONS_16(U_CONTRIBUTION, 0),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 16),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 32),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 48),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 64),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 80),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 96),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 112),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 128),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 144),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 160),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 176),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 192),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 208),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 224),
    CONTRIBUTIONS_16(U_CONTRIBUTION, 240)
};

static const uint32_t k_v_contributions[256] = {
    CONTRIBUTIONS_16(V_CONTRIBUTION, 0),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 16),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 32),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 48),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 64),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 80),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 96),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 112),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 128),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 144),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 160),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 176),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 192),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 208),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 224),
    CONTRIBUTIONS_16(V_CONTRIBUTION, 240)
};

#undef CONTRIBUTIONS_16
#undef V_CONTRIBUTION
#undef U_CONTRIBUTION
#undef PACK_CONTRIBUTION

static inline __attribute__((always_inline))
int sign_extend_20(uint32_t value) {
    return (int32_t)(value << 12) >> 12;
}
#endif

static inline __attribute__((always_inline))
uint16_t pack_rgb565(uint16_t y, int red, int green, int blue) {
    const uint8_t r = lips[(y + red) & 0x3ff];
    const uint8_t g = lips[(y - green) & 0x3ff];
    const uint8_t b = lips[(y + blue) & 0x3ff];
    return (uint16_t)(
        ((uint16_t)(r & 0xf8) << 8) |
        ((uint16_t)(g & 0xfc) << 3) |
        (b >> 3));
}

static inline __attribute__((always_inline))
void convert_group(
    int group,
    const int16_t *y0, const int16_t *y1,
    const int16_t *u, const int16_t *v,
    uint16_t *out0, uint16_t *out1) {
    int red;
    int green;
    int blue;
    const int x = group * 2;
#ifdef MJPEG_COLOR_TABLES
    const uint32_t u_contribution =
        k_u_contributions[(uint8_t)(u[group])];
    const uint32_t v_contribution =
        k_v_contributions[(uint8_t)(v[group])];
    red = (int32_t)(v_contribution) >> 20;
    green =
        (sign_extend_20(u_contribution) +
         sign_extend_20(v_contribution)) >> 12;
    blue = (int32_t)(u_contribution) >> 20;
#else
    const int u_delta = (int)(u[group]) - 128;
    const int v_delta = (int)(v[group]) - 128;
    red = (v_delta * 0x166f) >> 12;
    green = (u_delta * 0x581 + v_delta * 0xb6d) >> 12;
    blue = (u_delta * 0x1c5b) >> 12;
#endif
    out0[x] = pack_rgb565((uint16_t)(y0[x]), red, green, blue);
    out0[x + 1] =
        pack_rgb565((uint16_t)(y0[x + 1]), red, green, blue);
    out1[x] = pack_rgb565((uint16_t)(y1[x]), red, green, blue);
    out1[x + 1] =
        pack_rgb565((uint16_t)(y1[x + 1]), red, green, blue);
}

static void IRAM_ATTR fixed_yuv420_to_rgb565le(
    int16_t *y, int16_t *u, int16_t *v,
    int16_t *w_h, uint8_t *rgb_out) {
    uint16_t *output;
    int row;

    if (w_h[0] != k_output_stride ||
        w_h[2] != k_block_width ||
        w_h[3] != k_block_height) {
        ((color_function_t)(
            mjpeg_original_color_array[k_yuv420_rgb565le_index]))(
                y, u, v, w_h, rgb_out);
        return;
    }

    output = (uint16_t *)(rgb_out);
    for (row = 0; row < 8; ++row) {
        const int16_t *y0 = y + row * 32;
        const int16_t *y1 = y0 + 16;
        const int16_t *row_u = u + row * 8;
        const int16_t *row_v = v + row * 8;
        uint16_t *out0 = output + row * (k_output_stride * 2);
        uint16_t *out1 = out0 + k_output_stride;
        convert_group(0, y0, y1, row_u, row_v, out0, out1);
        convert_group(1, y0, y1, row_u, row_v, out0, out1);
        convert_group(2, y0, y1, row_u, row_v, out0, out1);
        convert_group(3, y0, y1, row_u, row_v, out0, out1);
        convert_group(4, y0, y1, row_u, row_v, out0, out1);
        convert_group(5, y0, y1, row_u, row_v, out0, out1);
        convert_group(6, y0, y1, row_u, row_v, out0, out1);
        convert_group(7, y0, y1, row_u, row_v, out0, out1);
    }
}

void *color_array[16] = {0};

void mjpeg_install_fixed_rgb565(void) {
    size_t index;
    for (index = 0; index < 16; ++index) {
        color_array[index] = mjpeg_original_color_array[index];
    }
    color_array[k_yuv420_rgb565le_index] =
        (void *)(&fixed_yuv420_to_rgb565le);
}
