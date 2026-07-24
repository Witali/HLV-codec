/*
 * Reference HLV-1 decoder.
 *
 * Decoding is single-pass and raster ordered.  It keeps one previous YUV420
 * reference, reconstructs the current frame in place, and exposes operation
 * counters used to enforce the 100 MHz playback budget.  No floating point,
 * future frames, or encoder-side search is required.
 */
#include "hlv1_internal.h"

#ifdef ARDUINO_ARCH_ESP32
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
    int have_previous;
    HLV1Stats stats;
    int mv_cols;
    int16_t *mv_top_x;
    int16_t *mv_top_y;
    int16_t *mv_cur_x;
    int16_t *mv_cur_y;
};

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

static void predict_plane_fractional(HLV1Stats *stats,
                                     uint8_t *dst, int dst_stride,
                                     int w, int h,
                                     const uint8_t *src, int src_stride,
                                     int origin_x_num, int origin_y_num,
                                     int denominator) {
    int bx = floor_div(origin_x_num, denominator);
    int by = floor_div(origin_y_num, denominator);
    int fx = origin_x_num - bx * denominator;
    int fy = origin_y_num - by * denominator;
    if (!fx && !fy) {
        if (stats) stats->copied_samples += (uint64_t)w * h;
        copy_block(dst, dst_stride, src + by * src_stride + bx,
                   src_stride, w, h);
        return;
    }
    if (!fy) {
        if (stats) stats->interpolated_hv_samples += (uint64_t)w * h;
        int inv_x = denominator - fx;
        int round = denominator / 2;
        for (int yy = 0; yy < h; ++yy) {
            const uint8_t *row = src + (by + yy) * src_stride + bx;
            uint8_t *out = dst + yy * dst_stride;
            for (int xx = 0; xx < w; ++xx)
                out[xx] = (uint8_t)((row[xx] * inv_x + row[xx + 1] * fx + round) /
                                    denominator);
        }
        return;
    }
    if (!fx) {
        if (stats) stats->interpolated_hv_samples += (uint64_t)w * h;
        int inv_y = denominator - fy;
        int round = denominator / 2;
        for (int yy = 0; yy < h; ++yy) {
            const uint8_t *row = src + (by + yy) * src_stride + bx;
            const uint8_t *next = row + src_stride;
            uint8_t *out = dst + yy * dst_stride;
            for (int xx = 0; xx < w; ++xx)
                out[xx] = (uint8_t)((row[xx] * inv_y + next[xx] * fy + round) /
                                    denominator);
        }
        return;
    }
    if (stats) stats->interpolated_bilinear_samples += (uint64_t)w * h;
    int inv_x = denominator - fx;
    int inv_y = denominator - fy;
    int round = denominator * denominator / 2;
    for (int yy = 0; yy < h; ++yy) {
        const uint8_t *row = src + (by + yy) * src_stride + bx;
        const uint8_t *next = row + src_stride;
        uint8_t *out = dst + yy * dst_stride;
        for (int xx = 0; xx < w; ++xx) {
            int top = row[xx] * inv_x + row[xx + 1] * fx;
            int bottom = next[xx] * inv_x + next[xx + 1] * fx;
            out[xx] = (uint8_t)((top * inv_y + bottom * fy + round) /
                                (denominator * denominator));
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
    HLV1Frame *cur = &d->current;
    const HLV1Frame *ref = &d->previous;
    predict_plane_fractional(&d->stats, cur->y + y * cur->stride_y + x, cur->stride_y,
                             16, 16, ref->y, ref->stride_y,
                             x * denominator + mvx,
                             y * denominator + mvy, denominator);
    int cx = x / 2, cy = y / 2;
    int cden = denominator * 2;
    predict_plane_fractional(&d->stats, cur->u + cy * cur->stride_u + cx, cur->stride_u,
                             8, 8, ref->u, ref->stride_u,
                             cx * cden + mvx, cy * cden + mvy, cden);
    predict_plane_fractional(&d->stats, cur->v + cy * cur->stride_v + cx, cur->stride_v,
                             8, 8, ref->v, ref->stride_v,
                             cx * cden + mvx, cy * cden + mvy, cden);
}

static void predict_motion_sb8(HLV1Decoder *d, int x, int y, int mvx, int mvy,
                               int denominator) {
    HLV1Frame *cur = &d->current;
    const HLV1Frame *ref = &d->previous;
    predict_plane_fractional(&d->stats, cur->y + y * cur->stride_y + x, cur->stride_y,
                             8, 8, ref->y, ref->stride_y,
                             x * denominator + mvx,
                             y * denominator + mvy, denominator);
    int cx = x / 2, cy = y / 2;
    int cden = denominator * 2;
    predict_plane_fractional(&d->stats, cur->u + cy * cur->stride_u + cx, cur->stride_u,
                             4, 4, ref->u, ref->stride_u,
                             cx * cden + mvx, cy * cden + mvy, cden);
    predict_plane_fractional(&d->stats, cur->v + cy * cur->stride_v + cx, cur->stride_v,
                             4, 4, ref->v, ref->stride_v,
                             cx * cden + mvx, cy * cden + mvy, cden);
}

static void predict_motion_rect(HLV1Decoder *d, int x, int y,
                                int w, int h, int mvx, int mvy,
                                int denominator) {
    HLV1Frame *cur = &d->current;
    const HLV1Frame *ref = &d->previous;
    predict_plane_fractional(&d->stats,
                             cur->y + y * cur->stride_y + x, cur->stride_y,
                             w, h, ref->y, ref->stride_y,
                             x * denominator + mvx,
                             y * denominator + mvy, denominator);
    int cx = x / 2, cy = y / 2, cden = denominator * 2;
    predict_plane_fractional(&d->stats,
                             cur->u + cy * cur->stride_u + cx, cur->stride_u,
                             w / 2, h / 2, ref->u, ref->stride_u,
                             cx * cden + mvx, cy * cden + mvy, cden);
    predict_plane_fractional(&d->stats,
                             cur->v + cy * cur->stride_v + cx, cur->stride_v,
                             w / 2, h / 2, ref->v, ref->stride_v,
                             cx * cden + mvx, cy * cden + mvy, cden);
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
    HLV1Frame *cur = &d->current;
    predict_gradient_plane(cur->y + y * cur->stride_y + x, cur->stride_y,
                           16, 16, base[0], dx[0], dy[0]);
    int cx = x / 2, cy = y / 2;
    predict_gradient_plane(cur->u + cy * cur->stride_u + cx, cur->stride_u,
                           8, 8, base[1], dx[1], dy[1]);
    predict_gradient_plane(cur->v + cy * cur->stride_v + cx, cur->stride_v,
                           8, 8, base[2], dx[2], dy[2]);
    d->stats.gradient_samples += 384;
    return HLV1_OK;
}

static int decode_palette(HLV1Decoder *d, HLV1BitReader *br,
                          unsigned version, int x, int y) {
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
    HLV1Frame *cur = &d->current;
    for (int yy = 0; yy < 16; ++yy) {
        uint8_t *dst = cur->y + (y + yy) * cur->stride_y + x;
        for (int xx = 0; xx < 16; ++xx) {
            unsigned index = hlv1_br_get(br, index_bits);
            if (br->error || index >= (unsigned)count) return HLV1_ERR_BITSTREAM;
            dst[xx] = colors[index][0];
        }
    }
    int cx = x / 2, cy = y / 2;
    for (int yy = 0; yy < 8; ++yy) {
        uint8_t *du = cur->u + (cy + yy) * cur->stride_u + cx;
        uint8_t *dv = cur->v + (cy + yy) * cur->stride_v + cx;
        for (int xx = 0; xx < 8; ++xx) {
            unsigned index = hlv1_br_get(br, index_bits);
            if (br->error || index >= (unsigned)count) return HLV1_ERR_BITSTREAM;
            du[xx] = colors[index][1];
            dv[xx] = colors[index][2];
        }
    }
    d->stats.palette_samples += 384;
    if (count == 2) d->stats.palette_2++;
    else if (count == 4) d->stats.palette_4++;
    else d->stats.palette_8++;
    return HLV1_OK;
}

static int read_literal_row(HLV1BitReader *br, uint8_t *destination,
                            int samples, unsigned sample_bits) {
    uint8_t packed[12];
    size_t bytes = ((size_t)samples * sample_bits + 7U) / 8U;
    for (size_t i = 0; i < bytes; ++i)
        packed[i] = (uint8_t)hlv1_br_get(br, 8);
    if (br->error) return br->error;

    unsigned bit = 0;
    unsigned shift = 8U - sample_bits;
    for (int i = 0; i < samples; ++i) {
        unsigned code = 0;
        for (unsigned b = 0; b < sample_bits; ++b, ++bit)
            code |= (unsigned)((packed[bit >> 3] >> (bit & 7U)) & 1U) << b;
        destination[i] = (uint8_t)(code << shift);
    }
    return HLV1_OK;
}

static int decode_literal(HLV1Decoder *d, HLV1BitReader *br, int x, int y) {
    if (hlv1_br_get(br, 4) != 0 || br->error)
        return br->error ? br->error : HLV1_ERR_BITSTREAM;
    HLV1Frame *cur = &d->current;
    int r = HLV1_OK;
    for (int yy = 0; r >= 0 && yy < 16; ++yy)
        r = read_literal_row(br,
                             cur->y + (y + yy) * cur->stride_y + x,
                             16, 6);
    int cx = x / 2, cy = y / 2;
    for (int yy = 0; r >= 0 && yy < 8; ++yy)
        r = read_literal_row(br,
                             cur->u + (cy + yy) * cur->stride_u + cx,
                             8, 5);
    for (int yy = 0; r >= 0 && yy < 8; ++yy)
        r = read_literal_row(br,
                             cur->v + (cy + yy) * cur->stride_v + cx,
                             8, 5);
    if (r >= 0) d->stats.literal_samples += 384;
    return r;
}

static void predict_fill(HLV1Decoder *d, int x, int y, const uint8_t means[3]) {
    d->stats.fill_samples += 384;
    HLV1Frame *cur = &d->current;
    fill_block(cur->y + y * cur->stride_y + x, cur->stride_y, 16, 16, means[0]);
    int cx = x / 2, cy = y / 2;
    fill_block(cur->u + cy * cur->stride_u + cx, cur->stride_u, 8, 8, means[1]);
    fill_block(cur->v + cy * cur->stride_v + cx, cur->stride_v, 8, 8, means[2]);
}

static uint8_t intra_dc_plane(const uint8_t *plane, int stride,
                              int px, int py, int size) {
    uint32_t sum = 0;
    unsigned count = 0;
    if (py > 0) {
        const uint8_t *top = plane + (py - 1) * stride + px;
        for (int i = 0; i < size; ++i) sum += top[i];
        count += (unsigned)size;
    }
    if (px > 0) {
        const uint8_t *left = plane + py * stride + px - 1;
        for (int i = 0; i < size; ++i) sum += left[i * stride];
        count += (unsigned)size;
    }
    return count ? (uint8_t)rounded_mean_even(sum, count) : 128;
}

static void predict_intra_plane(uint8_t *plane, int stride,
                                int px, int py, int size, int mode) {
    uint8_t *dst = plane + py * stride + px;
    if (mode == HLV1_INTRA_DC) {
        fill_block(dst, stride, size, size,
                   intra_dc_plane(plane, stride, px, py, size));
        return;
    }
    const uint8_t *top = py > 0 ? plane + (py - 1) * stride + px : NULL;
    const uint8_t *left = px > 0 ? plane + py * stride + px - 1 : NULL;
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
    d->stats.intra_samples += 384;
    HLV1Frame *cur = &d->current;
    predict_intra_plane(cur->y, cur->stride_y, x, y, 16, mode);
    int cx = x / 2, cy = y / 2;
    predict_intra_plane(cur->u, cur->stride_u, cx, cy, 8, mode);
    predict_intra_plane(cur->v, cur->stride_v, cx, cy, 8, mode);
}

/* --- Residual decoding fast paths --------------------------------------
 * Zero and DC-only blocks bypass the general inverse WHT.  These paths are
 * important because they dominate ordinary low-bitrate video. */
static int add_dc_only(uint8_t *dst, int stride, int level, int qstep) {
    int dc_step = HLV1_MAX(1, qstep / 2);
    int v = level * dc_step;
    int delta = v >= 0 ? (v + 8) / 16 : -((-v + 8) / 16);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            dst[y * stride + x] = hlv1_clip8((int)dst[y * stride + x] + delta);
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

/* Decode one non-zero 4x4 residual, dequantize it, add prediction, and clip
 * in place.  Sparse one- and two-coefficient cases avoid the general inverse
 * transform when the syntax permits a cheaper reconstruction. */
static int decode_nonzero_residual_4x4(HLV1Decoder *d,
                                       HLV1BitReader *br,
                                       uint8_t *dst, int stride,
                                       int qstep, int coeff_mode) {
    uint32_t count;
    int32_t qcoeff[16] = {0};
    int pos = -1;
    int only_dc = 0;
    if (coeff_mode && !hlv1_br_get(br, 1)) {
        int32_t level;
        if (br->error || get_level_v9(br, &level) < 0)
            return br->error ? br->error : HLV1_ERR_BITSTREAM;
        count = 1;
        qcoeff[0] = level;
        only_dc = 1;
        d->stats.run_zero_symbols++;
        if (level == 1 || level == -1) d->stats.unit_level_symbols++;
    } else {
        if (br->error) return br->error;
        count = hlv1_br_get_ue(br) + 1U;
        if (br->error || count > 16) return HLV1_ERR_BITSTREAM;
        only_dc = count == 1;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t run = 0;
            int32_t level;
            run = hlv1_br_get_ue(br);
            if (coeff_mode == 1) {
                int r = get_level_v9(br, &level);
                if (r < 0) return r;
            } else {
                level = hlv1_br_get_se(br);
            }
            if (br->error) return br->error;
            if (run > 16U || pos + (int)run + 1 >= 16)
                return HLV1_ERR_BITSTREAM;
            if (run == 0) d->stats.run_zero_symbols++;
            if (level == 1 || level == -1) d->stats.unit_level_symbols++;
            pos += (int)run + 1;
            int idx = scan4[pos];
            qcoeff[idx] = level;
            if (idx != 0) only_dc = 0;
        }
    }

    d->stats.coefficient_symbols += count;
    if (count == 1) d->stats.single_coefficient_blocks++;
    else if (count == 2) d->stats.two_coefficient_blocks++;

    if (only_dc) {
        d->stats.dc_only_blocks++;
        return add_dc_only(dst, stride, qcoeff[0], qstep);
    }

    d->stats.inverse_wht_blocks++;
    int dc_step = HLV1_MAX(1, qstep / 2);
    int32_t coeff[16];
    for (int i = 0; i < 16; ++i)
        coeff[i] = qcoeff[i] * (i == 0 ? dc_step : qstep);
    int16_t residual[16];
    hlv1_wht4_inverse(coeff, residual);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            dst[y * stride + x] = hlv1_clip8(
                (int)dst[y * stride + x] + residual[y * 4 + x]);
    return HLV1_OK;
}

static int decode_residual_4x4(HLV1Decoder *d, HLV1BitReader *br,
                               uint8_t *dst, int stride, int qstep) {
    d->stats.residual_blocks++;
    if (!hlv1_br_get(br, 1)) {
        if (br->error) return br->error;
        d->stats.zero_residual_blocks++;
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
            d->stats.residual_blocks++;
            if (!(mask & (UINT32_C(1) << *block_index))) {
                d->stats.zero_residual_blocks++;
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
        d, br, cur->y + y * cur->stride_y + x, cur->stride_y,
        16, 16, q_y, mask, &index, coeff_mode);
    if (r < 0) return r;
    int cx = x / 2, cy = y / 2;
    r = decode_plane_residual_masked(
        d, br, cur->u + cy * cur->stride_u + cx, cur->stride_u,
        8, 8, q_uv, mask, &index, coeff_mode);
    if (r < 0) return r;
    return decode_plane_residual_masked(
        d, br, cur->v + cy * cur->stride_v + cx, cur->stride_v,
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
        d, br, cur->y + y * cur->stride_y + x, cur->stride_y,
        8, 8, q_y, mask, &index, coeff_mode);
    if (r < 0) return r;
    int cx = x / 2, cy = y / 2;
    r = decode_plane_residual_masked(
        d, br, cur->u + cy * cur->stride_u + cx, cur->stride_u,
        4, 4, q_uv, mask, &index, coeff_mode);
    if (r < 0) return r;
    return decode_plane_residual_masked(
        d, br, cur->v + cy * cur->stride_v + cx, cur->stride_v,
        4, 4, q_uv, mask, &index, coeff_mode);
}

static int decode_mb_residual(HLV1Decoder *d, HLV1BitReader *br,
                              int x, int y, int q_y, int q_uv) {
    HLV1Frame *cur = &d->current;
    int r = decode_plane_residual(d, br, cur->y + y * cur->stride_y + x,
                                  cur->stride_y, 16, 16, q_y);
    if (r < 0) return r;
    int cx = x / 2, cy = y / 2;
    r = decode_plane_residual(d, br, cur->u + cy * cur->stride_u + cx,
                              cur->stride_u, 8, 8, q_uv);
    if (r < 0) return r;
    return decode_plane_residual(d, br, cur->v + cy * cur->stride_v + cx,
                                 cur->stride_v, 8, 8, q_uv);
}

static int decode_sb8_residual(HLV1Decoder *d, HLV1BitReader *br,
                               int x, int y, int q_y, int q_uv) {
    HLV1Frame *cur = &d->current;
    int r = decode_plane_residual(d, br, cur->y + y * cur->stride_y + x,
                                  cur->stride_y, 8, 8, q_y);
    if (r < 0) return r;
    int cx = x / 2, cy = y / 2;
    r = decode_plane_residual(d, br, cur->u + cy * cur->stride_u + cx,
                              cur->stride_u, 4, 4, q_uv);
    if (r < 0) return r;
    return decode_plane_residual(d, br, cur->v + cy * cur->stride_v + cx,
                                 cur->stride_v, 4, 4, q_uv);
}

static int decode_optional_sb8_residual(HLV1Decoder *d, HLV1BitReader *br,
                                        unsigned version, int x, int y,
                                        int q_y, int q_uv) {
    uint32_t has_residual = hlv1_br_get(br, 1);
    if (br->error) return br->error;
    if (!has_residual) {
        d->stats.residual_blocks += 6;
        d->stats.zero_residual_blocks += 6;
        return HLV1_OK;
    }
    return version >= HLV1_STREAM_VERSION_8 && q_y >= 64
               ? decode_sb8_residual_masked(d, br, version,
                                             x, y, q_y, q_uv)
               : decode_sb8_residual(d, br, x, y, q_y, q_uv);
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

static int decode_optional_mb_residual(HLV1Decoder *d, HLV1BitReader *br,
                                       unsigned version, int x, int y,
                                       int q_y, int q_uv) {
    if (version >= HLV1_STREAM_VERSION_2) {
        uint32_t has_residual = hlv1_br_get(br, 1);
        if (br->error) return br->error;
        if (!has_residual) {
            d->stats.residual_blocks += 24;
            d->stats.zero_residual_blocks += 24;
            d->stats.zero_residual_macroblocks++;
            return HLV1_OK;
        }
    }
    return version >= HLV1_STREAM_VERSION_8 && q_y >= 64
               ? decode_mb_residual_masked(d, br, version,
                                            x, y, q_y, q_uv)
               : decode_mb_residual(d, br, x, y, q_y, q_uv);
}

/* --- Public decoder lifecycle ----------------------------------------- */
HLV1Decoder *hlv1_decoder_create(const HLV1Header *header) {
    if (!header || !header->width || !header->height ||
        hlv1_stream_version(header) > HLV1_VERSION) return NULL;
    HLV1Decoder *d = (HLV1Decoder *)calloc(1, sizeof *d);
    if (!d) return NULL;
    trace_decoder_heap("after state");
    d->header = *header;
    if (hlv1_frame_alloc(&d->previous, header->width, header->height) < 0) {
        hlv1_decoder_destroy(d);
        return NULL;
    }
    trace_decoder_heap("after previous frame");
    if (hlv1_frame_alloc(&d->current, header->width, header->height) < 0) {
        hlv1_decoder_destroy(d);
        return NULL;
    }
    trace_decoder_heap("after current frame");
    if (hlv1_stream_version(header) >= HLV1_STREAM_VERSION_11) {
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
    }
    trace_decoder_heap("after motion state");
    return d;
}

void hlv1_decoder_destroy(HLV1Decoder *d) {
    if (!d) return;
    hlv1_frame_free(&d->previous);
    hlv1_frame_free(&d->current);
    free(d->mv_top_x); free(d->mv_top_y);
    free(d->mv_cur_x); free(d->mv_cur_y);
    free(d);
}

/* Decode one complete packet.  The first packet must be a keyframe; a packet
 * is committed as the new reference only after all syntax has validated. */
static int decoder_decode_packet(HLV1Decoder *d, const HLV1Packet *p,
                                 const HLV1Frame **frame,
                                 int segmented) {
    if (p->frame_type == HLV1_FRAME_P && !d->have_previous) return HLV1_ERR_FORMAT;
    unsigned version = hlv1_stream_version(&d->header);
    if (!p->q_y || !p->q_uv || p->q_shift > 3 ||
        (version < HLV1_STREAM_VERSION_4 && p->q_shift != 0) ||
        p->bit_length > p->payload_size * 8ULL)
        return HLV1_ERR_FORMAT;
    int q_y = (int)p->q_y << p->q_shift;
    int q_uv = (int)p->q_uv << p->q_shift;
    int denominator = version >= HLV1_STREAM_VERSION_6 ? 2 : 1;

    HLV1BitReader br;
    if (segmented)
        hlv1_br_init_packet(&br, p);
    else
        hlv1_br_init(&br, p->payload, p->payload_size, p->bit_length);
    int pw = d->current.padded_width, ph = d->current.padded_height;
    int global_mvx = 0, global_mvy = 0;
    int use_global = 0;
    if (p->frame_type == HLV1_FRAME_P && version >= HLV1_STREAM_VERSION_7) {
        use_global = (int)hlv1_br_get(&br, 1);
        if (br.error) return br.error;
        if (use_global) {
            int r = get_motion_vector(&br, version, &global_mvx, &global_mvy);
            if (r < 0) return r;
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
                d->stats.motion_predictor_blocks++;
            }
            int mode = get_mode(&br, version, p->frame_type, use_global);
            if (br.error) return br.error;
            int r = HLV1_OK;
            int context_mvx = fallback_mvx, context_mvy = fallback_mvy;
            switch (mode) {
            case HLV1_MODE_SKIP:
                if (p->frame_type != HLV1_FRAME_P || !d->have_previous)
                    return HLV1_ERR_BITSTREAM;
                predict_motion(d, x, y, 0, 0, denominator);
                context_mvx = context_mvy = 0;
                d->stats.skipped++;
                break;
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
                if (!motion_valid(&d->previous, x, y, 16,
                                  mvx, mvy, denominator))
                    return HLV1_ERR_BITSTREAM;
                predict_motion(d, x, y, mvx, mvy, denominator);
                context_mvx = mvx; context_mvy = mvy;
                r = decode_optional_mb_residual(d, &br, version, x, y, q_y, q_uv);
                d->stats.inter++;
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
                d->stats.global++;
                break;
            case HLV1_MODE_SPLIT_INTER: {
                if (version < HLV1_STREAM_VERSION_3 ||
                    p->frame_type != HLV1_FRAME_P || !d->have_previous)
                    return HLV1_ERR_BITSTREAM;
                int partition = 0;
                if (version >= 14) {
                    int first = (int)hlv1_br_get(&br, 1);
                    if (br.error) return br.error;
                    if (first) {
                        int orientation = (int)hlv1_br_get(&br, 1);
                        if (br.error) return br.error;
                        partition = orientation ? 2 : 1;
                    }
                }
                if (!partition) {
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
                            if (!motion_valid(&d->previous, gx, gy, 8,
                                              mvx, mvy, denominator))
                                return HLV1_ERR_BITSTREAM;
                            predict_motion_sb8(d, gx, gy, mvx, mvy, denominator);
                            r = decode_optional_sb8_residual(d, &br, version, gx, gy,
                                                             q_y, q_uv);
                            if (r < 0) return r;
                        }
                    }
                } else {
                    for (int i = 0; i < 2; ++i) {
                        int gx = x + (partition == 2 ? i * 8 : 0);
                        int gy = y + (partition == 1 ? i * 8 : 0);
                        int w = partition == 2 ? 8 : 16;
                        int h = partition == 2 ? 16 : 8;
                        uint32_t is_inter = hlv1_br_get(&br, 1);
                        if (br.error) return br.error;
                        int mvx = 0, mvy = 0;
                        if (is_inter) {
                            if ((r = get_motion_vector(&br, version, &mvx, &mvy)) < 0)
                                return r;
                            if (use_global) {
                                mvx += global_mvx;
                                mvy += global_mvy;
                            }
                        }
                        if (!motion_valid_rect(&d->previous, gx, gy, w, h,
                                               mvx, mvy, denominator))
                            return HLV1_ERR_BITSTREAM;
                        predict_motion_rect(d, gx, gy, w, h,
                                            mvx, mvy, denominator);
                    }
                    r = decode_optional_mb_residual(d, &br, version,
                                                    x, y, q_y, q_uv);
                    if (r < 0) return r;
                }
                d->stats.split_inter++;
                break;
            }
            case HLV1_MODE_FILL: {
                uint8_t means[3] = {
                    (uint8_t)hlv1_br_get(&br, 8),
                    (uint8_t)hlv1_br_get(&br, 8),
                    (uint8_t)hlv1_br_get(&br, 8)
                };
                if (br.error) return br.error;
                predict_fill(d, x, y, means);
                r = decode_optional_mb_residual(d, &br, version,
                                                x, y, q_y, q_uv);
                d->stats.fill++;
                break;
            }
            case HLV1_MODE_GRADIENT:
                if (version < HLV1_STREAM_VERSION_13)
                    return HLV1_ERR_BITSTREAM;
                r = decode_gradient(d, &br, x, y);
                if (r < 0) return r;
                r = decode_optional_mb_residual(d, &br, version,
                                                x, y, q_y, q_uv);
                d->stats.gradient++;
                break;
            case HLV1_MODE_PALETTE:
                if (version < HLV1_STREAM_VERSION_12)
                    return HLV1_ERR_BITSTREAM;
                r = decode_palette(d, &br, version, x, y);
                d->stats.palette++;
                break;
            case HLV1_MODE_LITERAL:
                if (version < HLV1_STREAM_VERSION_13)
                    return HLV1_ERR_BITSTREAM;
                r = decode_literal(d, &br, x, y);
                d->stats.literal++;
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
                    d->stats.intra_vertical++;
                else if (intra_mode == HLV1_INTRA_HORIZONTAL)
                    d->stats.intra_horizontal++;
                else if (intra_mode == HLV1_INTRA_PLANE)
                    d->stats.intra_plane++;
                else d->stats.intra_dc++;
                break;
            }
            default:
                return HLV1_ERR_BITSTREAM;
            }
            if (r < 0) return r;
            if (p->frame_type == HLV1_FRAME_P &&
                version >= HLV1_STREAM_VERSION_11) {
                d->mv_cur_x[mv_column] = (int16_t)context_mvx;
                d->mv_cur_y[mv_column] = (int16_t)context_mvy;
            }
            d->stats.macroblocks++;
        }
        if (p->frame_type == HLV1_FRAME_P &&
            version >= HLV1_STREAM_VERSION_11) {
            int16_t *swap;
            swap = d->mv_top_x; d->mv_top_x = d->mv_cur_x; d->mv_cur_x = swap;
            swap = d->mv_top_y; d->mv_top_y = d->mv_cur_y; d->mv_cur_y = swap;
        }
    }
    if (br.error) return br.error;

    HLV1Frame tmp = d->previous;
    d->previous = d->current;
    d->current = tmp;
    d->have_previous = 1;
    d->stats.frames++;
    d->stats.keyframes += p->frame_type == HLV1_FRAME_KEY;
    d->stats.payload_bytes += hlv1_packet_video_payload_size(p);
    d->stats.decoded_bits += p->bit_length;
    *frame = &d->previous;
    return HLV1_OK;
}

int hlv1_decoder_decode(HLV1Decoder *d, const HLV1Packet *p,
                        const HLV1Frame **frame) {
    if (!d || !p || !frame || p->payload_blocks ||
        (!p->payload && p->payload_size))
        return HLV1_ERR_ARGUMENT;
    return decoder_decode_packet(d, p, frame, 0);
}

int hlv1_decoder_decode_blocks(HLV1Decoder *d, const HLV1Packet *p,
                               const HLV1Frame **frame) {
    const uint8_t *first_payload = NULL;
    if (!d || !p || !frame || p->payload ||
        (p->payload_size &&
         !hlv1_packet_payload_span(p, 0, &first_payload)))
        return HLV1_ERR_ARGUMENT;
    return decoder_decode_packet(d, p, frame, 1);
}

const HLV1Stats *hlv1_decoder_stats(const HLV1Decoder *d) {
    return d ? &d->stats : NULL;
}
