/*
 * Reference HLV-1 decoder.
 *
 * Decoding is single-pass and raster ordered.  It keeps one previous YUV420
 * reference, reconstructs the current frame in place, and exposes operation
 * counters used to enforce the 100 MHz playback budget.  No floating point,
 * future frames, or encoder-side search is required.
 */
#include "hlv1_internal.h"

#ifndef HLV1_ENABLE_DECODER_STATS
#define HLV1_ENABLE_DECODER_STATS 1
#endif

#ifndef HLV1_ENABLE_STAGE_PROFILE
#define HLV1_ENABLE_STAGE_PROFILE 0
#endif

#if HLV1_ENABLE_STAGE_PROFILE
#include "esp_cpu.h"
#define HLV1_PROFILE_FIELD HLV1StageProfile profile;
#define HLV1_PROFILE_NOW() esp_cpu_get_cycle_count()
#define HLV1_PROFILE_ADD(d, field, start) \
    ((d)->profile.field += (uint32_t)(HLV1_PROFILE_NOW() - (start)))
#define HLV1_PROFILE_POINTER(d) (&(d)->profile)
#else
#define HLV1_PROFILE_FIELD
#define HLV1_PROFILE_NOW() 0U
#define HLV1_PROFILE_ADD(d, field, start) \
    do { (void)(d); (void)(start); } while (0)
#define HLV1_PROFILE_POINTER(d) NULL
#endif

#if HLV1_ENABLE_DECODER_STATS
#define HLV1_STATS_FIELD HLV1Stats stats;
#define HLV1_STATS_PARAMETER HLV1Stats *stats,
#define HLV1_STATS_ARGUMENT(d) &(d)->stats,
#define HLV1_STAT_ADD(d, field, value) ((d)->stats.field += (value))
#define HLV1_PRED_STAT_ADD(stats, field, value) \
    do { if (stats) (stats)->field += (value); } while (0)
