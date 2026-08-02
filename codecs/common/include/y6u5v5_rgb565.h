#ifndef Y6U5V5_RGB565_H
#define Y6U5V5_RGB565_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct y6u5v5_plane {
    unsigned width;
    unsigned height;
    unsigned stride;
    const uint8_t *data;
    unsigned correction_stride;
    const int8_t *correction;
} y6u5v5_plane_t;

typedef struct y6u5v5_frame {
    y6u5v5_plane_t y;
    y6u5v5_plane_t u;
    y6u5v5_plane_t v;
} y6u5v5_frame_t;

typedef struct y6u5v5_rgb565_color_tables {
    const int32_t *luma;
    const int32_t *red_add;
    const int32_t *green_u_add;
    const int32_t *green_v_add;
    const int32_t *blue_add;
} y6u5v5_rgb565_color_tables_t;

/* Initialize the optional Q4 and branch-free RGB565 tables once at boot. */
void y6u5v5_rgb565_initialize(void);

/*
 * The fused kernel operates on aligned 16x2 luma tiles. It returns zero for
 * scaled, unaligned, truncated or otherwise unsupported geometry so callers
 * can retain their existing row converter as an exact fallback.
 */
int y6u5v5_rgb565_can_convert_rows2(
    const y6u5v5_frame_t *frame, int source_y, int first_source_x,
    int output_width);

void y6u5v5_rgb565_convert_rows2(
    const y6u5v5_frame_t *frame,
    const y6u5v5_rgb565_color_tables_t *color,
    int source_y, int first_source_x,
    uint16_t *output0, uint16_t *output1, int output_width);

#ifdef __cplusplus
}
#endif

#endif