#else
#define HLV1_STATS_FIELD
#define HLV1_STATS_PARAMETER
#define HLV1_STATS_ARGUMENT(d)
#define HLV1_STAT_ADD(d, field, value) ((void)0)
#define HLV1_PRED_STAT_ADD(stats, field, value) ((void)0)
#endif

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
static void trace_decoder_heap(const char *stage) {
    printf("HLV decoder %s: heap=%u, largest=%u\n", stage,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
#else
static void trace_decoder_heap(const char *stage) { (void)stage; }
#endif

/* Must match the encoder coefficient scan exactly. */
static const uint8_t scan4[16] = {
    0, 1, 4, 5, 2, 8, 3, 12, 6, 9, 7, 13, 10, 11, 14, 15
};

/* Motion-vector row state is enough for the median left/top/top-right
 * predictor; the arrays are swapped at each macroblock row. */
struct HLV1Decoder {
    HLV1Header header;
    HLV1Frame previous;
    HLV1Frame current;
    HLV1Frame compact_current;
    int compact_y7_u6_v6;
    int compact_single_reference;
    int compact_ring_luma_rows;
    int compact_committed_luma_rows;
    int compact_committed_chroma_rows;
    HLV1ReferenceRowGuard reference_row_guard;
    void *reference_row_guard_opaque;
    int have_previous;
    HLV1_STATS_FIELD
    HLV1_PROFILE_FIELD
    int mv_cols;
    int16_t *mv_top_x;
    int16_t *mv_top_y;
    int16_t *mv_cur_x;
    int16_t *mv_cur_y;
};

enum {
    HLV1_PLANE_Y = 0,
    HLV1_PLANE_U = 1,
    HLV1_PLANE_V = 2
};

static int compact_frame_alloc_rows(HLV1Frame *f, int width, int height,
                                    int allocated_luma_rows) {
    if (!f || width <= 0 || height <= 0) return HLV1_ERR_ARGUMENT;
    memset(f, 0, sizeof *f);
    f->width = width;
    f->height = height;
    f->padded_width = (width + 15) & ~15;
    f->padded_height = (height + 15) & ~15;
    f->stride_y =
        f->padded_width * HLV1_V14_LUMA_BITS / 8;
    f->stride_u =
        (f->padded_width / 2) * HLV1_V14_CHROMA_BITS / 8;
    f->stride_v = f->stride_u;
    f->storage_mode = HLV1_FRAME_STORAGE_Y7_U6_V6;
    if (!allocated_luma_rows)
        allocated_luma_rows = f->padded_height;
    size_t y_size = (size_t)f->stride_y * allocated_luma_rows;
    size_t c_size = (size_t)f->stride_u * (allocated_luma_rows / 2);
    f->correction_stride_y = f->padded_width / 8;
    f->correction_stride_u = f->padded_width / 16;
    f->correction_stride_v = f->correction_stride_u;
    size_t y_correction_size =
        (size_t)f->correction_stride_y * (allocated_luma_rows / 8);
    size_t c_correction_size =
        (size_t)f->correction_stride_u * (allocated_luma_rows / 16);
    f->y = (uint8_t *)malloc(y_size);
    f->u = (uint8_t *)malloc(c_size);
    f->v = (uint8_t *)malloc(c_size);
    f->correction_storage =
        (int8_t *)malloc(y_correction_size + 2 * c_correction_size);
    if (f->correction_storage) {
        f->correction_y = f->correction_storage;
        f->correction_u = f->correction_y + y_correction_size;
        f->correction_v = f->correction_u + c_correction_size;
    }
    f->storage = f->y;
    if (!f->y || !f->u || !f->v || !f->correction_storage) {
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
        printf("HLV compact allocation failed:"
               " y=%p/%u u=%p/%u v=%p/%u q4=%p/%u"
               " heap=%u largest=%u\n",
               (void *)f->y, (unsigned)y_size,
               (void *)f->u, (unsigned)c_size,
               (void *)f->v, (unsigned)c_size,
               (void *)f->correction_storage,
               (unsigned)(y_correction_size + 2 * c_correction_size),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_largest_free_block(
                   MALLOC_CAP_8BIT));
#endif
        hlv1_frame_free(f);
        return HLV1_ERR_MEMORY;
    }
    memset(f->y, 0, y_size);
    memset(f->u, 0, c_size);
    memset(f->v, 0, c_size);
    memset(f->correction_storage, 0,
           y_correction_size + 2 * c_correction_size);
    return HLV1_OK;
}

static int compact_frame_alloc(HLV1Frame *f, int width, int height) {
    return compact_frame_alloc_rows(f, width, height, 0);
}

static int compact_current_luma_y(const HLV1Decoder *d, int y) {
    return d->compact_single_reference
               ? y & (d->compact_ring_luma_rows - 1)
               : y;
}

static int compact_current_chroma_y(const HLV1Decoder *d, int y) {
    return d->compact_single_reference
               ? y & ((d->compact_ring_luma_rows / 2) - 1)
               : y;
}

static uint8_t *compact_current_plane_row(HLV1Decoder *d, int plane, int y) {
    HLV1Frame *f = &d->compact_current;
    if (plane == HLV1_PLANE_Y)
        return f->y + compact_current_luma_y(d, y) * f->stride_y;
    y = compact_current_chroma_y(d, y);
    return (plane == HLV1_PLANE_U ? f->u : f->v) +
           y * (plane == HLV1_PLANE_U ? f->stride_u : f->stride_v);
}

static int8_t *compact_current_correction_row(HLV1Decoder *d, int plane,
                                               int y) {
    HLV1Frame *f = &d->compact_current;
    if (plane == HLV1_PLANE_Y) {
        y = compact_current_luma_y(d, y);
        return f->correction_y + (y / 8) * f->correction_stride_y;
    }
    y = compact_current_chroma_y(d, y);
    return (plane == HLV1_PLANE_U ? f->correction_u : f->correction_v) +
           (y / 8) *
               (plane == HLV1_PLANE_U
                    ? f->correction_stride_u
                    : f->correction_stride_v);
}

static uint8_t compact_current_sample(HLV1Decoder *d, int plane,
                                      int x, int y) {
    HLV1Frame *f = &d->compact_current;
    unsigned bits =
        plane == HLV1_PLANE_Y
            ? HLV1_V14_LUMA_BITS
            : HLV1_V14_CHROMA_BITS;
    int mapped_y =
        plane == HLV1_PLANE_Y
            ? compact_current_luma_y(d, y)
            : compact_current_chroma_y(d, y);
    const uint8_t *row = compact_current_plane_row(d, plane, y);
    const int8_t *correction =
        plane == HLV1_PLANE_Y
            ? f->correction_y
            : (plane == HLV1_PLANE_U
                   ? f->correction_u
                   : f->correction_v);
    int correction_stride =
        plane == HLV1_PLANE_Y
            ? f->correction_stride_y
            : (plane == HLV1_PLANE_U
                   ? f->correction_stride_u
                   : f->correction_stride_v);
    int value = hlv1_frame_packed_sample(row, x, bits) +
                hlv1_frame_compact_correction(
                    correction, correction_stride, x, mapped_y);
    return (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
}

static void compact_single_reference_commit(HLV1Decoder *d,
                                             int end_luma_y) {
    uint32_t profile_start = HLV1_PROFILE_NOW();
    HLV1Frame *previous = &d->previous;
    int start_luma_y = d->compact_committed_luma_rows;
    if (end_luma_y > previous->padded_height)
        end_luma_y = previous->padded_height;
    end_luma_y &= ~7;
    if (end_luma_y <= start_luma_y)
        return;
    if (d->reference_row_guard)
        d->reference_row_guard(
            d->reference_row_guard_opaque,
            start_luma_y, end_luma_y - start_luma_y);

    for (int y = start_luma_y; y < end_luma_y; ++y)
        memcpy(previous->y + y * previous->stride_y,
               compact_current_plane_row(d, HLV1_PLANE_Y, y),
               (size_t)previous->stride_y);
    for (int y = start_luma_y; y < end_luma_y; y += 8)
        memcpy(previous->correction_y +
                   (y / 8) * previous->correction_stride_y,
               compact_current_correction_row(
                   d, HLV1_PLANE_Y, y),
               (size_t)previous->correction_stride_y);
    d->compact_committed_luma_rows = end_luma_y;

    /* One chroma correction covers eight chroma rows (16 luma rows).
     * Replace chroma only when that complete correction tile is safe. */
    int end_chroma_y = (end_luma_y / 16) * 8;
    int start_chroma_y = d->compact_committed_chroma_rows;
    for (int y = start_chroma_y; y < end_chroma_y; ++y) {
        memcpy(previous->u + y * previous->stride_u,
               compact_current_plane_row(d, HLV1_PLANE_U, y),
               (size_t)previous->stride_u);
        memcpy(previous->v + y * previous->stride_v,
               compact_current_plane_row(d, HLV1_PLANE_V, y),
               (size_t)previous->stride_v);
    }
    for (int y = start_chroma_y; y < end_chroma_y; y += 8) {
        memcpy(previous->correction_u +
                   (y / 8) * previous->correction_stride_u,
               compact_current_correction_row(
                   d, HLV1_PLANE_U, y),
               (size_t)previous->correction_stride_u);
        memcpy(previous->correction_v +
                   (y / 8) * previous->correction_stride_v,
               compact_current_correction_row(
                   d, HLV1_PLANE_V, y),
               (size_t)previous->correction_stride_v);
    }
    d->compact_committed_chroma_rows = end_chroma_y;
    HLV1_PROFILE_ADD(
        d, reference_commit_cycles, profile_start);
}

static int motion_within_declared_radius(const HLV1Decoder *d,
                                         int mvx, int mvy,
                                         int denominator) {
    if (!d->compact_single_reference)
        return 1;
    int limit = d->header.search_radius * denominator;
    return mvx >= -limit && mvx <= limit &&
           mvy >= -limit && mvy <= limit;
}

static uint8_t *current_plane_ptr(HLV1Decoder *d, int plane,
                                  int x, int y) {
    HLV1Frame *f = &d->current;
    uint8_t *base;
    int stride;
    int rows;
    if (plane == HLV1_PLANE_Y) {
        base = f->y;
        stride = f->stride_y;
        rows = 16;
    } else if (plane == HLV1_PLANE_U) {
        base = f->u;
        stride = f->stride_u;
        rows = 8;
    } else {
        base = f->v;
        stride = f->stride_v;
        rows = 8;
    }
    unsigned row = d->compact_y7_u6_v6
                       ? (unsigned)y & (unsigned)(rows - 1)
                       : (unsigned)y;
    return base + row * (unsigned)stride + x;
}

static uint8_t compact_quantize_code(uint8_t *value, unsigned shift,
                                     unsigned maximum) {
    uint8_t code = compact_yuv420_quantize_code(*value, 8U - shift);
    if (code > maximum) code = (uint8_t)maximum;
    *value = (uint8_t)(code << shift);
    return code;
}

static int8_t compact_error_q4(int sum) {
    return compact_yuv420_error_q4(sum);
}

static void compact_store_luma8_y7(uint8_t *dst, uint8_t *src,
                                   int *error_sum) {
    unsigned code[8];
    for (int i = 0; i < 8; ++i) {
        unsigned quantized = ((unsigned)src[i] + 1U) >> 1;
        quantized -= quantized >> 7;
        code[i] = quantized;
        *error_sum += (int)src[i] - (int)(quantized << 1);
        src[i] = (uint8_t)(quantized << 1);
    }
    dst[0] = (uint8_t)(code[0] | code[1] << 7);
    dst[1] = (uint8_t)(code[1] >> 1 | code[2] << 6);
    dst[2] = (uint8_t)(code[2] >> 2 | code[3] << 5);
    dst[3] = (uint8_t)(code[3] >> 3 | code[4] << 4);
    dst[4] = (uint8_t)(code[4] >> 4 | code[5] << 3);
    dst[5] = (uint8_t)(code[5] >> 5 | code[6] << 2);
    dst[6] = (uint8_t)(code[6] >> 6 | code[7] << 1);
}

static void compact_store_luma16(uint8_t *dst, uint8_t *src,
                                 int error_sum[2]) {
    compact_store_luma8_y7(dst, src, &error_sum[0]);
    compact_store_luma8_y7(
        dst + HLV1_V14_LUMA_BITS, src + 8, &error_sum[1]);
}

static void compact_store_chroma8(uint8_t *dst, uint8_t *src,
                                  int *error_sum) {
    compact_yuv420_pack_aligned_samples(
        dst, src, 8, HLV1_V14_CHROMA_BITS, error_sum, src);
}

static void compact_store_macroblock(HLV1Decoder *d, int mb_x, int mb_y) {
    uint32_t profile_start = HLV1_PROFILE_NOW();
    HLV1Frame *unpacked = &d->current;
    HLV1Frame *packed = &d->compact_current;
    int error_sum[6] = {0, 0, 0, 0, 0, 0};
    uint8_t *luma_src = unpacked->y + mb_x;
    uint8_t *luma_dst =
        compact_current_plane_row(d, HLV1_PLANE_Y, mb_y) +
        mb_x * HLV1_V14_LUMA_BITS / 8;
    for (int y = 0; y < 16; ++y) {
        compact_store_luma16(
            luma_dst, luma_src, &error_sum[(y >> 3) << 1]);
        luma_src += unpacked->stride_y;
        luma_dst += packed->stride_y;
    }

    int chroma_x = mb_x >> 1;
    int chroma_y = mb_y >> 1;
    int chroma_byte = chroma_x * HLV1_V14_CHROMA_BITS / 8;
    uint8_t *u_src = unpacked->u + chroma_x;
    uint8_t *v_src = unpacked->v + chroma_x;
    uint8_t *u_dst =
        compact_current_plane_row(d, HLV1_PLANE_U, chroma_y) +
        chroma_byte;
    uint8_t *v_dst =
        compact_current_plane_row(d, HLV1_PLANE_V, chroma_y) +
        chroma_byte;
    for (int y = 0; y < 8; ++y) {
        compact_store_chroma8(u_dst, u_src, &error_sum[4]);
        compact_store_chroma8(v_dst, v_src, &error_sum[5]);
        u_src += unpacked->stride_u;
        v_src += unpacked->stride_v;
        u_dst += packed->stride_u;
        v_dst += packed->stride_v;
    }
    int y_tile_x = mb_x / 8;
    int y_tile_y = mb_y / 8;
    for (int row = 0; row < 2; ++row)
        for (int column = 0; column < 2; ++column)
            compact_current_correction_row(
                d, HLV1_PLANE_Y, (y_tile_y + row) * 8)
                [y_tile_x + column] =
                compact_error_q4(error_sum[row * 2 + column]);
    int chroma_x_tile = mb_x / 16;
    compact_current_correction_row(
        d, HLV1_PLANE_U, chroma_y)[chroma_x_tile] =
        compact_error_q4(error_sum[4]);
    compact_current_correction_row(
        d, HLV1_PLANE_V, chroma_y)[chroma_x_tile] =
        compact_error_q4(error_sum[5]);
    HLV1_PROFILE_ADD(d, packing_cycles, profile_start);
}

/* A zero-motion SKIP already has exactly the representation required by the
 * next compact reference frame.  Preserve those packed bits instead of
 * quantizing and packing the unpacked predictor again. */
static void compact_copy_macroblock(HLV1Decoder *d, int mb_x, int mb_y) {
    uint32_t profile_start = HLV1_PROFILE_NOW();
    const HLV1Frame *src = &d->previous;
    HLV1Frame *dst = &d->compact_current;
    int y_byte = mb_x * HLV1_V14_LUMA_BITS / 8;
    const uint8_t *y_src = src->y + mb_y * src->stride_y + y_byte;
    uint8_t *y_dst =
        compact_current_plane_row(d, HLV1_PLANE_Y, mb_y) + y_byte;
    for (int y = 0; y < 16; ++y) {
        memcpy(y_dst, y_src, 16 * HLV1_V14_LUMA_BITS / 8);
        y_src += src->stride_y;
        y_dst += dst->stride_y;
    }

    int chroma_x = mb_x >> 1;
    int chroma_y = mb_y >> 1;
    int chroma_byte = chroma_x * HLV1_V14_CHROMA_BITS / 8;
    const uint8_t *u_src =
        src->u + chroma_y * src->stride_u + chroma_byte;
    const uint8_t *v_src =
        src->v + chroma_y * src->stride_v + chroma_byte;
    uint8_t *u_dst =
        compact_current_plane_row(d, HLV1_PLANE_U, chroma_y) +
        chroma_byte;
    uint8_t *v_dst =
        compact_current_plane_row(d, HLV1_PLANE_V, chroma_y) +
        chroma_byte;
    for (int y = 0; y < 8; ++y) {
        memcpy(u_dst, u_src, 8 * HLV1_V14_CHROMA_BITS / 8);
        memcpy(v_dst, v_src, 8 * HLV1_V14_CHROMA_BITS / 8);
        u_src += src->stride_u;
        v_src += src->stride_v;
        u_dst += dst->stride_u;
        v_dst += dst->stride_v;
    }
    int y_tile_x = mb_x / 8;
    int y_tile_y = mb_y / 8;
    for (int row = 0; row < 2; ++row) {
        memcpy(
            compact_current_correction_row(
                d, HLV1_PLANE_Y, (y_tile_y + row) * 8) + y_tile_x,
            src->correction_y +
                (y_tile_y + row) * src->correction_stride_y + y_tile_x,
            2);
    }
    int chroma_index =
        (mb_y / 16) * src->correction_stride_u + mb_x / 16;
    int chroma_x_tile = mb_x / 16;
    compact_current_correction_row(
        d, HLV1_PLANE_U, chroma_y)[chroma_x_tile] =
        src->correction_u[chroma_index];
    compact_current_correction_row(
        d, HLV1_PLANE_V, chroma_y)[chroma_x_tile] =
        src->correction_v[chroma_index];
    HLV1_PROFILE_ADD(d, packing_cycles, profile_start);
}

static void compact_fill_macroblock(HLV1Decoder *d, int mb_x, int mb_y,
                                    const uint8_t means[3]) {
    uint32_t profile_start = HLV1_PROFILE_NOW();
    HLV1Frame *packed = &d->compact_current;
    uint8_t values[3] = {means[0], means[1], means[2]};
    uint8_t y_code = compact_quantize_code(&values[0], 1, 127);
    uint8_t u_code = compact_quantize_code(&values[1], 2, 63);
    uint8_t v_code = compact_quantize_code(&values[2], 2, 63);
    uint8_t y_pattern[14] = {0};
    unsigned y_bit = 0;
    for (int sample = 0; sample < 16; ++sample)
        for (unsigned b = 0; b < HLV1_V14_LUMA_BITS; ++b, ++y_bit)
            y_pattern[y_bit >> 3] |=
                (uint8_t)(((y_code >> b) & 1U) << (y_bit & 7U));
    uint8_t *y_dst =
        compact_current_plane_row(d, HLV1_PLANE_Y, mb_y) +
        mb_x * HLV1_V14_LUMA_BITS / 8;
    for (int y = 0; y < 16; ++y) {
        memcpy(y_dst, y_pattern, sizeof y_pattern);
        y_dst += packed->stride_y;
    }

    int chroma_x = mb_x >> 1;
    int chroma_y = mb_y >> 1;
    int chroma_byte = chroma_x * HLV1_V14_CHROMA_BITS / 8;
    uint8_t codes[2] = {u_code, v_code};
    uint8_t *dst[2] = {
        compact_current_plane_row(d, HLV1_PLANE_U, chroma_y) +
            chroma_byte,
        compact_current_plane_row(d, HLV1_PLANE_V, chroma_y) +
            chroma_byte
    };
    for (int plane = 0; plane < 2; ++plane) {
        uint8_t code = codes[plane];
        uint8_t pattern[8 * HLV1_V14_CHROMA_BITS / 8] = {0};
        unsigned bit = 0;
        for (int sample = 0; sample < 8; ++sample)
            for (unsigned b = 0; b < HLV1_V14_CHROMA_BITS; ++b, ++bit)
                pattern[bit >> 3] |=
                    (uint8_t)(((code >> b) & 1U) << (bit & 7U));
        uint8_t *row = dst[plane];
        for (int y = 0; y < 8; ++y) {
            memcpy(row, pattern, sizeof pattern);
            row += packed->stride_u;
        }
    }
    int y_tile_x = mb_x / 8;
    int y_tile_y = mb_y / 8;
    int8_t y_correction =
        (int8_t)(((int)means[0] - values[0]) * 16);
    for (int row = 0; row < 2; ++row)
        for (int column = 0; column < 2; ++column)
            compact_current_correction_row(
                d, HLV1_PLANE_Y, (y_tile_y + row) * 8)
                [y_tile_x + column] = y_correction;
    int correction_x = mb_x / 16;
    compact_current_correction_row(
        d, HLV1_PLANE_U, chroma_y)[correction_x] =
        (int8_t)(((int)means[1] - values[1]) * 16);
    compact_current_correction_row(
        d, HLV1_PLANE_V, chroma_y)[correction_x] =
        (int8_t)(((int)means[2] - values[2]) * 16);
    HLV1_STAT_ADD(d, fill_samples, 384);
    HLV1_PROFILE_ADD(d, packing_cycles, profile_start);
}

/* --- Small deterministic arithmetic helpers --------------------------- */
static int median3(int a, int b, int c) {
    if (a > b) { int t = a; a = b; b = t; }
    if (b > c) { int t = b; b = c; c = t; }
    if (a > b) b = a;
    return b;
}

static void motion_vector_predictor(const int16_t *top_x,
                                    const int16_t *top_y,
                                    const int16_t *cur_x,
                                    const int16_t *cur_y,
                                    int column, int columns,
                                    int fallback_x, int fallback_y,
                                    int *predict_x, int *predict_y) {
    int left_x = column > 0 ? cur_x[column - 1] : fallback_x;
    int left_y = column > 0 ? cur_y[column - 1] : fallback_y;
    int topv_x = top_x ? top_x[column] : fallback_x;
    int topv_y = top_y ? top_y[column] : fallback_y;
    int right_x = top_x && column + 1 < columns ? top_x[column + 1] : fallback_x;
    int right_y = top_y && column + 1 < columns ? top_y[column + 1] : fallback_y;
    *predict_x = median3(left_x, topv_x, right_x);
    *predict_y = median3(left_y, topv_y, right_y);
}

static int rounded_mean_even(uint32_t sum, unsigned count) {
    uint32_t q = sum / count;
    uint32_t r = sum % count;
    uint32_t twice = r * 2U;
    if (twice > count || (twice == count && (q & 1U))) ++q;
    return (int)q;
}

/* --- Block copy, fill, and fractional prediction ----------------------- */
static void copy_block(uint8_t *dst, int ds, const uint8_t *src, int ss,
                       int w, int h) {
    for (int y = 0; y < h; ++y)
        memcpy(dst + y * ds, src + y * ss, (size_t)w);
}

static void fill_block(uint8_t *dst, int stride, int w, int h, uint8_t value) {
    for (int y = 0; y < h; ++y)
        memset(dst + y * stride, value, (size_t)w);
}

static int floor_div(int value, int divisor) {
    int q = value / divisor;
    int r = value % divisor;
    if (r < 0) --q;
    return q;
}

static void predict_plane_fractional(HLV1_STATS_PARAMETER
                                     uint8_t *dst, int dst_stride,
                                     int w, int h,
                                     const HLV1Frame *src_frame,
                                     int src_plane,
                                     const uint8_t *src, int src_stride,
                                     int origin_x_num, int origin_y_num,
                                     int denominator) {
    int bx = floor_div(origin_x_num, denominator);
    int by = floor_div(origin_y_num, denominator);
    int fx = origin_x_num - bx * denominator;
    int fy = origin_y_num - by * denominator;
    if (src_frame->storage_mode == HLV1_FRAME_STORAGE_Y7_U6_V6) {
        if (!fx && !fy)
            HLV1_PRED_STAT_ADD(stats, copied_samples, (uint64_t)w * h);
        else if (!fx || !fy)
            HLV1_PRED_STAT_ADD(stats, interpolated_hv_samples,
                               (uint64_t)w * h);
        else
            HLV1_PRED_STAT_ADD(stats, interpolated_bilinear_samples,
                               (uint64_t)w * h);
        const uint8_t *packed_base;
        const int8_t *correction_base;
        int packed_stride;
        int correction_stride;
        unsigned packed_bits;
        if (src_plane == HLV1_PLANE_Y) {
            packed_base = src_frame->y;
            correction_base = src_frame->correction_y;
            packed_stride = src_frame->stride_y;
            correction_stride = src_frame->correction_stride_y;
            packed_bits = HLV1_V14_LUMA_BITS;
        } else if (src_plane == HLV1_PLANE_U) {
            packed_base = src_frame->u;
            correction_base = src_frame->correction_u;
            packed_stride = src_frame->stride_u;
            correction_stride = src_frame->correction_stride_u;
            packed_bits = HLV1_V14_CHROMA_BITS;
        } else {
            packed_base = src_frame->v;
            correction_base = src_frame->correction_v;
            packed_stride = src_frame->stride_v;
            correction_stride = src_frame->correction_stride_v;
            packed_bits = HLV1_V14_CHROMA_BITS;
        }
        if (!fx && !fy) {
            for (int yy = 0; yy < h; ++yy) {
                hlv1_frame_unpack_corrected_samples(
                    packed_base + (by + yy) * packed_stride,
                    bx, by + yy, packed_bits,
                    correction_base, correction_stride,
                    dst + yy * dst_stride, w);
            }
            return;
        }
        int inv_x = denominator - fx;
        int inv_y = denominator - fy;
        unsigned denominator_shift = denominator == 4 ? 2U : 1U;
        /* The aligned unpacker writes complete eight-sample groups.  Keep one
           padded group after the maximum 17 samples so compiler vectorisation
           and future aligned tails remain within the scratch arrays. */
        uint8_t top_samples[24];
        uint8_t bottom_samples[24];
        int row_samples = w + !!fx;
        if (!fy) {
            int round = denominator >> 1;
            for (int yy = 0; yy < h; ++yy) {
                uint8_t *out = dst + yy * dst_stride;
                hlv1_frame_unpack_corrected_samples(
                    packed_base + (by + yy) * packed_stride,
                    bx, by + yy, packed_bits,
                    correction_base, correction_stride,
                    top_samples, row_samples);
                for (int xx = 0; xx < w; ++xx) {
                    out[xx] = (uint8_t)(
                        (top_samples[xx] * inv_x +
                         top_samples[xx + 1] * fx + round) >>
                        denominator_shift);
                }
            }
            return;
        }
        if (!fx) {
            int round = denominator >> 1;
            uint8_t *top = top_samples;
            uint8_t *bottom = bottom_samples;
            hlv1_frame_unpack_corrected_samples(
                packed_base + by * packed_stride,
                bx, by, packed_bits,
                correction_base, correction_stride,
                top, w);
            for (int yy = 0; yy < h; ++yy) {
                uint8_t *out = dst + yy * dst_stride;
                hlv1_frame_unpack_corrected_samples(
                    packed_base + (by + yy + 1) * packed_stride,
                    bx, by + yy + 1, packed_bits,
                    correction_base, correction_stride,
                    bottom, w);
                for (int xx = 0; xx < w; ++xx) {
                    out[xx] = (uint8_t)(
                        (top[xx] * inv_y +
                         bottom[xx] * fy + round) >>
                        denominator_shift);
                }
                uint8_t *swap = top;
                top = bottom;
                bottom = swap;
            }
            return;
        }
        int round = (denominator * denominator) >> 1;
        unsigned bilinear_shift = denominator_shift * 2U;
        uint8_t *top = top_samples;
        uint8_t *bottom = bottom_samples;
        hlv1_frame_unpack_corrected_samples(
            packed_base + by * packed_stride,
            bx, by, packed_bits,
            correction_base, correction_stride,
            top, row_samples);
        for (int yy = 0; yy < h; ++yy) {
            uint8_t *out = dst + yy * dst_stride;
            hlv1_frame_unpack_corrected_samples(
                packed_base + (by + yy + 1) * packed_stride,
                bx, by + yy + 1, packed_bits,
                correction_base, correction_stride,
                bottom, row_samples);
            for (int xx = 0; xx < w; ++xx) {
                int top_value =
                    top[xx] * inv_x + top[xx + 1] * fx;
                int bottom_value =
                    bottom[xx] * inv_x + bottom[xx + 1] * fx;
                out[xx] = (uint8_t)(
                    (top_value * inv_y + bottom_value * fy + round) >>
                    bilinear_shift);
            }
            uint8_t *swap = top;
            top = bottom;
            bottom = swap;
        }
        return;
    }
    if (!fx && !fy) {
        HLV1_PRED_STAT_ADD(stats, copied_samples, (uint64_t)w * h);
        copy_block(dst, dst_stride, src + by * src_stride + bx,
                   src_stride, w, h);
        return;
    }
    if (!fy) {
        HLV1_PRED_STAT_ADD(stats, interpolated_hv_samples, (uint64_t)w * h);
        int inv_x = denominator - fx;
        int round = denominator / 2;
        unsigned denominator_shift = denominator == 4 ? 2U : 1U;
        for (int yy = 0; yy < h; ++yy) {
            const uint8_t *row = src + (by + yy) * src_stride + bx;
            uint8_t *out = dst + yy * dst_stride;
            for (int xx = 0; xx < w; ++xx)
                out[xx] = (uint8_t)((row[xx] * inv_x +
                                    row[xx + 1] * fx + round) >>
                                    denominator_shift);
        }
        return;
    }
    if (!fx) {
        HLV1_PRED_STAT_ADD(stats, interpolated_hv_samples, (uint64_t)w * h);
        int inv_y = denominator - fy;
        int round = denominator / 2;
        unsigned denominator_shift = denominator == 4 ? 2U : 1U;
        for (int yy = 0; yy < h; ++yy) {
            const uint8_t *row = src + (by + yy) * src_stride + bx;
            const uint8_t *next = row + src_stride;
            uint8_t *out = dst + yy * dst_stride;
            for (int xx = 0; xx < w; ++xx)
                out[xx] = (uint8_t)((row[xx] * inv_y +
                                    next[xx] * fy + round) >>
                                    denominator_shift);
        }
        return;
    }
    HLV1_PRED_STAT_ADD(stats, interpolated_bilinear_samples, (uint64_t)w * h);
    int inv_x = denominator - fx;
    int inv_y = denominator - fy;
    int round = denominator * denominator / 2;
    unsigned denominator_shift = denominator == 4 ? 4U : 2U;
    for (int yy = 0; yy < h; ++yy) {
        const uint8_t *row = src + (by + yy) * src_stride + bx;
        const uint8_t *next = row + src_stride;
        uint8_t *out = dst + yy * dst_stride;
        for (int xx = 0; xx < w; ++xx) {
            int top = row[xx] * inv_x + row[xx + 1] * fx;
            int bottom = next[xx] * inv_x + next[xx + 1] * fx;
            out[xx] = (uint8_t)((top * inv_y + bottom * fy + round) >>
                                denominator_shift);
        }
    }
}

static int motion_valid(const HLV1Frame *ref, int x, int y, int size,
                        int mvx, int mvy, int denominator) {
    int min_x = x * denominator + mvx;
    int min_y = y * denominator + mvy;
    int max_x = (x + size - 1) * denominator + mvx;
    int max_y = (y + size - 1) * denominator + mvy;
    return min_x >= 0 && min_y >= 0 &&
           max_x <= (ref->padded_width - 1) * denominator &&
           max_y <= (ref->padded_height - 1) * denominator;
}

static void predict_motion(HLV1Decoder *d, int x, int y, int mvx, int mvy,
                           int denominator) {
    uint32_t profile_start = HLV1_PROFILE_NOW();
    HLV1Frame *cur = &d->current;
    const HLV1Frame *ref = &d->previous;
    predict_plane_fractional(HLV1_STATS_ARGUMENT(d)
                             current_plane_ptr(d, HLV1_PLANE_Y, x, y),
                             cur->stride_y, 16, 16,
                             ref, HLV1_PLANE_Y, ref->y, ref->stride_y,
                             x * denominator + mvx,
                             y * denominator + mvy, denominator);
    int cx = x / 2, cy = y / 2;
    int cden = denominator * 2;
    predict_plane_fractional(HLV1_STATS_ARGUMENT(d)
                             current_plane_ptr(d, HLV1_PLANE_U, cx, cy),
                             cur->stride_u, 8, 8,
                             ref, HLV1_PLANE_U, ref->u, ref->stride_u,
                             cx * cden + mvx, cy * cden + mvy, cden);
    predict_plane_fractional(HLV1_STATS_ARGUMENT(d)
                             current_plane_ptr(d, HLV1_PLANE_V, cx, cy),
                             cur->stride_v, 8, 8,
                             ref, HLV1_PLANE_V, ref->v, ref->stride_v,
                             cx * cden + mvx, cy * cden + mvy, cden);
    HLV1_PROFILE_ADD(d, prediction_cycles, profile_start);
}

static void predict_motion_sb8(HLV1Decoder *d, int x, int y, int mvx, int mvy,
                               int denominator) {
    uint32_t profile_start = HLV1_PROFILE_NOW();
    HLV1Frame *cur = &d->current;
    const HLV1Frame *ref = &d->previous;
    predict_plane_fractional(HLV1_STATS_ARGUMENT(d)
                             current_plane_ptr(d, HLV1_PLANE_Y, x, y),
                             cur->stride_y, 8, 8,
                             ref, HLV1_PLANE_Y, ref->y, ref->stride_y,
                             x * denominator + mvx,
                             y * denominator + mvy, denominator);
    int cx = x / 2, cy = y / 2;
    int cden = denominator * 2;
    predict_plane_fractional(HLV1_STATS_ARGUMENT(d)
                             current_plane_ptr(d, HLV1_PLANE_U, cx, cy),
                             cur->stride_u, 4, 4,
                             ref, HLV1_PLANE_U, ref->u, ref->stride_u,
                             cx * cden + mvx, cy * cden + mvy, cden);
    predict_plane_fractional(HLV1_STATS_ARGUMENT(d)
                             current_plane_ptr(d, HLV1_PLANE_V, cx, cy),
                             cur->stride_v, 4, 4,
                             ref, HLV1_PLANE_V, ref->v, ref->stride_v,
                             cx * cden + mvx, cy * cden + mvy, cden);
    HLV1_PROFILE_ADD(d, prediction_cycles, profile_start);
}

static void predict_motion_rect(HLV1Decoder *d, int x, int y,
                                int w, int h, int mvx, int mvy,
                                int denominator) {
    uint32_t profile_start = HLV1_PROFILE_NOW();
    HLV1Frame *cur = &d->current;
    const HLV1Frame *ref = &d->previous;
    predict_plane_fractional(HLV1_STATS_ARGUMENT(d)
                             current_plane_ptr(d, HLV1_PLANE_Y, x, y),
                             cur->stride_y, w, h,
                             ref, HLV1_PLANE_Y, ref->y, ref->stride_y,
                             x * denominator + mvx,
                             y * denominator + mvy, denominator);
    int cx = x / 2, cy = y / 2, cden = denominator * 2;
    predict_plane_fractional(HLV1_STATS_ARGUMENT(d)
                             current_plane_ptr(d, HLV1_PLANE_U, cx, cy),
                             cur->stride_u, w / 2, h / 2,
                             ref, HLV1_PLANE_U, ref->u, ref->stride_u,
                             cx * cden + mvx, cy * cden + mvy, cden);
    predict_plane_fractional(HLV1_STATS_ARGUMENT(d)
                             current_plane_ptr(d, HLV1_PLANE_V, cx, cy),
                             cur->stride_v, w / 2, h / 2,
                             ref, HLV1_PLANE_V, ref->v, ref->stride_v,
                             cx * cden + mvx, cy * cden + mvy, cden);
    HLV1_PROFILE_ADD(d, prediction_cycles, profile_start);
}


static int motion_valid_rect(const HLV1Frame *ref, int x, int y,
                             int w, int h, int mvx, int mvy,
                             int denominator) {
    int min_x = x * denominator + mvx;
    int min_y = y * denominator + mvy;
    int max_x = (x + w - 1) * denominator + mvx;
    int max_y = (y + h - 1) * denominator + mvy;
    return min_x >= 0 && min_y >= 0 &&
           max_x <= (ref->padded_width - 1) * denominator &&
           max_y <= (ref->padded_height - 1) * denominator;
}

static int round_div_signed(int value, int divisor) {
    if (value >= 0) return (value + divisor / 2) / divisor;
    return -((-value + divisor / 2) / divisor);
}

static void predict_gradient_plane(uint8_t *dst, int stride, int w, int h,
                                   int base, int dx, int dy) {
    int xterm[16], yterm[16];
    for (int x = 0; x < w; ++x)
        xterm[x] = w > 1 ? round_div_signed(dx * x, w - 1) : 0;
    for (int y = 0; y < h; ++y)
        yterm[y] = h > 1 ? round_div_signed(dy * y, h - 1) : 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            dst[y * stride + x] = hlv1_clip8(base + xterm[x] + yterm[y]);
}

/* --- Predictor syntax -------------------------------------------------- */
static int decode_gradient(HLV1Decoder *d, HLV1BitReader *br, int x, int y) {
    uint8_t base[3];
    int8_t dx[3], dy[3];
    for (int i = 0; i < 3; ++i) {
        base[i] = (uint8_t)hlv1_br_get(br, 8);
        dx[i] = (int8_t)(uint8_t)hlv1_br_get(br, 8);
        dy[i] = (int8_t)(uint8_t)hlv1_br_get(br, 8);
    }
    if (br->error) return br->error;
    uint32_t profile_start = HLV1_PROFILE_NOW();
    HLV1Frame *cur = &d->current;
    predict_gradient_plane(current_plane_ptr(d, HLV1_PLANE_Y, x, y),
                           cur->stride_y,
                           16, 16, base[0], dx[0], dy[0]);
    int cx = x / 2, cy = y / 2;
    predict_gradient_plane(current_plane_ptr(d, HLV1_PLANE_U, cx, cy),
                           cur->stride_u,
                           8, 8, base[1], dx[1], dy[1]);
    predict_gradient_plane(current_plane_ptr(d, HLV1_PLANE_V, cx, cy),
                           cur->stride_v,
                           8, 8, base[2], dx[2], dy[2]);
    HLV1_STAT_ADD(d, gradient_samples, 384);
    HLV1_PROFILE_ADD(d, prediction_cycles, profile_start);
    return HLV1_OK;
}

static int decode_palette(HLV1Decoder *d, HLV1BitReader *br,
                          unsigned version, int x, int y) {
    uint32_t profile_start = HLV1_PROFILE_NOW();
    int count;
    if (version >= HLV1_STREAM_VERSION_13) {
        unsigned count_code = hlv1_br_get(br, 2);
        if (count_code > 2) return HLV1_ERR_BITSTREAM;
        count = 2 << count_code;
    } else {
        count = hlv1_br_get(br, 1) ? 4 : 2;
    }
    if (br->error) return br->error;
    uint8_t colors[8][3] = {{0}};
    for (int i = 0; i < count; ++i) {
        colors[i][0] = (uint8_t)hlv1_br_get(br, 8);
        colors[i][1] = (uint8_t)hlv1_br_get(br, 8);
        colors[i][2] = (uint8_t)hlv1_br_get(br, 8);
    }
    if (br->error) return br->error;
    unsigned index_bits = count == 2 ? 1U : count == 4 ? 2U : 3U;
    for (int yy = 0; yy < 16; ++yy) {
        uint8_t *dst = current_plane_ptr(d, HLV1_PLANE_Y, x, y + yy);
        for (int xx = 0; xx < 16; ++xx) {
            unsigned index = hlv1_br_get(br, index_bits);
            if (br->error || index >= (unsigned)count) return HLV1_ERR_BITSTREAM;
            dst[xx] = colors[index][0];
        }
    }
    int cx = x / 2, cy = y / 2;
    for (int yy = 0; yy < 8; ++yy) {
        uint8_t *du = current_plane_ptr(d, HLV1_PLANE_U, cx, cy + yy);
        uint8_t *dv = current_plane_ptr(d, HLV1_PLANE_V, cx, cy + yy);
        for (int xx = 0; xx < 8; ++xx) {
            unsigned index = hlv1_br_get(br, index_bits);
            if (br->error || index >= (unsigned)count) return HLV1_ERR_BITSTREAM;
            du[xx] = colors[index][1];
            dv[xx] = colors[index][2];
        }
    }
    HLV1_STAT_ADD(d, palette_samples, 384);
    if (count == 2) HLV1_STAT_ADD(d, palette_2, 1);
    else if (count == 4) HLV1_STAT_ADD(d, palette_4, 1);
    else HLV1_STAT_ADD(d, palette_8, 1);
    HLV1_PROFILE_ADD(d, prediction_cycles, profile_start);
    return HLV1_OK;
}

static int read_literal_bytes(HLV1BitReader *br, uint8_t *destination,
                              size_t bytes) {
    return hlv1_br_read_bytes(br, destination, bytes);
}

static int read_literal_unpacked_row(HLV1BitReader *br, uint8_t *destination,
                                     int samples, unsigned sample_bits) {
    uint8_t packed[14];
    size_t bytes = ((size_t)samples * sample_bits + 7U) / 8U;
    int r = read_literal_bytes(br, packed, bytes);
    if (r < 0) return r;
    hlv1_frame_unpack_packed_samples(packed, 0, sample_bits,
                                     destination, samples);
    return HLV1_OK;
}

static int decode_literal(HLV1Decoder *d, HLV1BitReader *br,
                          int x, int y, int *reference_output_ready) {
    if (hlv1_br_get(br, 4) != 0 || br->error)
        return br->error ? br->error : HLV1_ERR_BITSTREAM;
    int r = HLV1_OK;
    if (d->compact_y7_u6_v6) {
        HLV1Frame *packed = &d->compact_current;
        int y_byte = x * HLV1_V14_LUMA_BITS / 8;
        uint8_t *y_dst =
            compact_current_plane_row(d, HLV1_PLANE_Y, y) + y_byte;
        for (int yy = 0; r >= 0 && yy < 16; ++yy) {
            r = read_literal_bytes(
                br, y_dst, 16 * HLV1_V14_LUMA_BITS / 8);
            y_dst += packed->stride_y;
        }
        int cx = x / 2, cy = y / 2;
        int c_byte = cx * HLV1_V14_CHROMA_BITS / 8;
        uint8_t *u_dst =
            compact_current_plane_row(d, HLV1_PLANE_U, cy) + c_byte;
        uint8_t *v_dst =
            compact_current_plane_row(d, HLV1_PLANE_V, cy) + c_byte;
        for (int yy = 0; r >= 0 && yy < 8; ++yy) {
            r = read_literal_bytes(
                br, u_dst, 8 * HLV1_V14_CHROMA_BITS / 8);
            u_dst += packed->stride_u;
        }
        for (int yy = 0; r >= 0 && yy < 8; ++yy) {
            r = read_literal_bytes(
                br, v_dst, 8 * HLV1_V14_CHROMA_BITS / 8);
            v_dst += packed->stride_v;
        }
    } else {
        HLV1Frame *cur = &d->current;
        for (int yy = 0; r >= 0 && yy < 16; ++yy)
            r = read_literal_unpacked_row(
                br, cur->y + (y + yy) * cur->stride_y + x,
                16, HLV1_V14_LUMA_BITS);
        int cx = x / 2, cy = y / 2;
        for (int yy = 0; r >= 0 && yy < 8; ++yy)
            r = read_literal_unpacked_row(
                br, cur->u + (cy + yy) * cur->stride_u + cx,
                8, HLV1_V14_CHROMA_BITS);
        for (int yy = 0; r >= 0 && yy < 8; ++yy)
            r = read_literal_unpacked_row(
                br, cur->v + (cy + yy) * cur->stride_v + cx,
                8, HLV1_V14_CHROMA_BITS);
    }
    int8_t correction[6] = {0};
    for (int i = 0; r >= 0 && i < 6; ++i) {
        correction[i] = (int8_t)hlv1_br_get(br, 8);
        if (br->error) r = br->error;
    }
    if (r >= 0 && d->compact_y7_u6_v6) {
        int y_tile_x = x / 8;
        int y_tile_y = y / 8;
        int index = 0;
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 2; ++column)
                compact_current_correction_row(
                    d, HLV1_PLANE_Y, (y_tile_y + row) * 8)
                    [y_tile_x + column] = correction[index++];
        int correction_x = x / 16;
        compact_current_correction_row(
            d, HLV1_PLANE_U, y / 2)[correction_x] = correction[4];
        compact_current_correction_row(
            d, HLV1_PLANE_V, y / 2)[correction_x] = correction[5];
        *reference_output_ready = 1;
    } else if (r >= 0) {
        HLV1Frame *cur = &d->current;
        int index = 0;
        for (int tile_y = 0; tile_y < 16; tile_y += 8)
            for (int tile_x = 0; tile_x < 16; tile_x += 8)
                hlv1_apply_v14_reference_correction_tile(
                    cur->y + (y + tile_y) * cur->stride_y + x + tile_x,
                    cur->stride_y, x + tile_x, y + tile_y,
                    correction[index++]);
        int cx = x / 2, cy = y / 2;
        hlv1_apply_v14_reference_correction_tile(
            cur->u + cy * cur->stride_u + cx, cur->stride_u,
            cx, cy, correction[4]);
        hlv1_apply_v14_reference_correction_tile(
            cur->v + cy * cur->stride_v + cx, cur->stride_v,
            cx, cy, correction[5]);
        *reference_output_ready = 1;
    }
    if (r >= 0) HLV1_STAT_ADD(d, literal_samples, 384);
    return r;
}

static void predict_fill(HLV1Decoder *d, int x, int y, const uint8_t means[3]) {
    uint32_t profile_start = HLV1_PROFILE_NOW();
    HLV1_STAT_ADD(d, fill_samples, 384);
    HLV1Frame *cur = &d->current;
    fill_block(current_plane_ptr(d, HLV1_PLANE_Y, x, y),
               cur->stride_y, 16, 16, means[0]);
    int cx = x / 2, cy = y / 2;
    fill_block(current_plane_ptr(d, HLV1_PLANE_U, cx, cy),
               cur->stride_u, 8, 8, means[1]);
    fill_block(current_plane_ptr(d, HLV1_PLANE_V, cx, cy),
               cur->stride_v, 8, 8, means[2]);
    HLV1_PROFILE_ADD(d, prediction_cycles, profile_start);
}

static uint8_t intra_dc_plane(HLV1Decoder *d, int plane, int stride,
                              int px, int py, int size) {
    uint32_t sum = 0;
    unsigned count = 0;
    if (d->compact_y7_u6_v6) {
        if (py > 0) {
            for (int i = 0; i < size; ++i)
                sum += compact_current_sample(
                    d, plane, px + i, py - 1);
            count += (unsigned)size;
        }
        if (px > 0) {
            for (int i = 0; i < size; ++i)
                sum += compact_current_sample(
                    d, plane, px - 1, py + i);
            count += (unsigned)size;
        }
        return count ? (uint8_t)rounded_mean_even(sum, count) : 128;
    }
    if (py > 0) {
        const uint8_t *top = current_plane_ptr(d, plane, px, py - 1);
        for (int i = 0; i < size; ++i) sum += top[i];
        count += (unsigned)size;
    }
    if (px > 0) {
        const uint8_t *left = current_plane_ptr(d, plane, px - 1, py);
        for (int i = 0; i < size; ++i) sum += left[i * stride];
        count += (unsigned)size;
    }
    return count ? (uint8_t)rounded_mean_even(sum, count) : 128;
}

static void predict_intra_plane(HLV1Decoder *d, int plane, int stride,
                                int px, int py, int size, int mode) {
    uint8_t *dst = current_plane_ptr(d, plane, px, py);
    if (mode == HLV1_INTRA_DC) {
        fill_block(dst, stride, size, size,
                   intra_dc_plane(d, plane, stride, px, py, size));
        return;
    }
    if (d->compact_y7_u6_v6) {
        if (mode == HLV1_INTRA_HORIZONTAL) {
            for (int y = 0; y < size; ++y) {
                uint8_t value = 128;
                if (px > 0)
                    value = compact_current_sample(
                        d, plane, px - 1, py + y);
                memset(dst + y * stride, value, (size_t)size);
            }
            return;
        }

        /* Reuse the destination's last row as a temporary top-edge cache.
         * The final row is overwritten left-to-right only after each cached
         * sample has been consumed, so compact intra needs no extra stack or
         * heap storage. */
        uint8_t *top = dst + (size - 1) * stride;
        for (int x = 0; x < size; ++x) {
            uint8_t value = 128;
            if (py > 0)
                value = compact_current_sample(
                    d, plane, px + x, py - 1);
            top[x] = value;
        }
        if (mode == HLV1_INTRA_VERTICAL) {
            for (int y = 0; y < size - 1; ++y)
                memcpy(dst + y * stride, top, (size_t)size);
            return;
        }
        for (int y = 0; y < size; ++y) {
            uint8_t left = 128;
            if (px > 0)
                left = compact_current_sample(
                    d, plane, px - 1, py + y);
            for (int x = 0; x < size; ++x)
                dst[y * stride + x] =
                    (uint8_t)(((unsigned)top[x] + left + 1U) >> 1);
        }
        return;
    }
    const uint8_t *top = py > 0
                             ? current_plane_ptr(d, plane, px, py - 1)
                             : NULL;
    const uint8_t *left = px > 0
                              ? current_plane_ptr(d, plane, px - 1, py)
                              : NULL;
    if (mode == HLV1_INTRA_VERTICAL) {
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                dst[y * stride + x] = top ? top[x] : 128;
        return;
    }
    if (mode == HLV1_INTRA_HORIZONTAL) {
        for (int y = 0; y < size; ++y) {
            uint8_t value = left ? left[y * stride] : 128;
            memset(dst + y * stride, value, (size_t)size);
        }
        return;
    }
    for (int y = 0; y < size; ++y) {
        uint8_t lv = left ? left[y * stride] : 128;
        for (int x = 0; x < size; ++x) {
            uint8_t tv = top ? top[x] : 128;
            dst[y * stride + x] =
                (uint8_t)(((unsigned)tv + lv + 1U) >> 1);
        }
    }
}

static void predict_intra(HLV1Decoder *d, int x, int y, int mode) {
    uint32_t profile_start = HLV1_PROFILE_NOW();
    HLV1_STAT_ADD(d, intra_samples, 384);
    HLV1Frame *cur = &d->current;
    predict_intra_plane(d, HLV1_PLANE_Y, cur->stride_y, x, y, 16, mode);
    int cx = x / 2, cy = y / 2;
    predict_intra_plane(d, HLV1_PLANE_U, cur->stride_u, cx, cy, 8, mode);
    predict_intra_plane(d, HLV1_PLANE_V, cur->stride_v, cx, cy, 8, mode);
    HLV1_PROFILE_ADD(d, prediction_cycles, profile_start);
}

/* --- Residual decoding fast paths --------------------------------------
 * Zero and DC-only blocks bypass the general inverse WHT.  These paths are
 * important because they dominate ordinary low-bitrate video. */
static int round_wht_value(int32_t value) {
    return value >= 0 ? (int)((value + 8) / 16)
                      : -(int)((-value + 8) / 16);
}

static int add_dc_only(uint8_t *dst, int stride, int level, int qstep) {
    int dc_step = HLV1_MAX(1, qstep / 2);
    int delta = round_wht_value(level * dc_step);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            dst[y * stride + x] = hlv1_clip8((int)dst[y * stride + x] + delta);
    return HLV1_OK;
}

/* For a separable 4x4 Walsh-Hadamard transform each coefficient contributes
 * the same magnitude to every output, with only its sign changing.  These
 * 16-bit masks store the negative output positions and live in Flash. */
static const uint16_t sparse_wht_negative_mask[16] = {
    0x0000, 0xaaaa, 0xcccc, 0x6666,
    0xf0f0, 0x5a5a, 0x3c3c, 0x9696,
    0xff00, 0x55aa, 0x33cc, 0x9966,
    0x0ff0, 0xa55a, 0xc33c, 0x6996
};

static int add_sparse_wht(uint8_t *dst, int stride, int qstep,
                          uint32_t count,
                          int first_index, int32_t first_level,
                          int second_index, int32_t second_level) {
    int dc_step = HLV1_MAX(1, qstep / 2);
    int32_t first = first_level *
                    (first_index == 0 ? dc_step : qstep);
    int32_t second = count == 2
                         ? second_level *
                               (second_index == 0 ? dc_step : qstep)
                         : 0;
    uint16_t first_mask = sparse_wht_negative_mask[first_index];
    uint16_t second_mask = count == 2
                               ? sparse_wht_negative_mask[second_index]
                               : 0;
    uint16_t output_bit = 1;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x, output_bit <<= 1) {
            int32_t value = first_mask & output_bit ? -first : first;
            if (count == 2)
                value += second_mask & output_bit ? -second : second;
            dst[y * stride + x] = hlv1_clip8(
                (int)dst[y * stride + x] + round_wht_value(value));
        }
    }
    return HLV1_OK;
}

static int get_level_v9(HLV1BitReader *br, int32_t *level) {
    uint32_t first = hlv1_br_get(br, 1);
    if (br->error) return br->error;
    unsigned magnitude;
    if (!first) {
        magnitude = 1;
    } else {
        unsigned quotient = 0;
        while (quotient < 7 && hlv1_br_get(br, 1)) {
            if (br->error) return br->error;
            ++quotient;
        }
        if (br->error) return br->error;
        if (quotient < 7) {
            unsigned remainder = hlv1_br_get(br, 2);
            if (br->error) return br->error;
            magnitude = 2U + (quotient << 2) + remainder;
        } else {
            magnitude = 30U + hlv1_br_get_ue(br);
            if (br->error || magnitude > INT32_MAX)
                return HLV1_ERR_BITSTREAM;
        }
    }
    int negative = (int)hlv1_br_get(br, 1);
    if (br->error) return br->error;
    *level = negative ? -(int32_t)magnitude : (int32_t)magnitude;
    return HLV1_OK;
}

/* The most frequent residual symbol is run=0 followed by level +/-1.  Decode
 * its complete existing VLC in one cache operation and retain the normative
 * readers as the exact fallback for every other symbol and cache boundary. */
static int get_run_level(HLV1BitReader *br, int coeff_mode,
                         uint32_t *run, int32_t *level) {
#if HLV1_FAST_32BIT_BITREADER
    if (coeff_mode == 1 && br->bits >= 3U && br->bits_left >= 3U) {
        uint32_t code = br->cache >> 29;
        if ((code & 6U) == 4U) {
            (void)hlv1_br_get(br, 3);
            *run = 0;
            *level = code & 1U ? -1 : 1;
            return HLV1_OK;
        }
    } else if (!coeff_mode && br->bits >= 4U && br->bits_left >= 4U) {
        uint32_t code = br->cache >> 28;
        if ((code & 14U) == 10U) {
            (void)hlv1_br_get(br, 4);
            *run = 0;
            *level = code & 1U ? -1 : 1;
            return HLV1_OK;
        }
    }
#endif
    *run = hlv1_br_get_ue(br);
    if (coeff_mode == 1) {
        int result = get_level_v9(br, level);
        if (result < 0) return result;
    } else {
        *level = hlv1_br_get_se(br);
    }
    return br->error ? br->error : HLV1_OK;
}

/* Decode one non-zero 4x4 residual, dequantize it, add prediction, and clip
 * in place.  Sparse one- and two-coefficient cases avoid the general inverse
 * transform when the syntax permits a cheaper reconstruction. */
static int decode_nonzero_residual_4x4(HLV1Decoder *d,
                                       HLV1BitReader *br,
                                       uint8_t *dst, int stride,
                                       int qstep, int coeff_mode) {
#if !HLV1_ENABLE_DECODER_STATS
    (void)d;
#endif
    uint32_t count;
    int32_t qcoeff[16];
    int sparse_index[2] = {0, 0};
    int32_t sparse_level[2] = {0, 0};
    int pos = -1;
    int only_dc = 0;
    if (coeff_mode && !hlv1_br_get(br, 1)) {
        int32_t level;
        if (br->error || get_level_v9(br, &level) < 0)
            return br->error ? br->error : HLV1_ERR_BITSTREAM;
        count = 1;
        sparse_level[0] = level;
        only_dc = 1;
        HLV1_STAT_ADD(d, run_zero_symbols, 1);
        if (level == 1 || level == -1)
            HLV1_STAT_ADD(d, unit_level_symbols, 1);
    } else {
        if (br->error) return br->error;
        count = hlv1_br_get_ue(br) + 1U;
        if (br->error || count > 16) return HLV1_ERR_BITSTREAM;
        only_dc = count == 1;
        if (count > 2) memset(qcoeff, 0, sizeof qcoeff);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t run;
            int32_t level;
            int symbol_result =
                get_run_level(br, coeff_mode, &run, &level);
            if (symbol_result < 0) return symbol_result;
            if (run > 16U || pos + (int)run + 1 >= 16)
                return HLV1_ERR_BITSTREAM;
            if (run == 0) HLV1_STAT_ADD(d, run_zero_symbols, 1);
            if (level == 1 || level == -1)
                HLV1_STAT_ADD(d, unit_level_symbols, 1);
            pos += (int)run + 1;
            int idx = scan4[pos];
            if (count <= 2) {
                sparse_index[i] = idx;
                sparse_level[i] = level;
            } else {
                qcoeff[idx] = level;
            }
            if (idx != 0) only_dc = 0;
        }
    }

    HLV1_STAT_ADD(d, coefficient_symbols, count);
    if (count == 1) HLV1_STAT_ADD(d, single_coefficient_blocks, 1);
    else if (count == 2) HLV1_STAT_ADD(d, two_coefficient_blocks, 1);

    if (only_dc) {
        HLV1_STAT_ADD(d, dc_only_blocks, 1);
        uint32_t profile_start = HLV1_PROFILE_NOW();
        int result = add_dc_only(dst, stride, sparse_level[0], qstep);
        HLV1_PROFILE_ADD(d, inverse_wht_cycles, profile_start);
        return result;
    }

    HLV1_STAT_ADD(d, inverse_wht_blocks, 1);
    if (count <= 2) {
        uint32_t profile_start = HLV1_PROFILE_NOW();
        int result = add_sparse_wht(dst, stride, qstep, count,
                                    sparse_index[0], sparse_level[0],
                                    sparse_index[1], sparse_level[1]);
        HLV1_PROFILE_ADD(d, inverse_wht_cycles, profile_start);
        return result;
    }

    uint32_t profile_start = HLV1_PROFILE_NOW();
    int dc_step = HLV1_MAX(1, qstep / 2);
    for (int i = 0; i < 16; ++i)
        qcoeff[i] *= i == 0 ? dc_step : qstep;
    hlv1_wht4_inverse_add(qcoeff, dst, stride);
    HLV1_PROFILE_ADD(d, inverse_wht_cycles, profile_start);
    return HLV1_OK;
}

static int decode_residual_4x4(HLV1Decoder *d, HLV1BitReader *br,
                               uint8_t *dst, int stride, int qstep) {
    HLV1_STAT_ADD(d, residual_blocks, 1);
    if (!hlv1_br_get(br, 1)) {
        if (br->error) return br->error;
        HLV1_STAT_ADD(d, zero_residual_blocks, 1);
        return HLV1_OK;
    }
    return decode_nonzero_residual_4x4(d, br, dst, stride, qstep, 0);
}

static int decode_plane_residual(HLV1Decoder *d, HLV1BitReader *br,
                                 uint8_t *dst, int stride, int w, int h,
                                 int qstep) {
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4) {
            int r = decode_residual_4x4(d, br, dst + by * stride + bx, stride, qstep);
            if (r < 0) return r;
        }
    return HLV1_OK;
}

static int get_residual_mask(HLV1BitReader *br, unsigned version,
                             int block_count, uint32_t *mask,
                             int *coeff_mode) {
    *coeff_mode = 0;
    if (version >= HLV1_STREAM_VERSION_14) {
        *coeff_mode = (int)hlv1_br_get(br, 1);
        *mask = hlv1_br_get(br, (unsigned)block_count);
        return br->error ? br->error : HLV1_OK;
    }
    int first = (int)hlv1_br_get(br, 1);
    if (br->error) return br->error;
    int pivot = block_count - 1;
    if (!first) {
        *mask = hlv1_br_get(br, (unsigned)pivot);
        return br->error ? br->error : HLV1_OK;
    }
    int second = (int)hlv1_br_get(br, 1);
    if (br->error) return br->error;
    if (!second) {
        uint32_t lower = hlv1_br_get(br, (unsigned)pivot);
        if (br->error) return br->error;
        *mask = lower | (UINT32_C(1) << pivot);
        return HLV1_OK;
    }
    if (version >= HLV1_STREAM_VERSION_9) {
        *coeff_mode = (int)hlv1_br_get(br, 1);
        if (br->error) return br->error;
    }
    uint32_t count = hlv1_br_get_ue(br) + 1U;
    if (br->error || count == 0 || count > (uint32_t)block_count)
        return HLV1_ERR_BITSTREAM;
    uint32_t value = 0;
    int pos = -1;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t gap = hlv1_br_get_ue(br);
        if (br->error || gap > (uint32_t)block_count)
            return HLV1_ERR_BITSTREAM;
        pos += (int)gap + 1;
        if (pos < 0 || pos >= block_count ||
            (value & (UINT32_C(1) << pos)))
            return HLV1_ERR_BITSTREAM;
        value |= UINT32_C(1) << pos;
    }
    *mask = value;
    return HLV1_OK;
}

static int decode_plane_residual_masked(HLV1Decoder *d, HLV1BitReader *br,
                                        uint8_t *dst, int stride,
                                        int w, int h, int qstep,
                                        uint32_t mask, int *block_index,
                                        int coeff_mode) {
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4, ++*block_index) {
            HLV1_STAT_ADD(d, residual_blocks, 1);
            if (!(mask & (UINT32_C(1) << *block_index))) {
                HLV1_STAT_ADD(d, zero_residual_blocks, 1);
                continue;
            }
            int r = decode_nonzero_residual_4x4(
                d, br, dst + by * stride + bx, stride, qstep, coeff_mode);
            if (r < 0) return r;
        }
    return HLV1_OK;
}

static int decode_mb_residual_masked(HLV1Decoder *d, HLV1BitReader *br,
                                     unsigned version, int x, int y,
                                     int q_y, int q_uv) {
    uint32_t mask = 0;
    int coeff_mode = 0;
    int r = get_residual_mask(br, version, 24, &mask, &coeff_mode);
    if (r < 0 || !mask) return r < 0 ? r : HLV1_ERR_BITSTREAM;
    HLV1Frame *cur = &d->current;
    int index = 0;
    r = decode_plane_residual_masked(
        d, br, current_plane_ptr(d, HLV1_PLANE_Y, x, y), cur->stride_y,
        16, 16, q_y, mask, &index, coeff_mode);
    if (r < 0) return r;
    int cx = x / 2, cy = y / 2;
    r = decode_plane_residual_masked(
        d, br, current_plane_ptr(d, HLV1_PLANE_U, cx, cy), cur->stride_u,
        8, 8, q_uv, mask, &index, coeff_mode);
    if (r < 0) return r;
    return decode_plane_residual_masked(
        d, br, current_plane_ptr(d, HLV1_PLANE_V, cx, cy), cur->stride_v,
        8, 8, q_uv, mask, &index, coeff_mode);
}

static int decode_sb8_residual_masked(HLV1Decoder *d, HLV1BitReader *br,
                                      unsigned version, int x, int y,
                                      int q_y, int q_uv) {
    uint32_t mask = 0;
    int coeff_mode = 0;
    int r = get_residual_mask(br, version, 6, &mask, &coeff_mode);
    if (r < 0 || !mask) return r < 0 ? r : HLV1_ERR_BITSTREAM;
    HLV1Frame *cur = &d->current;
    int index = 0;
    r = decode_plane_residual_masked(
        d, br, current_plane_ptr(d, HLV1_PLANE_Y, x, y), cur->stride_y,
        8, 8, q_y, mask, &index, coeff_mode);
    if (r < 0) return r;
    int cx = x / 2, cy = y / 2;
    r = decode_plane_residual_masked(
        d, br, current_plane_ptr(d, HLV1_PLANE_U, cx, cy), cur->stride_u,
        4, 4, q_uv, mask, &index, coeff_mode);
    if (r < 0) return r;
    return decode_plane_residual_masked(
        d, br, current_plane_ptr(d, HLV1_PLANE_V, cx, cy), cur->stride_v,
        4, 4, q_uv, mask, &index, coeff_mode);
}

static int decode_mb_residual(HLV1Decoder *d, HLV1BitReader *br,
                              int x, int y, int q_y, int q_uv) {
    HLV1Frame *cur = &d->current;
    int r = decode_plane_residual(
                                  d, br,
                                  current_plane_ptr(d, HLV1_PLANE_Y, x, y),
                                  cur->stride_y, 16, 16, q_y);
    if (r < 0) return r;
    int cx = x / 2, cy = y / 2;
    r = decode_plane_residual(
                              d, br,
                              current_plane_ptr(d, HLV1_PLANE_U, cx, cy),
                              cur->stride_u, 8, 8, q_uv);
    if (r < 0) return r;
    return decode_plane_residual(
                                 d, br,
                                 current_plane_ptr(d, HLV1_PLANE_V, cx, cy),
                                 cur->stride_v, 8, 8, q_uv);
}

static int decode_sb8_residual(HLV1Decoder *d, HLV1BitReader *br,
                               int x, int y, int q_y, int q_uv) {
    HLV1Frame *cur = &d->current;
    int r = decode_plane_residual(
                                  d, br,
                                  current_plane_ptr(d, HLV1_PLANE_Y, x, y),
                                  cur->stride_y, 8, 8, q_y);
    if (r < 0) return r;
    int cx = x / 2, cy = y / 2;
    r = decode_plane_residual(
                              d, br,
                              current_plane_ptr(d, HLV1_PLANE_U, cx, cy),
                              cur->stride_u, 4, 4, q_uv);
    if (r < 0) return r;
    return decode_plane_residual(
                                 d, br,
                                 current_plane_ptr(d, HLV1_PLANE_V, cx, cy),
                                 cur->stride_v, 4, 4, q_uv);
}

static int decode_optional_sb8_residual(HLV1Decoder *d, HLV1BitReader *br,
                                        unsigned version, int x, int y,
                                        int q_y, int q_uv) {
    uint32_t profile_start = HLV1_PROFILE_NOW();
    uint32_t has_residual = hlv1_br_get(br, 1);
    if (br->error) {
        HLV1_PROFILE_ADD(d, residual_cycles, profile_start);
        return br->error;
    }
    if (!has_residual) {
        HLV1_STAT_ADD(d, residual_blocks, 6);
        HLV1_STAT_ADD(d, zero_residual_blocks, 6);
        HLV1_PROFILE_ADD(d, residual_cycles, profile_start);
        return HLV1_OK;
    }
    int result = version >= HLV1_STREAM_VERSION_8 && q_y >= 64
                     ? decode_sb8_residual_masked(d, br, version,
                                                  x, y, q_y, q_uv)
                     : decode_sb8_residual(d, br, x, y, q_y, q_uv);
    HLV1_PROFILE_ADD(d, residual_cycles, profile_start);
    return result;
}

static int get_mode(HLV1BitReader *br, unsigned version, int frame_type,
                    int use_global) {
    if (version >= HLV1_STREAM_VERSION_13)
        return (int)hlv1_br_get(br, 4);
    if (version < HLV1_STREAM_VERSION_2)
        return (int)hlv1_br_get(br, 2);
    if (frame_type == HLV1_FRAME_KEY) {
        if (version >= HLV1_STREAM_VERSION_12) {
            if (!hlv1_br_get(br, 1)) return HLV1_MODE_INTRA_DC;
            return hlv1_br_get(br, 1) ? HLV1_MODE_PALETTE : HLV1_MODE_FILL;
        }
        return hlv1_br_get(br, 1) ? HLV1_MODE_FILL : HLV1_MODE_INTRA_DC;
    }
    if (!hlv1_br_get(br, 1)) return HLV1_MODE_SKIP;
    if (version >= HLV1_STREAM_VERSION_12 && use_global) {
        if (!hlv1_br_get(br, 1)) return HLV1_MODE_GLOBAL;
        if (!hlv1_br_get(br, 1)) return HLV1_MODE_INTER;
        if (!hlv1_br_get(br, 1)) return HLV1_MODE_SPLIT_INTER;
        if (!hlv1_br_get(br, 1)) return HLV1_MODE_INTRA_DC;
        return hlv1_br_get(br, 1) ? HLV1_MODE_PALETTE : HLV1_MODE_FILL;
    }
    if (version >= HLV1_STREAM_VERSION_7 && use_global) {
        if (!hlv1_br_get(br, 1)) return HLV1_MODE_GLOBAL;
        if (!hlv1_br_get(br, 1)) return HLV1_MODE_INTER;
        if (!hlv1_br_get(br, 1)) return HLV1_MODE_SPLIT_INTER;
        return hlv1_br_get(br, 1) ? HLV1_MODE_FILL : HLV1_MODE_INTRA_DC;
    }
    if (!hlv1_br_get(br, 1)) return HLV1_MODE_INTER;
    if (version >= HLV1_STREAM_VERSION_12) {
        if (!hlv1_br_get(br, 1)) return HLV1_MODE_SPLIT_INTER;
        if (!hlv1_br_get(br, 1)) return HLV1_MODE_INTRA_DC;
        return hlv1_br_get(br, 1) ? HLV1_MODE_PALETTE : HLV1_MODE_FILL;
    }
    if (version < HLV1_STREAM_VERSION_3)
        return hlv1_br_get(br, 1) ? HLV1_MODE_FILL : HLV1_MODE_INTRA_DC;
    if (!hlv1_br_get(br, 1)) return HLV1_MODE_SPLIT_INTER;
    return hlv1_br_get(br, 1) ? HLV1_MODE_FILL : HLV1_MODE_INTRA_DC;
}

static int align_reader_zero(HLV1BitReader *br, uint32_t packet_bits) {
    uint64_t consumed = (uint64_t)packet_bits - br->bits_left;
    unsigned padding = (unsigned)((8U - (consumed & 7U)) & 7U);
    if (padding && hlv1_br_get(br, padding) != 0)
        return br->error ? br->error : HLV1_ERR_BITSTREAM;
    return br->error ? br->error : HLV1_OK;
}

static int get_motion_vector(HLV1BitReader *br, unsigned version,
                             int *mvx, int *mvy) {
    int x, y;
    if (version < HLV1_STREAM_VERSION_5) {
        x = hlv1_br_get_se(br) * 2;
        y = hlv1_br_get_se(br) * 2;
    } else if (version < HLV1_STREAM_VERSION_6) {
        x = hlv1_br_get_se(br);
        y = hlv1_br_get_se(br);
    } else {
        int fractional = (int)hlv1_br_get(br, 1);
        x = hlv1_br_get_se(br);
        y = hlv1_br_get_se(br);
        if (!fractional) { x *= 2; y *= 2; }
    }
    if (br->error) return br->error;
    *mvx = x;
    *mvy = y;
    return HLV1_OK;
}

static int decode_present_mb_residual(HLV1Decoder *d, HLV1BitReader *br,
                                      unsigned version, int x, int y,
                                      int q_y, int q_uv) {
    uint32_t profile_start = HLV1_PROFILE_NOW();
    int result = version >= HLV1_STREAM_VERSION_8 && q_y >= 64
                     ? decode_mb_residual_masked(d, br, version,
                                                 x, y, q_y, q_uv)
                     : decode_mb_residual(d, br, x, y, q_y, q_uv);
    HLV1_PROFILE_ADD(d, residual_cycles, profile_start);
    return result;
}

static int decode_optional_mb_residual(HLV1Decoder *d, HLV1BitReader *br,
                                       unsigned version, int x, int y,
                                       int q_y, int q_uv) {
    if (version >= HLV1_STREAM_VERSION_2) {
        uint32_t profile_start = HLV1_PROFILE_NOW();
        uint32_t has_residual = hlv1_br_get(br, 1);
        if (br->error) {
            HLV1_PROFILE_ADD(d, residual_cycles, profile_start);
            return br->error;
        }
        if (!has_residual) {
            HLV1_STAT_ADD(d, residual_blocks, 24);
            HLV1_STAT_ADD(d, zero_residual_blocks, 24);
            HLV1_STAT_ADD(d, zero_residual_macroblocks, 1);
            HLV1_PROFILE_ADD(d, residual_cycles, profile_start);
            return HLV1_OK;
        }
        HLV1_PROFILE_ADD(d, residual_cycles, profile_start);
    }
    return decode_present_mb_residual(d, br, version, x, y, q_y, q_uv);
}

/* --- Public decoder lifecycle ----------------------------------------- */
static HLV1Decoder *decoder_create_mode(const HLV1Header *header,
                                        int compact_mode) {
    unsigned version = hlv1_stream_version(header);
    if (!header || !header->width || !header->height ||
        version < HLV1_MIN_VERSION || version > HLV1_MAX_VERSION)
        return NULL;
    if (compact_mode == 2 &&
        header->search_radius > HLV1_SINGLE_REFERENCE_MAX_RADIUS)
        return NULL;
    HLV1Decoder *d = (HLV1Decoder *)calloc(1, sizeof *d);
    if (!d) return NULL;
    trace_decoder_heap("after state");
    d->header = *header;
    d->compact_y7_u6_v6 = compact_mode != 0;
    d->compact_single_reference = compact_mode == 2;
    if (d->compact_y7_u6_v6) {
        if (compact_frame_alloc(&d->previous, header->width,
                                header->height) < 0) {
            hlv1_decoder_destroy(d);
            return NULL;
        }
        trace_decoder_heap("after previous reference");
        d->compact_ring_luma_rows =
            d->compact_single_reference
                ? HLV1_SINGLE_REFERENCE_LUMA_ROWS
                : d->previous.padded_height;
        if (d->compact_ring_luma_rows > d->previous.padded_height)
            d->compact_ring_luma_rows = d->previous.padded_height;
        if (compact_frame_alloc_rows(
                &d->compact_current, header->width, header->height,
                d->compact_ring_luma_rows) < 0) {
            hlv1_decoder_destroy(d);
            return NULL;
        }
        trace_decoder_heap("after current reference");
        d->current.width = header->width;
        d->current.height = header->height;
        d->current.padded_width = d->previous.padded_width;
        d->current.padded_height = d->previous.padded_height;
        d->current.stride_y = d->current.padded_width;
        d->current.stride_u = d->current.padded_width / 2;
        d->current.stride_v = d->current.stride_u;
        d->current.y = (uint8_t *)malloc(
            (size_t)d->current.stride_y * 16U);
        d->current.u = (uint8_t *)malloc(
            (size_t)d->current.stride_u * 8U);
        d->current.v = (uint8_t *)malloc(
            (size_t)d->current.stride_v * 8U);
        if (!d->current.y || !d->current.u || !d->current.v) {
            hlv1_decoder_destroy(d);
            return NULL;
        }
    } else {
        if (hlv1_frame_alloc(&d->previous, header->width,
                             header->height) < 0 ||
            hlv1_frame_alloc(&d->current, header->width,
                             header->height) < 0) {
            hlv1_decoder_destroy(d);
            return NULL;
        }
    }
    trace_decoder_heap("after frame storage");
    d->mv_cols = d->current.padded_width / 16;
    size_t bytes = (size_t)d->mv_cols * sizeof(int16_t);
    d->mv_top_x = (int16_t *)malloc(bytes);
    d->mv_top_y = (int16_t *)malloc(bytes);
    d->mv_cur_x = (int16_t *)malloc(bytes);
    d->mv_cur_y = (int16_t *)malloc(bytes);
    if (!d->mv_top_x || !d->mv_top_y || !d->mv_cur_x || !d->mv_cur_y) {
        hlv1_decoder_destroy(d);
        return NULL;
    }
    trace_decoder_heap("after motion state");
    return d;
}

HLV1Decoder *hlv1_decoder_create(const HLV1Header *header) {
    return decoder_create_mode(header, 0);
}

HLV1Decoder *hlv1_decoder_create_y7_u6_v6(const HLV1Header *header) {
    return decoder_create_mode(header, 1);
}

HLV1Decoder *hlv1_decoder_create_y7_u6_v6_single_reference(
    const HLV1Header *header) {
    return decoder_create_mode(header, 2);
}

void hlv1_decoder_set_reference_row_guard(
    HLV1Decoder *d, HLV1ReferenceRowGuard guard, void *opaque) {
    if (!d) return;
    d->reference_row_guard = guard;
    d->reference_row_guard_opaque = opaque;
}

void hlv1_decoder_destroy(HLV1Decoder *d) {
    if (!d) return;
    hlv1_frame_free(&d->previous);
    if (d->compact_y7_u6_v6) {
        hlv1_frame_free(&d->compact_current);
        free(d->current.y);
        free(d->current.u);
        free(d->current.v);
    } else {
        hlv1_frame_free(&d->current);
    }
    free(d->mv_top_x); free(d->mv_top_y);
    free(d->mv_cur_x); free(d->mv_cur_y);
    free(d);
}

/* Decode one complete packet.  The first packet must be a keyframe; a packet
 * is committed as the new reference only after all syntax has validated. */
static int decoder_decode_packet(HLV1Decoder *d, const HLV1Packet *p,
                                 const HLV1Frame **frame,
                                 int segmented,
                                 HLV1BitReaderRefill refill,
                                 void *refill_context) {
    const unsigned version = hlv1_stream_version(&d->header);
    if ((p->frame_type == HLV1_FRAME_P ||
         p->frame_type == HLV1_FRAME_REPEAT) && !d->have_previous)
        return HLV1_ERR_FORMAT;
    if (!p->q_y || !p->q_uv || p->q_shift > 3 ||
        (version < HLV1_STREAM_VERSION_4 && p->q_shift != 0) ||
        p->bit_length > p->payload_size * 8ULL)
        return HLV1_ERR_FORMAT;
    uint32_t profile_start = HLV1_PROFILE_NOW();
    if (p->frame_type == HLV1_FRAME_REPEAT) {
        if (version < HLV1_STREAM_VERSION_15 || p->bit_length != 0 ||
            hlv1_packet_video_payload_size(p) != 0)
            return HLV1_ERR_FORMAT;
        HLV1_STAT_ADD(d, frames, 1);
        HLV1_STAT_ADD(d, repeated_frames, 1);
        *frame = &d->previous;
        HLV1_PROFILE_ADD(d, total_cycles, profile_start);
#if HLV1_ENABLE_STAGE_PROFILE
        ++d->profile.frames;
#endif
        return HLV1_OK;
    }
    int q_y = (int)p->q_y << p->q_shift;
    int q_uv = (int)p->q_uv << p->q_shift;
    int denominator = version >= HLV1_STREAM_VERSION_6 ? 2 : 1;

    HLV1BitReader br;
    if (refill)
        hlv1_br_init_stream(&br, p->bit_length, refill, refill_context);
    else if (segmented)
        hlv1_br_init_packet(&br, p);
    else
        hlv1_br_init(&br, p->payload, p->payload_size, p->bit_length);
    d->compact_committed_luma_rows = 0;
    d->compact_committed_chroma_rows = 0;
    int pw = d->current.padded_width, ph = d->current.padded_height;
    int global_mvx = 0, global_mvy = 0;
    int use_global = 0;
    if (p->frame_type == HLV1_FRAME_P && version >= HLV1_STREAM_VERSION_7) {
        use_global = (int)hlv1_br_get(&br, 1);
        if (br.error) return br.error;
        if (use_global) {
            int r = get_motion_vector(&br, version, &global_mvx, &global_mvy);
            if (r < 0) return r;
            if (!motion_within_declared_radius(
                    d, global_mvx, global_mvy, denominator))
                return HLV1_ERR_BITSTREAM;
        }
    }

    int fallback_mvx = use_global ? global_mvx : 0;
    int fallback_mvy = use_global ? global_mvy : 0;
    if (p->frame_type == HLV1_FRAME_P && version >= HLV1_STREAM_VERSION_11) {
        for (int i = 0; i < d->mv_cols; ++i) {
            d->mv_top_x[i] = (int16_t)fallback_mvx;
            d->mv_top_y[i] = (int16_t)fallback_mvy;
        }
    }

    for (int y = 0; y < ph; y += 16) {
        if (d->compact_single_reference &&
            y >= d->compact_ring_luma_rows) {
            compact_single_reference_commit(
                d, y + 16 - d->compact_ring_luma_rows);
        }
        if (p->frame_type == HLV1_FRAME_P && version >= HLV1_STREAM_VERSION_11) {
            for (int i = 0; i < d->mv_cols; ++i) {
                d->mv_cur_x[i] = (int16_t)fallback_mvx;
                d->mv_cur_y[i] = (int16_t)fallback_mvy;
            }
        }
        for (int x = 0; x < pw; x += 16) {
            if (version >= HLV1_STREAM_VERSION_13) {
                int align_result = align_reader_zero(&br, p->bit_length);
                if (align_result < 0) return align_result;
            }
            int mv_column = x / 16;
            int predictor_mvx = fallback_mvx, predictor_mvy = fallback_mvy;
            if (p->frame_type == HLV1_FRAME_P &&
                version >= HLV1_STREAM_VERSION_11) {
                motion_vector_predictor(d->mv_top_x, d->mv_top_y,
                                        d->mv_cur_x, d->mv_cur_y,
                                        mv_column, d->mv_cols,
                                        fallback_mvx, fallback_mvy,
                                        &predictor_mvx, &predictor_mvy);
                HLV1_STAT_ADD(d, motion_predictor_blocks, 1);
            }
            int mode = get_mode(&br, version, p->frame_type, use_global);
            if (br.error) return br.error;
            int r = HLV1_OK;
            int compact_output_ready = 0;
            int context_mvx = fallback_mvx, context_mvy = fallback_mvy;
            switch (mode) {
            case HLV1_MODE_SKIP:
                if (p->frame_type != HLV1_FRAME_P || !d->have_previous)
                    return HLV1_ERR_BITSTREAM;
                if (d->compact_y7_u6_v6) {
                    compact_copy_macroblock(d, x, y);
                    compact_output_ready = 1;
                    HLV1_STAT_ADD(d, copied_samples, 384);
                } else {
                    predict_motion(d, x, y, 0, 0, denominator);
                }
                context_mvx = context_mvy = 0;
                HLV1_STAT_ADD(d, skipped, 1);
                break;
            case HLV1_MODE_SKIP_RUN: {
                if (version < HLV1_STREAM_VERSION_15 ||
                    p->frame_type != HLV1_FRAME_P || !d->have_previous)
                    return HLV1_ERR_BITSTREAM;
                int run = (int)hlv1_br_get(&br, 4) + 2;
                if (br.error || run > (pw - x) / 16)
                    return HLV1_ERR_BITSTREAM;
                for (int i = 0; i < run; ++i) {
                    int gx = x + i * 16;
                    if (d->compact_y7_u6_v6) {
                        compact_copy_macroblock(d, gx, y);
                        HLV1_STAT_ADD(d, copied_samples, 384);
                    } else {
                        predict_motion(d, gx, y, 0, 0, denominator);
                        hlv1_frame_quantize_v14_reference_mb(
                            &d->current, gx, y);
                    }
                    if (version >= HLV1_STREAM_VERSION_11) {
                        int column = mv_column + i;
                        d->mv_cur_x[column] = 0;
                        d->mv_cur_y[column] = 0;
                    }
                }
                HLV1_STAT_ADD(d, skipped, run);
                HLV1_STAT_ADD(d, skip_runs, 1);
                HLV1_STAT_ADD(d, macroblocks, run);
                x += (run - 1) * 16;
                continue;
            }
            case HLV1_MODE_INTER: {
                if (p->frame_type != HLV1_FRAME_P || !d->have_previous)
                    return HLV1_ERR_BITSTREAM;
                int mvx = 0, mvy = 0;
                if ((r = get_motion_vector(&br, version, &mvx, &mvy)) < 0)
                    return r;
                if (version >= HLV1_STREAM_VERSION_11) {
                    mvx += predictor_mvx;
                    mvy += predictor_mvy;
                } else if (use_global) {
                    mvx += global_mvx;
                    mvy += global_mvy;
                }
                if (!motion_within_declared_radius(
                        d, mvx, mvy, denominator) ||
                    !motion_valid(&d->previous, x, y, 16,
                                  mvx, mvy, denominator))
                    return HLV1_ERR_BITSTREAM;
                predict_motion(d, x, y, mvx, mvy, denominator);
                context_mvx = mvx; context_mvy = mvy;
                r = decode_optional_mb_residual(d, &br, version, x, y, q_y, q_uv);
                HLV1_STAT_ADD(d, inter, 1);
                break;
            }
            case HLV1_MODE_GLOBAL:
                if (version < HLV1_STREAM_VERSION_7 || !use_global ||
                    p->frame_type != HLV1_FRAME_P || !d->have_previous ||
                    !motion_valid(&d->previous, x, y, 16,
                                  global_mvx, global_mvy, denominator))
                    return HLV1_ERR_BITSTREAM;
                predict_motion(d, x, y, global_mvx, global_mvy, denominator);
                context_mvx = global_mvx; context_mvy = global_mvy;
                r = decode_optional_mb_residual(d, &br, version,
                                                x, y, q_y, q_uv);
                HLV1_STAT_ADD(d, global, 1);
                break;
            case HLV1_MODE_SPLIT_INTER: {
                if (version < HLV1_STREAM_VERSION_3 ||
                    p->frame_type != HLV1_FRAME_P || !d->have_previous)
                    return HLV1_ERR_BITSTREAM;
                for (int sy = 0; sy < 16; sy += 8) {
                    for (int sx = 0; sx < 16; sx += 8) {
                            int gx = x + sx, gy = y + sy;
                            uint32_t is_inter = hlv1_br_get(&br, 1);
                            if (br.error) return br.error;
                            if (!is_inter) {
                                predict_motion_sb8(d, gx, gy, 0, 0, denominator);
                                continue;
                            }
                            int mvx = 0, mvy = 0;
                            if ((r = get_motion_vector(&br, version, &mvx, &mvy)) < 0)
                                return r;
                            if (use_global) {
                                mvx += global_mvx;
                                mvy += global_mvy;
                            }
                            if (!motion_within_declared_radius(
                                    d, mvx, mvy, denominator) ||
                                !motion_valid(&d->previous, gx, gy, 8,
                                              mvx, mvy, denominator))
                                return HLV1_ERR_BITSTREAM;
                            predict_motion_sb8(d, gx, gy, mvx, mvy, denominator);
                            r = decode_optional_sb8_residual(d, &br, version, gx, gy,
                                                             q_y, q_uv);
                            if (r < 0) return r;
                    }
                }
                HLV1_STAT_ADD(d, split_inter, 1);
                break;
            }
            case HLV1_MODE_SPLIT_JOINT: {
                if (version < HLV1_STREAM_VERSION_15 ||
                    p->frame_type != HLV1_FRAME_P || !d->have_previous)
                    return HLV1_ERR_BITSTREAM;
                for (int sy = 0; sy < 16; sy += 8) {
                    for (int sx = 0; sx < 16; sx += 8) {
                        int gx = x + sx, gy = y + sy;
                        uint32_t is_inter = hlv1_br_get(&br, 1);
                        if (br.error) return br.error;
                        int mvx = 0, mvy = 0;
                        if (is_inter) {
                            if ((r = get_motion_vector(
                                     &br, version, &mvx, &mvy)) < 0)
                                return r;
                            if (use_global) {
                                mvx += global_mvx;
                                mvy += global_mvy;
                            }
                        }
                        if (!motion_within_declared_radius(
                                d, mvx, mvy, denominator) ||
                            !motion_valid(&d->previous, gx, gy, 8,
                                          mvx, mvy, denominator))
                            return HLV1_ERR_BITSTREAM;
                        predict_motion_sb8(d, gx, gy, mvx, mvy, denominator);
                    }
                }
                r = decode_optional_mb_residual(d, &br, version,
                                                x, y, q_y, q_uv);
                HLV1_STAT_ADD(d, split_joint, 1);
                break;
            }
            case HLV1_MODE_RECT_SPLIT: {
                if (version < HLV1_STREAM_VERSION_15 ||
                    p->frame_type != HLV1_FRAME_P || !d->have_previous)
                    return HLV1_ERR_BITSTREAM;
                int partition = hlv1_br_get(&br, 1) ? 2 : 1;
                if (br.error) return br.error;
                for (int i = 0; i < 2; ++i) {
                    int gx = x + (partition == 2 ? i * 8 : 0);
                    int gy = y + (partition == 1 ? i * 8 : 0);
                    int w = partition == 2 ? 8 : 16;
                    int h = partition == 2 ? 16 : 8;
                    uint32_t is_inter = hlv1_br_get(&br, 1);
                    if (br.error) return br.error;
                    int mvx = 0, mvy = 0;
                    if (is_inter) {
                        if ((r = get_motion_vector(
                                 &br, version, &mvx, &mvy)) < 0)
                            return r;
                        if (use_global) {
                            mvx += global_mvx;
                            mvy += global_mvy;
                        }
                    }
                    if (!motion_within_declared_radius(
                            d, mvx, mvy, denominator) ||
                        !motion_valid_rect(&d->previous, gx, gy, w, h,
                                           mvx, mvy, denominator))
                        return HLV1_ERR_BITSTREAM;
                    predict_motion_rect(d, gx, gy, w, h,
                                        mvx, mvy, denominator);
                }
                r = decode_optional_mb_residual(d, &br, version,
                                                x, y, q_y, q_uv);
                HLV1_STAT_ADD(d, rect_split, 1);
                break;
            }
            case HLV1_MODE_FILL: {
                uint8_t means[3] = {
                    (uint8_t)hlv1_br_get(&br, 8),
                    (uint8_t)hlv1_br_get(&br, 8),
                    (uint8_t)hlv1_br_get(&br, 8)
                };
                if (br.error) return br.error;
                if (d->compact_y7_u6_v6 &&
                    version >= HLV1_STREAM_VERSION_2) {
                    uint32_t has_residual = hlv1_br_get(&br, 1);
                    if (br.error) return br.error;
                    if (!has_residual) {
                        compact_fill_macroblock(d, x, y, means);
                        compact_output_ready = 1;
                        HLV1_STAT_ADD(d, residual_blocks, 24);
                        HLV1_STAT_ADD(d, zero_residual_blocks, 24);
                        HLV1_STAT_ADD(d, zero_residual_macroblocks, 1);
                    } else {
                        predict_fill(d, x, y, means);
                        r = decode_present_mb_residual(
                            d, &br, version, x, y, q_y, q_uv);
                    }
                } else {
                    predict_fill(d, x, y, means);
                    r = decode_optional_mb_residual(
                        d, &br, version, x, y, q_y, q_uv);
                }
                HLV1_STAT_ADD(d, fill, 1);
                break;
            }
            case HLV1_MODE_GRADIENT:
                if (version < HLV1_STREAM_VERSION_13)
                    return HLV1_ERR_BITSTREAM;
                r = decode_gradient(d, &br, x, y);
                if (r < 0) return r;
                r = decode_optional_mb_residual(d, &br, version,
                                                x, y, q_y, q_uv);
                HLV1_STAT_ADD(d, gradient, 1);
                break;
            case HLV1_MODE_PALETTE:
                if (version < HLV1_STREAM_VERSION_12)
                    return HLV1_ERR_BITSTREAM;
                r = decode_palette(d, &br, version, x, y);
                HLV1_STAT_ADD(d, palette, 1);
                break;
            case HLV1_MODE_LITERAL:
                if (version < HLV1_STREAM_VERSION_13)
                    return HLV1_ERR_BITSTREAM;
                r = decode_literal(d, &br, x, y, &compact_output_ready);
                HLV1_STAT_ADD(d, literal, 1);
                break;
            case HLV1_MODE_INTRA_DC: {
                int intra_mode = HLV1_INTRA_DC;
                if (version >= HLV1_STREAM_VERSION_10) {
                    intra_mode = (int)hlv1_br_get(&br, 2);
                    if (br.error || intra_mode > HLV1_INTRA_PLANE)
                        return HLV1_ERR_BITSTREAM;
                }
                predict_intra(d, x, y, intra_mode);
                r = decode_optional_mb_residual(d, &br, version,
                                                x, y, q_y, q_uv);
                if (intra_mode == HLV1_INTRA_VERTICAL)
                    HLV1_STAT_ADD(d, intra_vertical, 1);
                else if (intra_mode == HLV1_INTRA_HORIZONTAL)
                    HLV1_STAT_ADD(d, intra_horizontal, 1);
                else if (intra_mode == HLV1_INTRA_PLANE)
                    HLV1_STAT_ADD(d, intra_plane, 1);
                else HLV1_STAT_ADD(d, intra_dc, 1);
                break;
            }
            default:
                return HLV1_ERR_BITSTREAM;
            }
            if (r < 0) return r;
            if (d->compact_y7_u6_v6 && !compact_output_ready)
                compact_store_macroblock(d, x, y);
            else if (!d->compact_y7_u6_v6 && !compact_output_ready)
                hlv1_frame_quantize_v14_reference_mb(&d->current, x, y);
            if (p->frame_type == HLV1_FRAME_P &&
                version >= HLV1_STREAM_VERSION_11) {
                d->mv_cur_x[mv_column] = (int16_t)context_mvx;
                d->mv_cur_y[mv_column] = (int16_t)context_mvy;
            }
            HLV1_STAT_ADD(d, macroblocks, 1);
        }
        if (p->frame_type == HLV1_FRAME_P &&
            version >= HLV1_STREAM_VERSION_11) {
            int16_t *swap;
            swap = d->mv_top_x; d->mv_top_x = d->mv_cur_x; d->mv_cur_x = swap;
            swap = d->mv_top_y; d->mv_top_y = d->mv_cur_y; d->mv_cur_y = swap;
        }
    }
    if (br.error) return br.error;

    if (d->compact_single_reference) {
        compact_single_reference_commit(d, ph);
    } else if (d->compact_y7_u6_v6) {
        HLV1Frame tmp = d->previous;
        d->previous = d->compact_current;
        d->compact_current = tmp;
    } else {
        HLV1Frame tmp = d->previous;
        d->previous = d->current;
        d->current = tmp;
    }
    d->have_previous = 1;
    HLV1_STAT_ADD(d, frames, 1);
    HLV1_STAT_ADD(d, keyframes, p->frame_type == HLV1_FRAME_KEY);
    HLV1_STAT_ADD(d, payload_bytes, hlv1_packet_video_payload_size(p));
    HLV1_STAT_ADD(d, decoded_bits, p->bit_length);
    *frame = &d->previous;
    HLV1_PROFILE_ADD(d, total_cycles, profile_start);
#if HLV1_ENABLE_STAGE_PROFILE
    ++d->profile.frames;
#endif
    return HLV1_OK;
}

int hlv1_decoder_decode(HLV1Decoder *d, const HLV1Packet *p,
                        const HLV1Frame **frame) {
    if (!d || !p || !frame || p->payload_blocks ||
        (!p->payload && p->payload_size))
        return HLV1_ERR_ARGUMENT;
    return decoder_decode_packet(d, p, frame, 0, NULL, NULL);
}

int hlv1_decoder_decode_blocks(HLV1Decoder *d, const HLV1Packet *p,
                               const HLV1Frame **frame) {
    const uint8_t *first_payload = NULL;
    if (!d || !p || !frame || p->payload ||
        (p->payload_size &&
         !hlv1_packet_payload_span(p, 0, &first_payload)))
        return HLV1_ERR_ARGUMENT;
    return decoder_decode_packet(d, p, frame, 1, NULL, NULL);
}

typedef struct HLV1FileDecodeStream {
    FILE *file;
    uint8_t *buffer;
    size_t buffer_size;
    size_t remaining;
    uint32_t crc;
    HLV1StageProfile *profile;
} HLV1FileDecodeStream;

static size_t file_decode_refill(void *opaque, const uint8_t **data,
                                 int *error) {
    HLV1FileDecodeStream *stream = (HLV1FileDecodeStream *)opaque;
    if (!stream || !data || !error || !stream->file || !stream->buffer ||
        !stream->buffer_size) {
        if (error) *error = HLV1_ERR_ARGUMENT;
        return 0;
    }
    if (!stream->remaining) return 0;
    size_t bytes = HLV1_MIN(stream->remaining, stream->buffer_size);
    uint32_t input_profile_start = HLV1_PROFILE_NOW();
    if (fread(stream->buffer, 1, bytes, stream->file) != bytes) {
#if HLV1_ENABLE_STAGE_PROFILE
        if (stream->profile)
            stream->profile->input_cycles +=
                (uint32_t)(HLV1_PROFILE_NOW() - input_profile_start);
#else
        (void)input_profile_start;
#endif
        *error = HLV1_ERR_IO;
        return 0;
    }
#if HLV1_ENABLE_STAGE_PROFILE
    if (stream->profile) {
        stream->profile->input_cycles +=
            (uint32_t)(HLV1_PROFILE_NOW() - input_profile_start);
        stream->profile->input_bytes += bytes;
        ++stream->profile->input_refills;
    }
#else
    (void)input_profile_start;
#endif
    uint32_t profile_start = HLV1_PROFILE_NOW();
    stream->crc = hlv1_crc32_update(
        stream->crc, stream->buffer, bytes);
#if HLV1_ENABLE_STAGE_PROFILE
    if (stream->profile)
        stream->profile->crc_cycles +=
            (uint32_t)(HLV1_PROFILE_NOW() - profile_start);
#else
    (void)profile_start;
#endif
    stream->remaining -= bytes;
    *data = stream->buffer;
    return bytes;
}

int hlv1_decoder_decode_file_packet(
    HLV1Decoder *d, FILE *file, uint8_t *buffer, size_t buffer_size,
    const HLV1Packet *packet, uint32_t expected_crc,
    const HLV1Frame **frame) {
    if (!d || !file || !buffer || !buffer_size || !packet || !frame ||
        packet->payload || packet->payload_blocks)
        return HLV1_ERR_ARGUMENT;
    HLV1FileDecodeStream stream = {
        .file = file,
        .buffer = buffer,
        .buffer_size = buffer_size,
        .remaining = packet->payload_size,
        .crc = hlv1_crc32_begin(),
        .profile = HLV1_PROFILE_POINTER(d)
    };
    int result = decoder_decode_packet(
        d, packet, frame, 0, file_decode_refill, &stream);
    while (stream.remaining) {
        const uint8_t *unused = NULL;
        int refill_error = HLV1_OK;
        if (!file_decode_refill(&stream, &unused, &refill_error)) {
            if (result >= 0) result = refill_error;
            break;
        }
    }
    if (!stream.remaining && hlv1_crc32_end(stream.crc) != expected_crc)
        result = HLV1_ERR_CRC;
    return result;
}

int hlv1_decoder_decode_file(HLV1Decoder *d, FILE *file,
                             uint8_t *buffer, size_t buffer_size,
                             HLV1Packet *packet_info,
                             const HLV1Frame **frame) {
    if (!d || !file || !buffer || !buffer_size || !frame)
        return HLV1_ERR_ARGUMENT;
    uint8_t header_bytes[HLV1_FRAME_HEADER_SIZE];
    size_t header_size = fread(
        header_bytes, 1, sizeof header_bytes, file);
    if (!header_size && feof(file)) return HLV1_EOF;
    if (header_size != sizeof header_bytes) return HLV1_ERR_IO;

    HLV1Packet packet = {0};
    uint32_t expected_crc = 0;
    int result = hlv1_packet_header_parse(
        header_bytes, &packet, &expected_crc);
    if (result < 0) return result;
    if (packet_info) *packet_info = packet;

    return hlv1_decoder_decode_file_packet(
        d, file, buffer, buffer_size, &packet, expected_crc, frame);
}

const HLV1Stats *hlv1_decoder_stats(const HLV1Decoder *d) {
#if HLV1_ENABLE_DECODER_STATS
    return d ? &d->stats : NULL;
#else
    static const HLV1Stats empty_stats = {0};
    return d ? &empty_stats : NULL;
#endif
}

const HLV1StageProfile *hlv1_decoder_stage_profile(const HLV1Decoder *d) {
#if HLV1_ENABLE_STAGE_PROFILE
    return d ? &d->profile : NULL;
#else
    (void)d;
    return NULL;
#endif
}

void hlv1_decoder_stage_profile_reset(HLV1Decoder *d) {
#if HLV1_ENABLE_STAGE_PROFILE
    if (d) memset(&d->profile, 0, sizeof d->profile);
#else
    (void)d;
#endif
}
