/*
 * Reference HLV-1 encoder.
 *
 * The encoder is intentionally much more complex than the decoder: it searches
 * motion, evaluates prediction modes, fully codes several RDO candidates, and
 * may clone its predictive state for quality/rate-control trials.  Every mode
 * decision ultimately emits syntax that the small sequential decoder can apply
 * with integer arithmetic and one previous frame.
 */
#include "hlv1_internal.h"

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <emmintrin.h>
#include <intrin.h>
#define HLV1_ENCODER_SSE2 1
#elif defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__))
#include <emmintrin.h>
#define HLV1_ENCODER_SSE2 1
#else
#define HLV1_ENCODER_SSE2 0
#endif

/* Low-frequency-first scan for 4x4 transform coefficients. */
static const uint8_t scan4[16] = {
    0, 1, 4, 5, 2, 8, 3, 12, 6, 9, 7, 13, 10, 11, 14, 15
};

/* A complete 16x16 YUV420 macroblock in tightly packed working memory. */
typedef struct MB {
    uint8_t y[16 * 16];
    uint8_t u[8 * 8];
    uint8_t v[8 * 8];
} MB;

typedef struct SB8 {
    uint8_t y[8 * 8];
    uint8_t u[4 * 4];
    uint8_t v[4 * 4];
} SB8;

typedef struct PaletteColor {
    uint8_t y;
    uint8_t u;
    uint8_t v;
} PaletteColor;

/* Fully reconstructed RDO candidate.  Keeping rec with its encoded bits avoids
 * a second reconstruction after the winning mode has been selected. */
typedef struct Candidate {
    double score;
    uint64_t estimated_decode_cycles;
    int mode;
    int intra_mode;
    int palette_size;
    int partition;
    int mvx;
    int mvy;
    HLV1BitWriter bits;
    MB rec;
} Candidate;

typedef struct MotionChoice {
    int mvx;
    int mvy;
    uint64_t sad;
} MotionChoice;

/* Persistent predictive state.  previous/current are swapped only after the
 * chosen packet has been committed, which makes encoder cloning deterministic. */
struct HLV1Encoder {
    HLV1Header header;
    double scene_cut;
    int q_y;
    int q_uv;
    unsigned q_shift;
    double chroma_scale;
    double lambda_scale;
    double decode_cycle_weight;
    double ac_deadzone;
    int luma_weight;
    int motion_candidates;
    int use_simd;
    unsigned adaptive_min_key_interval;
    double adaptive_keyframe_bias;
    uint32_t frames_since_key;
    uint32_t frame_index;
    uint64_t estimated_decode_cycles;
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

static int encoder_sse2_available(void) {
#if HLV1_ENCODER_SSE2 && defined(_MSC_VER)
    int registers[4];
    __cpuid(registers, 1);
    return (registers[3] & (1 << 26)) != 0;
#elif HLV1_ENCODER_SSE2 && (defined(__GNUC__) || defined(__clang__))
    return __builtin_cpu_supports("sse2") != 0;
#else
    return 0;
#endif
}

static uint64_t squared_error_u8_scalar(const uint8_t *a, const uint8_t *b,
                                        size_t count) {
    uint64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        int difference = (int)a[i] - b[i];
        sum += (uint64_t)(difference * difference);
    }
    return sum;
}

#if HLV1_ENCODER_SSE2
static uint64_t squared_error_u8_sse2(const uint8_t *a, const uint8_t *b,
                                      size_t count) {
    const __m128i zero = _mm_setzero_si128();
    __m128i sum = zero;
    size_t i = 0;
    for (; i + 16 <= count; i += 16) {
        __m128i av = _mm_loadu_si128((const __m128i *)(a + i));
        __m128i bv = _mm_loadu_si128((const __m128i *)(b + i));
        __m128i difference_low = _mm_sub_epi16(
            _mm_unpacklo_epi8(av, zero), _mm_unpacklo_epi8(bv, zero));
        __m128i difference_high = _mm_sub_epi16(
            _mm_unpackhi_epi8(av, zero), _mm_unpackhi_epi8(bv, zero));
        sum = _mm_add_epi32(
            sum, _mm_madd_epi16(difference_low, difference_low));
        sum = _mm_add_epi32(
            sum, _mm_madd_epi16(difference_high, difference_high));
    }
    uint32_t lanes[4];
    _mm_storeu_si128((__m128i *)lanes, sum);
    uint64_t result = (uint64_t)lanes[0] + lanes[1] + lanes[2] + lanes[3];
    return result + squared_error_u8_scalar(a + i, b + i, count - i);
}

static uint64_t sad_rows_u8_sse2(const uint8_t *a, int a_stride,
                                 const uint8_t *b, int b_stride,
                                 int width, int height) {
    __m128i sum = _mm_setzero_si128();
    uint64_t tail = 0;
    for (int y = 0; y < height; ++y) {
        int x = 0;
        for (; x + 16 <= width; x += 16) {
            __m128i av = _mm_loadu_si128((const __m128i *)(a + x));
            __m128i bv = _mm_loadu_si128((const __m128i *)(b + x));
            sum = _mm_add_epi64(sum, _mm_sad_epu8(av, bv));
        }
        if (x + 8 <= width) {
            __m128i av = _mm_loadl_epi64((const __m128i *)(a + x));
            __m128i bv = _mm_loadl_epi64((const __m128i *)(b + x));
            sum = _mm_add_epi64(sum, _mm_sad_epu8(av, bv));
            x += 8;
        }
        for (; x < width; ++x)
            tail += (unsigned)abs((int)a[x] - b[x]);
        a += a_stride;
        b += b_stride;
    }
    uint64_t lanes[2];
    _mm_storeu_si128((__m128i *)lanes, sum);
    return lanes[0] + lanes[1] + tail;
}

static __m128i load_motion_row_sse2(const uint8_t *row, int width) {
    return width >= 16
        ? _mm_loadu_si128((const __m128i *)row)
        : _mm_loadl_epi64((const __m128i *)row);
}

static uint64_t sad_rows_half_pixel_sse2(
    const uint8_t *a, int a_stride,
    const uint8_t *reference, int reference_stride,
    int width, int height, int fx, int fy) {
    const __m128i zero = _mm_setzero_si128();
    const __m128i round = _mm_set1_epi16(2);
    __m128i sum = zero;
    for (int y = 0; y < height; ++y) {
        __m128i av = load_motion_row_sse2(a, width);
        __m128i top_left = load_motion_row_sse2(reference, width);
        __m128i prediction;
        if (!fy) {
            __m128i top_right = load_motion_row_sse2(reference + 1, width);
            prediction = _mm_avg_epu8(top_left, top_right);
        } else if (!fx) {
            __m128i bottom_left =
                load_motion_row_sse2(reference + reference_stride, width);
            prediction = _mm_avg_epu8(top_left, bottom_left);
        } else {
            __m128i top_right = load_motion_row_sse2(reference + 1, width);
            __m128i bottom_left =
                load_motion_row_sse2(reference + reference_stride, width);
            __m128i bottom_right =
                load_motion_row_sse2(reference + reference_stride + 1,
                                     width);
            __m128i low = _mm_add_epi16(
                _mm_add_epi16(_mm_unpacklo_epi8(top_left, zero),
                              _mm_unpacklo_epi8(top_right, zero)),
                _mm_add_epi16(_mm_unpacklo_epi8(bottom_left, zero),
                              _mm_unpacklo_epi8(bottom_right, zero)));
            __m128i high = _mm_add_epi16(
                _mm_add_epi16(_mm_unpackhi_epi8(top_left, zero),
                              _mm_unpackhi_epi8(top_right, zero)),
                _mm_add_epi16(_mm_unpackhi_epi8(bottom_left, zero),
                              _mm_unpackhi_epi8(bottom_right, zero)));
            low = _mm_srli_epi16(_mm_add_epi16(low, round), 2);
            high = _mm_srli_epi16(_mm_add_epi16(high, round), 2);
            prediction = _mm_packus_epi16(low, high);
        }
        sum = _mm_add_epi64(sum, _mm_sad_epu8(av, prediction));
        a += a_stride;
        reference += reference_stride;
    }
    uint64_t lanes[2];
    _mm_storeu_si128((__m128i *)lanes, sum);
    return lanes[0] + lanes[1];
}
#endif

static uint64_t squared_error_u8(const uint8_t *a, const uint8_t *b,
                                 size_t count, int use_simd) {
#if HLV1_ENCODER_SSE2
    if (use_simd) return squared_error_u8_sse2(a, b, count);
#else
    (void)use_simd;
#endif
    return squared_error_u8_scalar(a, b, count);
}

/* --- Shared predictor arithmetic --------------------------------------- */
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

/* --- Macroblock extraction and storage -------------------------------- */
static void copy_from_plane(uint8_t *dst, int w, int h,
                            const uint8_t *src, int stride) {
    for (int y = 0; y < h; ++y) memcpy(dst + y * w, src + y * stride, (size_t)w);
}

static void extract_mb(const HLV1Frame *f, int x, int y, MB *mb) {
    copy_from_plane(mb->y, 16, 16, f->y + y * f->stride_y + x, f->stride_y);
    int cx = x / 2, cy = y / 2;
    copy_from_plane(mb->u, 8, 8, f->u + cy * f->stride_u + cx, f->stride_u);
    copy_from_plane(mb->v, 8, 8, f->v + cy * f->stride_v + cx, f->stride_v);
}

static void extract_sb8(const HLV1Frame *f, int x, int y, SB8 *sb) {
    copy_from_plane(sb->y, 8, 8, f->y + y * f->stride_y + x, f->stride_y);
    int cx = x / 2, cy = y / 2;
    copy_from_plane(sb->u, 4, 4, f->u + cy * f->stride_u + cx, f->stride_u);
    copy_from_plane(sb->v, 4, 4, f->v + cy * f->stride_v + cx, f->stride_v);
}

static int floor_div(int value, int divisor) {
    int q = value / divisor;
    int r = value % divisor;
    if (r < 0) --q;
    return q;
}

/* --- Fractional-pixel motion compensation ------------------------------
 * Motion coordinates are expressed in denominator-sized subpixel units.  The
 * chroma predictor doubles that denominator because YUV420 chroma samples cover
 * two luma pixels in each dimension. */
static void predict_plane_fractional(uint8_t *dst, int dst_stride,
                                     int w, int h,
                                     const uint8_t *src, int src_stride,
                                     int origin_x_num, int origin_y_num,
                                     int denominator,
                                     HLV1EncoderWork *work) {
    int bx = floor_div(origin_x_num, denominator);
    int by = floor_div(origin_y_num, denominator);
    int fx = origin_x_num - bx * denominator;
    int fy = origin_y_num - by * denominator;
    uint64_t samples = (uint64_t)w * (uint64_t)h;
    if (work) {
        if (!fx && !fy)
            work->prediction_copied_samples += samples;
        else if (!fx || !fy)
            work->prediction_hv_samples += samples;
        else
            work->prediction_bilinear_samples += samples;
    }
    if (!fx && !fy) {
        for (int yy = 0; yy < h; ++yy)
            memcpy(dst + yy * dst_stride,
                   src + (by + yy) * src_stride + bx, (size_t)w);
        return;
    }
    if (!fy) {
        int round = denominator / 2;
        int inv_x = denominator - fx;
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
        int round = denominator / 2;
        int inv_y = denominator - fy;
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

static void motion_predict_sb8(const HLV1Frame *ref, int x, int y,
                               int mvx, int mvy, int denominator, SB8 *sb,
                               HLV1EncoderWork *work) {
    predict_plane_fractional(sb->y, 8, 8, 8, ref->y, ref->stride_y,
                             x * denominator + mvx,
                             y * denominator + mvy, denominator, work);
    int cden = denominator * 2;
    predict_plane_fractional(sb->u, 4, 4, 4, ref->u, ref->stride_u,
                             (x / 2) * cden + mvx,
                             (y / 2) * cden + mvy, cden, work);
    predict_plane_fractional(sb->v, 4, 4, 4, ref->v, ref->stride_v,
                             (x / 2) * cden + mvx,
                             (y / 2) * cden + mvy, cden, work);
}

static void store_sb8_to_mb(MB *mb, int sx, int sy, const SB8 *sb) {
    for (int yy = 0; yy < 8; ++yy)
        memcpy(mb->y + (sy + yy) * 16 + sx, sb->y + yy * 8, 8);
    int cx = sx / 2, cy = sy / 2;
    for (int yy = 0; yy < 4; ++yy) {
        memcpy(mb->u + (cy + yy) * 8 + cx, sb->u + yy * 4, 4);
        memcpy(mb->v + (cy + yy) * 8 + cx, sb->v + yy * 4, 4);
    }
}

static void store_to_plane(uint8_t *dst, int stride, const uint8_t *src,
                           int w, int h) {
    for (int y = 0; y < h; ++y) memcpy(dst + y * stride, src + y * w, (size_t)w);
}

static void store_mb(HLV1Frame *f, int x, int y, const MB *mb) {
    store_to_plane(f->y + y * f->stride_y + x, f->stride_y, mb->y, 16, 16);
    int cx = x / 2, cy = y / 2;
    store_to_plane(f->u + cy * f->stride_u + cx, f->stride_u, mb->u, 8, 8);
    store_to_plane(f->v + cy * f->stride_v + cx, f->stride_v, mb->v, 8, 8);
}

static void motion_predict(const HLV1Frame *ref, int x, int y,
                           int mvx, int mvy, int denominator, MB *mb,
                           HLV1EncoderWork *work) {
    predict_plane_fractional(mb->y, 16, 16, 16, ref->y, ref->stride_y,
                             x * denominator + mvx,
                             y * denominator + mvy, denominator, work);
    int cden = denominator * 2;
    predict_plane_fractional(mb->u, 8, 8, 8, ref->u, ref->stride_u,
                             (x / 2) * cden + mvx,
                             (y / 2) * cden + mvy, cden, work);
    predict_plane_fractional(mb->v, 8, 8, 8, ref->v, ref->stride_v,
                             (x / 2) * cden + mvx,
                             (y / 2) * cden + mvy, cden, work);
}

static void motion_predict_rect_to_mb(const HLV1Frame *ref, int x, int y,
                                      int sx, int sy, int w, int h,
                                      int mvx, int mvy, int denominator,
                                      MB *mb, HLV1EncoderWork *work) {
    predict_plane_fractional(mb->y + sy * 16 + sx, 16, w, h,
                             ref->y, ref->stride_y,
                             (x + sx) * denominator + mvx,
                             (y + sy) * denominator + mvy, denominator, work);
    int cden = denominator * 2;
    int csx = sx / 2, csy = sy / 2, cw = w / 2, ch = h / 2;
    predict_plane_fractional(mb->u + csy * 8 + csx, 8, cw, ch,
                             ref->u, ref->stride_u,
                             (x / 2 + csx) * cden + mvx,
                             (y / 2 + csy) * cden + mvy, cden, work);
    predict_plane_fractional(mb->v + csy * 8 + csx, 8, cw, ch,
                             ref->v, ref->stride_v,
                             (x / 2 + csx) * cden + mvx,
                             (y / 2 + csy) * cden + mvy, cden, work);
}

/* --- Intra and simple spatial predictors ------------------------------- */
static uint8_t intra_dc_plane(const uint8_t *p, int stride, int px, int py, int size) {
    uint32_t sum = 0; unsigned count = 0;
    if (py > 0) {
        const uint8_t *top = p + (py - 1) * stride + px;
        for (int i = 0; i < size; ++i) sum += top[i];
        count += (unsigned)size;
    }
    if (px > 0) {
        const uint8_t *left = p + py * stride + px - 1;
        for (int i = 0; i < size; ++i) sum += left[i * stride];
        count += (unsigned)size;
    }
    return count ? (uint8_t)rounded_mean_even(sum, count) : 128;
}

static void intra_predict_plane(const uint8_t *plane, int stride,
                                int px, int py, int size, int mode,
                                uint8_t *dst) {
    if (mode == HLV1_INTRA_DC) {
        memset(dst, intra_dc_plane(plane, stride, px, py, size),
               (size_t)size * size);
        return;
    }
    const uint8_t *top = py > 0 ? plane + (py - 1) * stride + px : NULL;
    const uint8_t *left = px > 0 ? plane + py * stride + px - 1 : NULL;
    if (mode == HLV1_INTRA_VERTICAL) {
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                dst[y * size + x] = top ? top[x] : 128;
        return;
    }
    if (mode == HLV1_INTRA_HORIZONTAL) {
        for (int y = 0; y < size; ++y) {
            uint8_t value = left ? left[y * stride] : 128;
            memset(dst + y * size, value, (size_t)size);
        }
        return;
    }
    /* A deliberately simple planar predictor: the top and left boundary
       contributions are blended for each sample.  It preserves gradients
       while requiring only additions and one shift in the decoder. */
    for (int y = 0; y < size; ++y) {
        uint8_t lv = left ? left[y * stride] : 128;
        for (int x = 0; x < size; ++x) {
            uint8_t tv = top ? top[x] : 128;
            dst[y * size + x] = (uint8_t)(((unsigned)tv + lv + 1U) >> 1);
        }
    }
}

static void intra_predict(const HLV1Frame *cur, int x, int y,
                          int mode, MB *mb) {
    intra_predict_plane(cur->y, cur->stride_y, x, y, 16, mode, mb->y);
    int cx = x / 2, cy = y / 2;
    intra_predict_plane(cur->u, cur->stride_u, cx, cy, 8, mode, mb->u);
    intra_predict_plane(cur->v, cur->stride_v, cx, cy, 8, mode, mb->v);
}

static void fill_predict(const MB *src, MB *pred, uint8_t means[3]) {
    uint32_t sy = 0, su = 0, sv = 0;
    for (unsigned i = 0; i < sizeof src->y; ++i) sy += src->y[i];
    for (unsigned i = 0; i < sizeof src->u; ++i) su += src->u[i];
    for (unsigned i = 0; i < sizeof src->v; ++i) sv += src->v[i];
    means[0] = (uint8_t)rounded_mean_even(sy, sizeof src->y);
    means[1] = (uint8_t)rounded_mean_even(su, sizeof src->u);
    means[2] = (uint8_t)rounded_mean_even(sv, sizeof src->v);
    memset(pred->y, means[0], sizeof pred->y);
    memset(pred->u, means[1], sizeof pred->u);
    memset(pred->v, means[2], sizeof pred->v);
}

/* Distortion metric used by macroblock RDO.  Luma receives an explicit weight
 * so encoder tuning can favor visible edge/detail preservation. */
static uint64_t weighted_sse(const MB *a, const MB *b, int luma_weight,
                             int use_simd, HLV1EncoderWork *work) {
    if (work) work->rdo_sse_samples += 384;
    uint64_t y = squared_error_u8(a->y, b->y, sizeof a->y, use_simd);
    uint64_t u = squared_error_u8(a->u, b->u, sizeof a->u, use_simd);
    uint64_t v = squared_error_u8(a->v, b->v, sizeof a->v, use_simd);
    return (uint64_t)luma_weight * y + u + v;
}

static uint64_t weighted_sse_sb8(const SB8 *a, const SB8 *b,
                                 int luma_weight, int use_simd,
                                 HLV1EncoderWork *work) {
    if (work) work->rdo_sse_samples += 96;
    uint64_t y = squared_error_u8(a->y, b->y, sizeof a->y, use_simd);
    uint64_t u = squared_error_u8(a->u, b->u, sizeof a->u, use_simd);
    uint64_t v = squared_error_u8(a->v, b->v, sizeof a->v, use_simd);
    return (uint64_t)luma_weight * y + u + v;
}

/*
 * Conservative architecture-independent decoder estimate used by RDO.
 *
 * Payload traversal is charged separately from predictor/reconstruction work.
 * Transform candidates also pay a residual syntax surcharge proportional to
 * their coded bits; this approximates coefficient VLC parsing and inverse WHT
 * without running a decoder for every encoder trial.
 */
static uint64_t estimate_candidate_decode_cycles(const Candidate *candidate) {
    uint64_t bits = candidate->bits.bit_count;
    uint64_t input_cycles = ((bits + 7U) / 8U) * 8U;
    uint64_t predictor_cycles = 0;
    uint64_t residual_cycles = 0;

    switch (candidate->mode) {
    case HLV1_MODE_SKIP:
        predictor_cycles = 384U * 2U;
        break;
    case HLV1_MODE_INTER:
    case HLV1_MODE_GLOBAL: {
        int fx = candidate->mvx & 1;
        int fy = candidate->mvy & 1;
        predictor_cycles = 384U * (!fx && !fy ? 2U : fx && fy ? 14U : 8U);
        residual_cycles = bits * 5U;
        break;
    }
    case HLV1_MODE_SPLIT_INTER:
        predictor_cycles = 384U * 14U;
        residual_cycles = bits * 5U;
        break;
    case HLV1_MODE_FILL:
        predictor_cycles = 384U * 2U;
        residual_cycles = bits > 32U ? (bits - 32U) * 5U : 0U;
        break;
    case HLV1_MODE_INTRA_DC:
        predictor_cycles = 384U * 4U;
        residual_cycles = bits > 8U ? (bits - 8U) * 5U : 0U;
        break;
    case HLV1_MODE_GRADIENT:
        predictor_cycles = 384U * 4U;
        residual_cycles = bits > 80U ? (bits - 80U) * 5U : 0U;
        break;
    case HLV1_MODE_PALETTE:
        predictor_cycles = 384U * 3U;
        break;
    case HLV1_MODE_LITERAL:
        /* The v13 literal payload is already byte-aligned and packed in the
           ESP32 frame layout, so it is copied instead of parsed bit by bit. */
        input_cycles = 272U * 2U;
        break;
    default:
        residual_cycles = bits * 5U;
        break;
    }
    return 100U + input_cycles + predictor_cycles + residual_cycles;
}

static double score_candidate(HLV1Encoder *encoder, const MB *source,
                              Candidate *candidate, double lambda_bits) {
    candidate->estimated_decode_cycles =
        estimate_candidate_decode_cycles(candidate);
    return (double)weighted_sse(source, &candidate->rec,
                                encoder->luma_weight,
                                encoder->use_simd,
                                &encoder->stats.encoder_work) +
           lambda_bits *
               ((double)candidate->bits.bit_count +
                encoder->decode_cycle_weight *
                    (double)candidate->estimated_decode_cycles);
}

/* --- Quantization and residual syntax --------------------------------- */
static int qround(int x, int step) {
    return x >= 0 ? (x + step / 2) / step : -((-x + step / 2) / step);
}

static int qround_ac(int x, int step, double deadzone) {
    int magnitude = x < 0 ? -x : x;
    if ((double)magnitude < deadzone * step) return 0;
    return qround(x, step);
}

static void encoder_bw_init(HLV1BitWriter *bw, HLV1EncoderWork *work) {
    hlv1_bw_init(bw);
    bw->encoder_work = work;
}

static void encoder_bw_init_like(HLV1BitWriter *bw,
                                 const HLV1BitWriter *parent) {
    encoder_bw_init(bw, parent ? parent->encoder_work : NULL);
}

/* v9 coefficient levels.  The overwhelmingly common +/-1 value gets a
   two-bit code.  Larger magnitudes use a small Rice code, with an escape
   before the unary prefix can become expensive. */
static int put_level_v9(HLV1BitWriter *bw, int level) {
    unsigned magnitude = (unsigned)(level < 0 ? -level : level);
    int r;
    if (!magnitude) return HLV1_ERR_ARGUMENT;
    if (magnitude == 1) {
        if ((r = hlv1_bw_put(bw, 0, 1)) < 0) return r;
        return hlv1_bw_put(bw, level < 0, 1);
    }

    if ((r = hlv1_bw_put(bw, 1, 1)) < 0) return r;
    unsigned n = magnitude - 2;
    unsigned quotient = n >> 2;
    if (quotient < 7) {
        if (quotient &&
            (r = hlv1_bw_put(bw, (1U << quotient) - 1U,
                              quotient)) < 0)
            return r;
        if ((r = hlv1_bw_put(bw, 0, 1)) < 0) return r;
        if ((r = hlv1_bw_put(bw, n & 3U, 2)) < 0) return r;
    } else {
        if ((r = hlv1_bw_put(bw, 0x7f, 7)) < 0) return r;
        if ((r = hlv1_bw_put_ue(bw, magnitude - 30U)) < 0) return r;
    }
    return hlv1_bw_put(bw, level < 0, 1);
}

static int put_coeff_block_v9(HLV1BitWriter *bw,
                              const int runs[16], const int levels[16],
                              int entries) {
    int r;
    /* DC-only blocks are common and need neither a count nor a run. */
    if (entries == 1 && runs[0] == 0) {
        if ((r = hlv1_bw_put(bw, 0, 1)) < 0) return r;
        return put_level_v9(bw, levels[0]);
    }
    if ((r = hlv1_bw_put(bw, 1, 1)) < 0) return r;
    if ((r = hlv1_bw_put_ue(bw, (uint32_t)(entries - 1))) < 0) return r;
    for (int i = 0; i < entries; ++i) {
        if ((r = hlv1_bw_put_ue(bw, (uint32_t)runs[i])) < 0) return r;
        if ((r = put_level_v9(bw, levels[i])) < 0) return r;
    }
    return HLV1_OK;
}

#if defined(_MSC_VER)
static __forceinline void reconstruct_residual_4x4(
#else
static inline __attribute__((always_inline)) void reconstruct_residual_4x4(
#endif
    uint8_t *rec, const uint8_t *pred, int stride,
    int entries, const int runs[16],
    const int32_t deq[16],
    HLV1EncoderWork *work) {
    if (!entries) {
        if (work) ++work->zero_residual_fast_blocks;
        for (int y = 0; y < 4; ++y)
            memcpy(rec + y * stride, pred + y * stride, 4);
        return;
    }
    if (entries == 1 && runs[0] == 0) {
        if (work) ++work->dc_only_fast_blocks;
        int value = deq[0];
        int delta = value >= 0 ? (value + 8) / 16
                               : -((-value + 8) / 16);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                rec[y * stride + x] = hlv1_clip8(
                    (int)pred[y * stride + x] + delta);
        return;
    }
    int16_t inv[16];
    if (work) ++work->inverse_wht_blocks;
    hlv1_wht4_inverse(deq, inv);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            rec[y * stride + x] = hlv1_clip8(
                (int)pred[y * stride + x] + inv[y * 4 + x]);
}

static int encode_plane(HLV1BitWriter *bw,
                        const uint8_t *src, const uint8_t *pred, uint8_t *rec,
                        int w, int h, int qstep, double ac_deadzone,
                        int *nonzero_blocks) {
    int dc_step = HLV1_MAX(1, qstep / 2);
    for (int by = 0; by < h; by += 4) {
        for (int bx = 0; bx < w; bx += 4) {
            int16_t residual[16];
            for (int yy = 0; yy < 4; ++yy)
                for (int xx = 0; xx < 4; ++xx)
                    residual[yy * 4 + xx] = (int16_t)((int)src[(by + yy) * w + bx + xx] - pred[(by + yy) * w + bx + xx]);
            int32_t coeff[16], qcoeff[16], deq[16];
            if (bw->encoder_work) ++bw->encoder_work->forward_wht_blocks;
            hlv1_wht4_forward(residual, coeff);
            if (bw->encoder_work)
                bw->encoder_work->quantized_coefficients += 16;
            for (int i = 0; i < 16; ++i) {
                int step = i == 0 ? dc_step : qstep;
                qcoeff[i] = i == 0 ? qround(coeff[i], step)
                                     : qround_ac(coeff[i], step, ac_deadzone);
                deq[i] = qcoeff[i] * step;
            }
            int runs[16], levels[16], entries = 0, last = -1;
            for (int pos = 0; pos < 16; ++pos) {
                int level = qcoeff[scan4[pos]];
                if (level) {
                    runs[entries] = pos - last - 1;
                    levels[entries] = level;
                    ++entries; last = pos;
                }
            }
            if (entries && nonzero_blocks) ++*nonzero_blocks;
            int r = hlv1_bw_put(bw, entries != 0, 1);
            if (r < 0) return r;
            if (entries) {
                if ((r = hlv1_bw_put_ue(bw, (uint32_t)(entries - 1))) < 0) return r;
                for (int i = 0; i < entries; ++i) {
                    if ((r = hlv1_bw_put_ue(bw, (uint32_t)runs[i])) < 0) return r;
                    if ((r = hlv1_bw_put_se(bw, levels[i])) < 0) return r;
                }
            }
            reconstruct_residual_4x4(
                rec + by * w + bx, pred + by * w + bx, w,
                entries, runs, deq, bw->encoder_work);
        }
    }
    return HLV1_OK;
}


static int encode_plane_masked(HLV1BitWriter *coeff_bits,
                               HLV1BitWriter *coeff_bits_v9,
                               const uint8_t *src, const uint8_t *pred,
                               uint8_t *rec, int w, int h, int qstep,
                               double ac_deadzone, uint32_t *mask,
                               int *block_index, int *nonzero_blocks) {
    int dc_step = HLV1_MAX(1, qstep / 2);
    for (int by = 0; by < h; by += 4) {
        for (int bx = 0; bx < w; bx += 4, ++*block_index) {
            int16_t residual[16];
            for (int yy = 0; yy < 4; ++yy)
                for (int xx = 0; xx < 4; ++xx)
                    residual[yy * 4 + xx] = (int16_t)(
                        (int)src[(by + yy) * w + bx + xx] -
                        pred[(by + yy) * w + bx + xx]);
            int32_t coeff[16], qcoeff[16], deq[16];
            if (coeff_bits->encoder_work)
                ++coeff_bits->encoder_work->forward_wht_blocks;
            hlv1_wht4_forward(residual, coeff);
            if (coeff_bits->encoder_work)
                coeff_bits->encoder_work->quantized_coefficients += 16;
            for (int i = 0; i < 16; ++i) {
                int step = i == 0 ? dc_step : qstep;
                qcoeff[i] = i == 0 ? qround(coeff[i], step)
                                     : qround_ac(coeff[i], step, ac_deadzone);
                deq[i] = qcoeff[i] * step;
            }
            int runs[16], levels[16], entries = 0, last = -1;
            for (int pos = 0; pos < 16; ++pos) {
                int level = qcoeff[scan4[pos]];
                if (level) {
                    runs[entries] = pos - last - 1;
                    levels[entries] = level;
                    ++entries;
                    last = pos;
                }
            }
            if (entries) {
                *mask |= UINT32_C(1) << *block_index;
                ++*nonzero_blocks;
                int r = hlv1_bw_put_ue(coeff_bits, (uint32_t)(entries - 1));
                if (r < 0) return r;
                for (int i = 0; i < entries; ++i) {
                    if ((r = hlv1_bw_put_ue(coeff_bits,
                                            (uint32_t)runs[i])) < 0)
                        return r;
                    if ((r = hlv1_bw_put_se(coeff_bits, levels[i])) < 0)
                        return r;
                }
                if (coeff_bits_v9 &&
                    (r = put_coeff_block_v9(coeff_bits_v9, runs, levels,
                                             entries)) < 0)
                    return r;
            }
            reconstruct_residual_4x4(
                rec + by * w + bx, pred + by * w + bx, w,
                entries, runs, deq, coeff_bits->encoder_work);
        }
    }
    return HLV1_OK;
}

static int put_residual_mask(HLV1BitWriter *dst, uint32_t mask,
                             int block_count, int nonzero_blocks) {
    /* The last block is used as a pivot.  A raw mask whose pivot is zero
       costs exactly block_count bits, equal to the legacy per-block flags:
         0 + remaining bits
       A pivot-one raw mask costs one extra bit:
         10 + remaining bits
       Sparse masks use 11 followed by count and position gaps.  This makes
       v8 effectively free on the common pivot-zero path while still allowing
       large savings for sparse residuals. */
    HLV1BitWriter raw, sparse;
    encoder_bw_init_like(&raw, dst);
    encoder_bw_init_like(&sparse, dst);
    int pivot = block_count - 1;
    uint32_t lower_mask = pivot == 32 ? UINT32_MAX
                                      : ((UINT32_C(1) << pivot) - 1U);
    int pivot_set = (mask & (UINT32_C(1) << pivot)) != 0;
    int r = hlv1_bw_put(&raw, pivot_set ? 2U : 0U,
                        pivot_set ? 2U : 1U);
    if (r >= 0)
        r = hlv1_bw_put(&raw, mask & lower_mask, (unsigned)pivot);
    if (r >= 0) r = hlv1_bw_finish(&raw);

    int rs = hlv1_bw_put(&sparse, 3, 2);
    if (rs >= 0)
        rs = hlv1_bw_put_ue(&sparse, (uint32_t)(nonzero_blocks - 1));
    int previous = -1;
    for (int i = 0; rs >= 0 && i < block_count; ++i) {
        if (!(mask & (UINT32_C(1) << i))) continue;
        rs = hlv1_bw_put_ue(&sparse, (uint32_t)(i - previous - 1));
        previous = i;
    }
    if (rs >= 0) rs = hlv1_bw_finish(&sparse);
    if (r < 0 || rs < 0) {
        hlv1_bw_free(&raw);
        hlv1_bw_free(&sparse);
        return r < 0 ? r : rs;
    }
    const HLV1BitWriter *best =
        sparse.bit_count < raw.bit_count ? &sparse : &raw;
    r = hlv1_bw_append(dst, best);
    hlv1_bw_free(&raw);
    hlv1_bw_free(&sparse);
    return r;
}

static int put_sparse_mask(HLV1BitWriter *bw, uint32_t mask,
                           int block_count, int nonzero_blocks,
                           uint32_t prefix, unsigned prefix_bits) {
    int r = hlv1_bw_put(bw, prefix, prefix_bits);
    if (r >= 0)
        r = hlv1_bw_put_ue(bw, (uint32_t)(nonzero_blocks - 1));
    int previous = -1;
    for (int i = 0; r >= 0 && i < block_count; ++i) {
        if (!(mask & (UINT32_C(1) << i))) continue;
        r = hlv1_bw_put_ue(bw, (uint32_t)(i - previous - 1));
        previous = i;
    }
    return r;
}

/* v9 chooses the complete residual representation, not just the block mask.
   Raw masks retain legacy coefficient coding.  Sparse masks have separate
   prefixes for legacy and v9 coefficient tokens, so the encoder can select
   whichever complete representation is shortest. */
static int put_residual_group_v9(HLV1BitWriter *dst, uint32_t mask,
                                 int block_count, int nonzero_blocks,
                                 const HLV1BitWriter *legacy_coeff,
                                 const HLV1BitWriter *v9_coeff) {
    HLV1BitWriter raw, sparse_legacy, sparse_v9;
    encoder_bw_init_like(&raw, dst);
    encoder_bw_init_like(&sparse_legacy, dst);
    encoder_bw_init_like(&sparse_v9, dst);

    int pivot = block_count - 1;
    uint32_t lower_mask = (UINT32_C(1) << pivot) - 1U;
    int pivot_set = (mask & (UINT32_C(1) << pivot)) != 0;
    int r = hlv1_bw_put(&raw, pivot_set ? 2U : 0U,
                        pivot_set ? 2U : 1U);
    if (r >= 0)
        r = hlv1_bw_put(&raw, mask & lower_mask, (unsigned)pivot);
    if (r >= 0) r = hlv1_bw_append(&raw, legacy_coeff);
    if (r >= 0) r = hlv1_bw_finish(&raw);

    int rl = put_sparse_mask(&sparse_legacy, mask, block_count,
                             nonzero_blocks, 6, 3); /* 110 */
    if (rl >= 0) rl = hlv1_bw_append(&sparse_legacy, legacy_coeff);
    if (rl >= 0) rl = hlv1_bw_finish(&sparse_legacy);

    int rv = put_sparse_mask(&sparse_v9, mask, block_count,
                             nonzero_blocks, 7, 3); /* 111 */
    if (rv >= 0) rv = hlv1_bw_append(&sparse_v9, v9_coeff);
    if (rv >= 0) rv = hlv1_bw_finish(&sparse_v9);

    if (r < 0 || rl < 0 || rv < 0) {
        hlv1_bw_free(&raw);
        hlv1_bw_free(&sparse_legacy);
        hlv1_bw_free(&sparse_v9);
        return r < 0 ? r : (rl < 0 ? rl : rv);
    }

    const HLV1BitWriter *best = &raw;
    if (sparse_legacy.bit_count < best->bit_count) best = &sparse_legacy;
    if (sparse_v9.bit_count < best->bit_count) best = &sparse_v9;
    r = hlv1_bw_append(dst, best);
    hlv1_bw_free(&raw);
    hlv1_bw_free(&sparse_legacy);
    hlv1_bw_free(&sparse_v9);
    return r;
}

/*
 * Experimental v14 residual groups have one directly readable coefficient
 * mode bit followed by the complete block mask.  The encoder still chooses
 * the shorter coefficient representation, but the decoder no longer walks
 * the raw/sparse mask decision tree for every residual macroblock.
 */
static int put_residual_group_v14(HLV1BitWriter *dst, uint32_t mask,
                                  int block_count,
                                  const HLV1BitWriter *legacy_coeff,
                                  const HLV1BitWriter *v9_coeff) {
    int use_v9 = v9_coeff->bit_count < legacy_coeff->bit_count;
    int r = hlv1_bw_put(dst, (uint32_t)use_v9, 1);
    if (r >= 0)
        r = hlv1_bw_put(dst, mask, (unsigned)block_count);
    if (r >= 0)
        r = hlv1_bw_append(dst, use_v9 ? v9_coeff : legacy_coeff);
    return r;
}

static int encode_residual_masked(HLV1BitWriter *bw,
                                  const MB *src, const MB *pred, MB *rec,
                                  int qy, int quv, double ac_deadzone,
                                  int *nonzero_blocks) {
    HLV1BitWriter coeff_bits;
    encoder_bw_init_like(&coeff_bits, bw);
    uint32_t mask = 0;
    int index = 0, nz = 0;
    int r = encode_plane_masked(&coeff_bits, NULL,
                                src->y, pred->y, rec->y,
                                16, 16, qy, ac_deadzone,
                                &mask, &index, &nz);
    if (r >= 0)
        r = encode_plane_masked(&coeff_bits, NULL,
                                src->u, pred->u, rec->u,
                                8, 8, quv, ac_deadzone,
                                &mask, &index, &nz);
    if (r >= 0)
        r = encode_plane_masked(&coeff_bits, NULL,
                                src->v, pred->v, rec->v,
                                8, 8, quv, ac_deadzone,
                                &mask, &index, &nz);
    if (r >= 0) r = hlv1_bw_finish(&coeff_bits);
    if (r >= 0 && nz) r = put_residual_mask(bw, mask, 24, nz);
    if (r >= 0 && nz) r = hlv1_bw_append(bw, &coeff_bits);
    if (nonzero_blocks) *nonzero_blocks = nz;
    hlv1_bw_free(&coeff_bits);
    return r;
}

static int encode_residual_sb8_masked(HLV1BitWriter *bw,
                                      const SB8 *src, const SB8 *pred,
                                      SB8 *rec, int qy, int quv,
                                      double ac_deadzone,
                                      int *nonzero_blocks) {
    HLV1BitWriter coeff_bits;
    encoder_bw_init_like(&coeff_bits, bw);
    uint32_t mask = 0;
    int index = 0, nz = 0;
    int r = encode_plane_masked(&coeff_bits, NULL,
                                src->y, pred->y, rec->y,
                                8, 8, qy, ac_deadzone,
                                &mask, &index, &nz);
    if (r >= 0)
        r = encode_plane_masked(&coeff_bits, NULL,
                                src->u, pred->u, rec->u,
                                4, 4, quv, ac_deadzone,
                                &mask, &index, &nz);
    if (r >= 0)
        r = encode_plane_masked(&coeff_bits, NULL,
                                src->v, pred->v, rec->v,
                                4, 4, quv, ac_deadzone,
                                &mask, &index, &nz);
    if (r >= 0) r = hlv1_bw_finish(&coeff_bits);
    if (r >= 0 && nz) r = put_residual_mask(bw, mask, 6, nz);
    if (r >= 0 && nz) r = hlv1_bw_append(bw, &coeff_bits);
    if (nonzero_blocks) *nonzero_blocks = nz;
    hlv1_bw_free(&coeff_bits);
    return r;
}

static int encode_residual_v9(HLV1BitWriter *bw, unsigned version,
                              const MB *src, const MB *pred, MB *rec,
                              int qy, int quv, double ac_deadzone,
                              int *nonzero_blocks) {
    HLV1BitWriter legacy, vlc;
    encoder_bw_init_like(&legacy, bw);
    encoder_bw_init_like(&vlc, bw);
    uint32_t mask = 0;
    int index = 0, nz = 0;
    int r = encode_plane_masked(&legacy, &vlc,
                                src->y, pred->y, rec->y,
                                16, 16, qy, ac_deadzone,
                                &mask, &index, &nz);
    if (r >= 0)
        r = encode_plane_masked(&legacy, &vlc,
                                src->u, pred->u, rec->u,
                                8, 8, quv, ac_deadzone,
                                &mask, &index, &nz);
    if (r >= 0)
        r = encode_plane_masked(&legacy, &vlc,
                                src->v, pred->v, rec->v,
                                8, 8, quv, ac_deadzone,
                                &mask, &index, &nz);
    if (r >= 0) r = hlv1_bw_finish(&legacy);
    if (r >= 0) r = hlv1_bw_finish(&vlc);
    if (r >= 0 && nz)
        r = version >= HLV1_STREAM_VERSION_14
                ? put_residual_group_v14(bw, mask, 24, &legacy, &vlc)
                : put_residual_group_v9(bw, mask, 24, nz, &legacy, &vlc);
    if (nonzero_blocks) *nonzero_blocks = nz;
    hlv1_bw_free(&legacy);
    hlv1_bw_free(&vlc);
    return r;
}

static int encode_residual_sb8_v9(HLV1BitWriter *bw, unsigned version,
                                  const SB8 *src, const SB8 *pred, SB8 *rec,
                                  int qy, int quv, double ac_deadzone,
                                  int *nonzero_blocks) {
    HLV1BitWriter legacy, vlc;
    encoder_bw_init_like(&legacy, bw);
    encoder_bw_init_like(&vlc, bw);
    uint32_t mask = 0;
    int index = 0, nz = 0;
    int r = encode_plane_masked(&legacy, &vlc,
                                src->y, pred->y, rec->y,
                                8, 8, qy, ac_deadzone,
                                &mask, &index, &nz);
    if (r >= 0)
        r = encode_plane_masked(&legacy, &vlc,
                                src->u, pred->u, rec->u,
                                4, 4, quv, ac_deadzone,
                                &mask, &index, &nz);
    if (r >= 0)
        r = encode_plane_masked(&legacy, &vlc,
                                src->v, pred->v, rec->v,
                                4, 4, quv, ac_deadzone,
                                &mask, &index, &nz);
    if (r >= 0) r = hlv1_bw_finish(&legacy);
    if (r >= 0) r = hlv1_bw_finish(&vlc);
    if (r >= 0 && nz)
        r = version >= HLV1_STREAM_VERSION_14
                ? put_residual_group_v14(bw, mask, 6, &legacy, &vlc)
                : put_residual_group_v9(bw, mask, 6, nz, &legacy, &vlc);
    if (nonzero_blocks) *nonzero_blocks = nz;
    hlv1_bw_free(&legacy);
    hlv1_bw_free(&vlc);
    return r;
}

static int encode_residual(HLV1BitWriter *bw, const MB *src, const MB *pred,
                           MB *rec, int qy, int quv, double ac_deadzone,
                           int *nonzero_blocks) {
    int nz = 0;
    int r = encode_plane(bw, src->y, pred->y, rec->y, 16, 16, qy, ac_deadzone, &nz);
    if (r < 0) return r;
    r = encode_plane(bw, src->u, pred->u, rec->u, 8, 8, quv, ac_deadzone, &nz);
    if (r < 0) return r;
    r = encode_plane(bw, src->v, pred->v, rec->v, 8, 8, quv, ac_deadzone, &nz);
    if (nonzero_blocks) *nonzero_blocks = nz;
    return r;
}

static int encode_residual_sb8(HLV1BitWriter *bw, const SB8 *src, const SB8 *pred,
                               SB8 *rec, int qy, int quv, double ac_deadzone,
                               int *nonzero_blocks) {
    int nz = 0;
    int r = encode_plane(bw, src->y, pred->y, rec->y, 8, 8, qy, ac_deadzone, &nz);
    if (r < 0) return r;
    r = encode_plane(bw, src->u, pred->u, rec->u, 4, 4, quv, ac_deadzone, &nz);
    if (r < 0) return r;
    r = encode_plane(bw, src->v, pred->v, rec->v, 4, 4, quv, ac_deadzone, &nz);
    if (nonzero_blocks) *nonzero_blocks = nz;
    return r;
}


static int put_mode(HLV1BitWriter *bw, unsigned version, int frame_type,
                    int mode, int use_global);
static int put_residual(HLV1BitWriter *dst, unsigned version,
                        const MB *src, const MB *pred, MB *rec,
                        int qy, int quv, double ac_deadzone,
                        int *had_residual);

/* v13 literal rows use the same little-endian Y6/U5/V5 packing as the ESP32
 * compact reference frame.  Reconstruct the quantized samples here so future
 * P-frames see exactly the values produced by every decoder. */
static int put_literal_row(HLV1BitWriter *bw, const uint8_t *source,
                           uint8_t *reconstructed, int samples,
                           unsigned sample_bits) {
    uint8_t packed[12] = {0};
    unsigned output_shift = 8U - sample_bits;
    unsigned maximum = (1U << sample_bits) - 1U;
    unsigned bit = 0;
    for (int i = 0; i < samples; ++i) {
        unsigned code =
            ((unsigned)source[i] + (1U << (output_shift - 1U))) >>
            output_shift;
        if (code > maximum) code = maximum;
        reconstructed[i] = (uint8_t)(code << output_shift);
        for (unsigned b = 0; b < sample_bits; ++b, ++bit)
            packed[bit >> 3] |=
                (uint8_t)(((code >> b) & 1U) << (bit & 7U));
    }
    size_t bytes = ((size_t)samples * sample_bits + 7U) / 8U;
    for (size_t i = 0; i < bytes; ++i) {
        int r = hlv1_bw_put(bw, packed[i], 8);
        if (r < 0) return r;
    }
    return HLV1_OK;
}

static int encode_literal_candidate(const MB *source, unsigned version,
                                    int frame_type, int use_global,
                                    Candidate *out) {
    out->mode = HLV1_MODE_LITERAL;
    int r = put_mode(&out->bits, version, frame_type,
                     HLV1_MODE_LITERAL, use_global);
    /* All v13 macroblocks begin on a byte boundary.  The fixed four-bit mode
       therefore needs exactly four zero bits before the byte-copy payload. */
    if (r >= 0) r = hlv1_bw_put(&out->bits, 0, 4);
    for (int y = 0; r >= 0 && y < 16; ++y)
        r = put_literal_row(&out->bits, source->y + y * 16,
                            out->rec.y + y * 16, 16, 6);
    for (int y = 0; r >= 0 && y < 8; ++y)
        r = put_literal_row(&out->bits, source->u + y * 8,
                            out->rec.u + y * 8, 8, 5);
    for (int y = 0; r >= 0 && y < 8; ++y)
        r = put_literal_row(&out->bits, source->v + y * 8,
                            out->rec.v + y * 8, 8, 5);
    if (r >= 0) r = hlv1_bw_finish(&out->bits);
    return r;
}

/* --- Screen-content and low-complexity block models -------------------- */
static int palette_axis_minimum_colors(const uint8_t *samples, size_t count,
                                       int error_limit,
                                       HLV1EncoderWork *work) {
    uint8_t present[256] = {0};
    for (size_t i = 0; i < count; ++i)
        present[samples[i]] = 1;
    if (work) work->palette_prefilter_samples += count;

    int colors = 0;
    int value = 0;
    uint64_t scanned = 0;
    while (value < 256) {
        while (value < 256 && !present[value]) {
            ++value;
            ++scanned;
        }
        if (value >= 256) break;
        ++colors;
        int covered = value + 2 * error_limit;
        do {
            ++value;
            ++scanned;
        } while (value < 256 && value <= covered);
    }
    if (work) work->palette_prefilter_bins += scanned;
    return colors;
}

static int palette_minimum_colors(const MB *src, HLV1EncoderWork *work) {
    int y = palette_axis_minimum_colors(
        src->y, sizeof src->y, 10, work);
    int u = palette_axis_minimum_colors(
        src->u, sizeof src->u, 12, work);
    int v = palette_axis_minimum_colors(
        src->v, sizeof src->v, 12, work);
    return HLV1_MAX(y, HLV1_MAX(u, v));
}

static unsigned palette_distance(int y, int u, int v,
                                 const PaletteColor *color,
                                 HLV1EncoderWork *work) {
    if (work) ++work->palette_distance_evaluations;
    int dy = y - color->y;
    int du = u - color->u;
    int dv = v - color->v;
    return (unsigned)(4 * dy * dy + du * du + dv * dv);
}

static int palette_nearest(int y, int u, int v,
                           const PaletteColor *colors, int count,
                           HLV1EncoderWork *work) {
    unsigned best = UINT_MAX;
    int index = 0;
    for (int i = 0; i < count; ++i) {
        unsigned distance = palette_distance(y, u, v, &colors[i], work);
        if (distance < best) {
            best = distance;
            index = i;
        }
    }
    return index;
}

static int palette_build(const MB *src, int count,
                         PaletteColor colors[8],
                         uint8_t y_index[256], uint8_t c_index[64],
                         MB *rec, HLV1EncoderWork *work) {
    int min_i = 0, max_i = 0;
    for (int i = 1; i < 256; ++i) {
        if (src->y[i] < src->y[min_i]) min_i = i;
        if (src->y[i] > src->y[max_i]) max_i = i;
    }
    int initial[4] = {min_i, max_i, 0, 0};
    for (int k = 0; k < 2; ++k) {
        int i = initial[k];
        int x = i & 15, y = i >> 4;
        colors[k].y = src->y[i];
        colors[k].u = src->u[(y >> 1) * 8 + (x >> 1)];
        colors[k].v = src->v[(y >> 1) * 8 + (x >> 1)];
    }
    for (int k = 2; k < count; ++k) {
        unsigned farthest = 0;
        int farthest_i = 0;
        for (int i = 0; i < 256; ++i) {
            int x = i & 15, y = i >> 4;
            int u = src->u[(y >> 1) * 8 + (x >> 1)];
            int v = src->v[(y >> 1) * 8 + (x >> 1)];
            unsigned nearest = UINT_MAX;
            for (int j = 0; j < k; ++j) {
                unsigned distance = palette_distance(
                    src->y[i], u, v, &colors[j], work);
                if (distance < nearest) nearest = distance;
            }
            if (nearest > farthest) {
                farthest = nearest;
                farthest_i = i;
            }
        }
        int x = farthest_i & 15, y = farthest_i >> 4;
        colors[k].y = src->y[farthest_i];
        colors[k].u = src->u[(y >> 1) * 8 + (x >> 1)];
        colors[k].v = src->v[(y >> 1) * 8 + (x >> 1)];
    }

    for (int iteration = 0; iteration < 6; ++iteration) {
        uint32_t sy[8] = {0}, su[8] = {0}, sv[8] = {0}, n[8] = {0};
        for (int i = 0; i < 256; ++i) {
            int x = i & 15, y = i >> 4;
            int u = src->u[(y >> 1) * 8 + (x >> 1)];
            int v = src->v[(y >> 1) * 8 + (x >> 1)];
            int index = palette_nearest(
                src->y[i], u, v, colors, count, work);
            sy[index] += src->y[i];
            su[index] += (unsigned)u;
            sv[index] += (unsigned)v;
            n[index]++;
        }
        for (int k = 0; k < count; ++k) if (n[k]) {
            colors[k].y = (uint8_t)rounded_mean_even(sy[k], n[k]);
            colors[k].u = (uint8_t)rounded_mean_even(su[k], n[k]);
            colors[k].v = (uint8_t)rounded_mean_even(sv[k], n[k]);
        }
    }

    for (int i = 0; i < 256; ++i) {
        int x = i & 15, y = i >> 4;
        int u = src->u[(y >> 1) * 8 + (x >> 1)];
        int v = src->v[(y >> 1) * 8 + (x >> 1)];
        int index = palette_nearest(src->y[i], u, v, colors, count, work);
        if (abs((int)src->y[i] - colors[index].y) > 10)
            return 0;
        y_index[i] = (uint8_t)index;
        rec->y[i] = colors[index].y;
    }
    for (int cy = 0; cy < 8; ++cy) {
        for (int cx = 0; cx < 8; ++cx) {
            int ysum = 0;
            for (int yy = 0; yy < 2; ++yy)
                for (int xx = 0; xx < 2; ++xx)
                    ysum += src->y[(cy * 2 + yy) * 16 + cx * 2 + xx];
            int yavg = (ysum + 2) >> 2;
            int i = cy * 8 + cx;
            int index = palette_nearest(
                yavg, src->u[i], src->v[i], colors, count, work);
            if (abs((int)src->u[i] - colors[index].u) > 12 ||
                abs((int)src->v[i] - colors[index].v) > 12)
                return 0;
            c_index[i] = (uint8_t)index;
            rec->u[i] = colors[index].u;
            rec->v[i] = colors[index].v;
        }
    }
    return 1;
}

static int encode_palette_candidate(const MB *src, unsigned version,
                                    int frame_type, int use_global,
                                    int count, int qy, int quv,
                                    double lambda_bits,
                                    int luma_weight, int use_simd,
                                    HLV1EncoderWork *work,
                                    Candidate *out) {
    PaletteColor colors[8] = {{0}};
    uint8_t y_index[256], c_index[64];
    /* Palette blocks are visually harsh when a rare color is merged into a
       distant cluster.  Reject those blocks even if the bit-only RDO score
       would accept them.  This is encoder-only and keeps the decoder trivial. */
    (void)qy;
    (void)quv;
    if (!palette_build(src, count, colors, y_index, c_index, &out->rec,
                       work)) {
        out->mode = HLV1_MODE_PALETTE;
        out->score = HUGE_VAL;
        return HLV1_OK;
    }
    out->mode = HLV1_MODE_PALETTE;
    out->palette_size = count;
    int r = put_mode(&out->bits, version, frame_type,
                     HLV1_MODE_PALETTE, use_global);
    if (r >= 0) {
        if (version >= HLV1_STREAM_VERSION_13) {
            uint32_t count_code =
                count == 2 ? 0U : count == 4 ? 1U : 2U;
            r = hlv1_bw_put(&out->bits, count_code, 2);
        } else {
            r = hlv1_bw_put(&out->bits, count == 4, 1);
        }
    }
    for (int i = 0; r >= 0 && i < count; ++i) {
        r = hlv1_bw_put(&out->bits, colors[i].y, 8);
        if (r >= 0) r = hlv1_bw_put(&out->bits, colors[i].u, 8);
        if (r >= 0) r = hlv1_bw_put(&out->bits, colors[i].v, 8);
    }
    unsigned index_bits = count == 2 ? 1U : count == 4 ? 2U : 3U;
    for (int i = 0; r >= 0 && i < 256; ++i)
        r = hlv1_bw_put(&out->bits, y_index[i], index_bits);
    for (int i = 0; r >= 0 && i < 64; ++i)
        r = hlv1_bw_put(&out->bits, c_index[i], index_bits);
    if (r >= 0) r = hlv1_bw_finish(&out->bits);
    if (r < 0) return r;
    out->score = (double)weighted_sse(src, &out->rec, luma_weight,
                                      use_simd, work) +
                 lambda_bits * out->bits.bit_count;
    return HLV1_OK;
}

/* --- Gradient experiment helpers --------------------------------------- */
static int round_div_signed(int value, int divisor) {
    if (value >= 0) return (value + divisor / 2) / divisor;
    return -((-value + divisor / 2) / divisor);
}

static void gradient_predict_plane(uint8_t *dst, int stride, int w, int h,
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

static uint64_t gradient_plane_sse(const uint8_t *src, int stride,
                                   int w, int h, int base, int dx, int dy) {
    int xterm[16], yterm[16];
    for (int x = 0; x < w; ++x)
        xterm[x] = w > 1 ? round_div_signed(dx * x, w - 1) : 0;
    for (int y = 0; y < h; ++y)
        yterm[y] = h > 1 ? round_div_signed(dy * y, h - 1) : 0;
    uint64_t sse = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int prediction = HLV1_MAX(0, HLV1_MIN(255,
                base + xterm[x] + yterm[y]));
            int error = (int)src[y * stride + x] - prediction;
            sse += (uint64_t)(error * error);
        }
    return sse;
}

static void gradient_fit_plane(const uint8_t *src, int stride, int w, int h,
                               uint8_t *base_out, int8_t *dx_out,
                               int8_t *dy_out, uint8_t *pred,
                               int pred_stride) {
    int64_t sum = 0, num_x = 0, num_y = 0;
    int64_t den_x = 0, den_y = 0;
    for (int x = 0; x < w; ++x) {
        int z = 2 * x - (w - 1);
        den_x += (int64_t)z * z;
    }
    for (int y = 0; y < h; ++y) {
        int z = 2 * y - (h - 1);
        den_y += (int64_t)z * z;
    }
    for (int y = 0; y < h; ++y) {
        int zy = 2 * y - (h - 1);
        for (int x = 0; x < w; ++x) {
            int value = src[y * stride + x];
            int zx = 2 * x - (w - 1);
            sum += value;
            num_x += (int64_t)zx * value;
            num_y += (int64_t)zy * value;
        }
    }
    int mean = (int)rounded_mean_even((uint32_t)sum, (unsigned)(w * h));
    int dx0 = den_x ? round_div_signed(
        (int)(2 * (w - 1) * num_x), (int)(h * den_x)) : 0;
    int dy0 = den_y ? round_div_signed(
        (int)(2 * (h - 1) * num_y), (int)(w * den_y)) : 0;
    dx0 = HLV1_MAX(-127, HLV1_MIN(127, dx0));
    dy0 = HLV1_MAX(-127, HLV1_MIN(127, dy0));

    uint64_t best_sse = UINT64_MAX;
    int best_base = mean, best_dx = dx0, best_dy = dy0;
    /* The decoder model is fixed.  The encoder alone refines the slopes in
       a small neighbourhood and derives the optimal intercept for each pair. */
    for (int oy = -2; oy <= 2; ++oy) {
        int dy = HLV1_MAX(-127, HLV1_MIN(127, dy0 + oy));
        int ysum = 0;
        for (int y = 0; y < h; ++y)
            ysum += h > 1 ? round_div_signed(dy * y, h - 1) : 0;
        for (int ox = -2; ox <= 2; ++ox) {
            int dx = HLV1_MAX(-127, HLV1_MIN(127, dx0 + ox));
            int xsum = 0;
            for (int x = 0; x < w; ++x)
                xsum += w > 1 ? round_div_signed(dx * x, w - 1) : 0;
            int offset_mean = round_div_signed(xsum, w) +
                              round_div_signed(ysum, h);
            int base0 = HLV1_MAX(0, HLV1_MIN(255, mean - offset_mean));
            for (int ob = -1; ob <= 1; ++ob) {
                int base = HLV1_MAX(0, HLV1_MIN(255, base0 + ob));
                uint64_t sse = gradient_plane_sse(src, stride, w, h,
                                                  base, dx, dy);
                if (sse < best_sse) {
                    best_sse = sse;
                    best_base = base;
                    best_dx = dx;
                    best_dy = dy;
                }
            }
        }
    }
    *base_out = (uint8_t)best_base;
    *dx_out = (int8_t)best_dx;
    *dy_out = (int8_t)best_dy;
    gradient_predict_plane(pred, pred_stride, w, h,
                           best_base, best_dx, best_dy);
}

static void gradient_fit_mb(const MB *src, MB *pred,
                            uint8_t base[3], int8_t dx[3], int8_t dy[3]) {
    gradient_fit_plane(src->y, 16, 16, 16, &base[0], &dx[0], &dy[0],
                       pred->y, 16);
    gradient_fit_plane(src->u, 8, 8, 8, &base[1], &dx[1], &dy[1],
                       pred->u, 8);
    gradient_fit_plane(src->v, 8, 8, 8, &base[2], &dx[2], &dy[2],
                       pred->v, 8);
}

static int encode_gradient_candidate(HLV1Encoder *e, const MB *src,
                                     unsigned version, int frame_type,
                                     int use_global, double lambda_bits,
                                     Candidate *out) {
    MB pred;
    uint8_t base[3];
    int8_t dx[3], dy[3];
    gradient_fit_mb(src, &pred, base, dx, dy);
    out->mode = HLV1_MODE_GRADIENT;
    int r = put_mode(&out->bits, version, frame_type,
                     HLV1_MODE_GRADIENT, use_global);
    for (int i = 0; r >= 0 && i < 3; ++i) {
        r = hlv1_bw_put(&out->bits, base[i], 8);
        if (r >= 0) r = hlv1_bw_put(&out->bits, (uint8_t)dx[i], 8);
        if (r >= 0) r = hlv1_bw_put(&out->bits, (uint8_t)dy[i], 8);
    }
    if (r >= 0)
        r = put_residual(&out->bits, version, src, &pred, &out->rec,
                         e->q_y, e->q_uv, e->ac_deadzone, NULL);
    if (r >= 0) r = hlv1_bw_finish(&out->bits);
    if (r < 0) return r;
    out->score = (double)weighted_sse(src, &out->rec, e->luma_weight,
                                      e->use_simd,
                                      &e->stats.encoder_work) +
                 lambda_bits * out->bits.bit_count;
    return HLV1_OK;
}

static int put_mode(HLV1BitWriter *bw, unsigned version, int frame_type,
                    int mode, int use_global) {
    if (version >= HLV1_STREAM_VERSION_13) {
        if (mode < HLV1_MODE_SKIP || mode > HLV1_MODE_LITERAL)
            return HLV1_ERR_ARGUMENT;
        return hlv1_bw_put(bw, (uint32_t)mode, 4);
    }
    if (version < HLV1_STREAM_VERSION_2)
        return hlv1_bw_put(bw, (uint32_t)mode, 2);
    if (frame_type == HLV1_FRAME_KEY) {
        if (version >= HLV1_STREAM_VERSION_12) {
            if (mode == HLV1_MODE_INTRA_DC) return hlv1_bw_put(bw, 0, 1);   /* 0 */
            if (mode == HLV1_MODE_FILL) return hlv1_bw_put(bw, 2, 2);       /* 10 */
            if (mode == HLV1_MODE_PALETTE) return hlv1_bw_put(bw, 3, 2);    /* 11 */
            return HLV1_ERR_ARGUMENT;
        }
        if (mode == HLV1_MODE_INTRA_DC) return hlv1_bw_put(bw, 0, 1);
        if (mode == HLV1_MODE_FILL) return hlv1_bw_put(bw, 1, 1);
        return HLV1_ERR_ARGUMENT;
    }
    if (version >= HLV1_STREAM_VERSION_12 && use_global) {
        switch (mode) {
        case HLV1_MODE_SKIP:        return hlv1_bw_put(bw, 0, 1);       /* 0 */
        case HLV1_MODE_GLOBAL:      return hlv1_bw_put(bw, 2, 2);       /* 10 */
        case HLV1_MODE_INTER:       return hlv1_bw_put(bw, 6, 3);       /* 110 */
        case HLV1_MODE_SPLIT_INTER: return hlv1_bw_put(bw, 14, 4);      /* 1110 */
        case HLV1_MODE_INTRA_DC:    return hlv1_bw_put(bw, 30, 5);      /* 11110 */
        case HLV1_MODE_FILL:        return hlv1_bw_put(bw, 62, 6);      /* 111110 */
        case HLV1_MODE_PALETTE:     return hlv1_bw_put(bw, 63, 6);      /* 111111 */
        default:                    return HLV1_ERR_ARGUMENT;
        }
    }
    if (version >= HLV1_STREAM_VERSION_7 && use_global) {
        switch (mode) {
        case HLV1_MODE_SKIP:        return hlv1_bw_put(bw, 0, 1);       /* 0 */
        case HLV1_MODE_GLOBAL:      return hlv1_bw_put(bw, 2, 2);       /* 10 */
        case HLV1_MODE_INTER:       return hlv1_bw_put(bw, 6, 3);       /* 110 */
        case HLV1_MODE_SPLIT_INTER: return hlv1_bw_put(bw, 14, 4);      /* 1110 */
        case HLV1_MODE_INTRA_DC:    return hlv1_bw_put(bw, 30, 5);      /* 11110 */
        case HLV1_MODE_FILL:        return hlv1_bw_put(bw, 31, 5);      /* 11111 */
        default:                    return HLV1_ERR_ARGUMENT;
        }
    }
    if (version >= HLV1_STREAM_VERSION_12) {
        switch (mode) {
        case HLV1_MODE_SKIP:        return hlv1_bw_put(bw, 0, 1);      /* 0 */
        case HLV1_MODE_INTER:       return hlv1_bw_put(bw, 2, 2);      /* 10 */
        case HLV1_MODE_SPLIT_INTER: return hlv1_bw_put(bw, 6, 3);      /* 110 */
        case HLV1_MODE_INTRA_DC:    return hlv1_bw_put(bw, 14, 4);     /* 1110 */
        case HLV1_MODE_FILL:        return hlv1_bw_put(bw, 30, 5);     /* 11110 */
        case HLV1_MODE_PALETTE:     return hlv1_bw_put(bw, 31, 5);     /* 11111 */
        default:                    return HLV1_ERR_ARGUMENT;
        }
    }
    if (version < HLV1_STREAM_VERSION_3) {
        switch (mode) {
        case HLV1_MODE_SKIP:     return hlv1_bw_put(bw, 0, 1);
        case HLV1_MODE_INTER:    return hlv1_bw_put(bw, 2, 2);      /* 10 */
        case HLV1_MODE_INTRA_DC: return hlv1_bw_put(bw, 6, 3);      /* 110 */
        case HLV1_MODE_FILL:     return hlv1_bw_put(bw, 7, 3);      /* 111 */
        default:                 return HLV1_ERR_ARGUMENT;
        }
    }
    switch (mode) {
    case HLV1_MODE_SKIP:        return hlv1_bw_put(bw, 0, 1);      /* 0 */
    case HLV1_MODE_INTER:       return hlv1_bw_put(bw, 2, 2);      /* 10 */
    case HLV1_MODE_SPLIT_INTER: return hlv1_bw_put(bw, 6, 3);      /* 110 */
    case HLV1_MODE_INTRA_DC:    return hlv1_bw_put(bw, 14, 4);     /* 1110 */
    case HLV1_MODE_FILL:        return hlv1_bw_put(bw, 15, 4);     /* 1111 */
    default:                    return HLV1_ERR_ARGUMENT;
    }
}

static int align_writer_zero(HLV1BitWriter *bw) {
    unsigned padding = (unsigned)((8U - (bw->bit_count & 7U)) & 7U);
    return padding ? hlv1_bw_put(bw, 0, padding) : HLV1_OK;
}

static int put_motion_vector(HLV1BitWriter *bw, unsigned version,
                             int mvx, int mvy) {
    int r;
    if (version < HLV1_STREAM_VERSION_5) {
        if ((r = hlv1_bw_put_se(bw, mvx / 2)) < 0) return r;
        return hlv1_bw_put_se(bw, mvy / 2);
    }
    if (version < HLV1_STREAM_VERSION_6) {
        if ((r = hlv1_bw_put_se(bw, mvx)) < 0) return r;
        return hlv1_bw_put_se(bw, mvy);
    }
    /* v6 keeps common integer-pixel vectors short.  Half-pixel units are
       only used when at least one component is fractional. */
    int fractional = (mvx | mvy) & 1;
    if ((r = hlv1_bw_put(bw, (uint32_t)fractional, 1)) < 0) return r;
    if (!fractional) {
        mvx /= 2;
        mvy /= 2;
    }
    if ((r = hlv1_bw_put_se(bw, mvx)) < 0) return r;
    return hlv1_bw_put_se(bw, mvy);
}

static int put_residual(HLV1BitWriter *dst, unsigned version,
                        const MB *src, const MB *pred, MB *rec,
                        int qy, int quv, double ac_deadzone, int *had_residual) {
    HLV1BitWriter residual;
    encoder_bw_init_like(&residual, dst);
    if (dst->encoder_work) ++dst->encoder_work->residual_candidates;
    int nonzero = 0;
    int r = version >= HLV1_STREAM_VERSION_9 && qy >= 64
                ? encode_residual_v9(&residual, version, src, pred, rec,
                                     qy, quv, ac_deadzone, &nonzero)
                : version >= HLV1_STREAM_VERSION_8 && qy >= 64
                ? encode_residual_masked(&residual, src, pred, rec,
                                         qy, quv, ac_deadzone, &nonzero)
                : encode_residual(&residual, src, pred, rec,
                                  qy, quv, ac_deadzone, &nonzero);
    if (r >= 0) r = hlv1_bw_finish(&residual);
    if (r >= 0 && version >= HLV1_STREAM_VERSION_2)
        r = hlv1_bw_put(dst, nonzero != 0, 1);
    if (r >= 0 && (version < HLV1_STREAM_VERSION_2 || nonzero))
        r = hlv1_bw_append(dst, &residual);
    if (had_residual) *had_residual = nonzero != 0;
    hlv1_bw_free(&residual);
    return r;
}

static int put_residual_sb8(HLV1BitWriter *dst, unsigned version,
                            const SB8 *src, const SB8 *pred, SB8 *rec,
                            int qy, int quv, double ac_deadzone, int *had_residual) {
    HLV1BitWriter residual;
    encoder_bw_init_like(&residual, dst);
    if (dst->encoder_work) ++dst->encoder_work->residual_candidates;
    int nonzero = 0;
    int r = version >= HLV1_STREAM_VERSION_9 && qy >= 64
                ? encode_residual_sb8_v9(&residual, version, src, pred, rec,
                                         qy, quv, ac_deadzone, &nonzero)
                : version >= HLV1_STREAM_VERSION_8 && qy >= 64
                ? encode_residual_sb8_masked(&residual, src, pred, rec,
                                             qy, quv, ac_deadzone, &nonzero)
                : encode_residual_sb8(&residual, src, pred, rec,
                                      qy, quv, ac_deadzone, &nonzero);
    if (r >= 0) r = hlv1_bw_finish(&residual);
    if (r >= 0) r = hlv1_bw_put(dst, nonzero != 0, 1);
    if (r >= 0 && nonzero) r = hlv1_bw_append(dst, &residual);
    if (had_residual) *had_residual = nonzero != 0;
    hlv1_bw_free(&residual);
    (void)version;
    return r;
}

/* --- Motion search and candidate refinement ----------------------------
 * SAD generates a short list; only motion_candidates entries pay the cost of
 * residual coding and full rate/distortion evaluation. */
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

static void count_sad_samples(HLV1EncoderWork *work, int fx, int fy,
                              uint64_t samples) {
    if (!work) return;
    if (!fx && !fy)
        work->sad_integer_samples += samples;
    else if (!fx || !fy)
        work->sad_hv_samples += samples;
    else
        work->sad_bilinear_samples += samples;
}

static uint64_t motion_sad_luma(const HLV1Frame *src, const HLV1Frame *ref,
                                int x, int y, int size,
                                int mvx, int mvy, int denominator,
                                int use_simd, HLV1EncoderWork *work) {
    int bx = floor_div(x * denominator + mvx, denominator);
    int by = floor_div(y * denominator + mvy, denominator);
    int fx = x * denominator + mvx - bx * denominator;
    int fy = y * denominator + mvy - by * denominator;
    int inv_x = denominator - fx;
    int inv_y = denominator - fy;
    int round = denominator * denominator / 2;
    if (work) ++work->motion_sad_evaluations;
    count_sad_samples(work, fx, fy, (uint64_t)size * (uint64_t)size);
#if HLV1_ENCODER_SSE2
    if (use_simd && !fx && !fy) {
        const uint8_t *a = src->y + y * src->stride_y + x;
        const uint8_t *b = ref->y + by * ref->stride_y + bx;
        return sad_rows_u8_sse2(a, src->stride_y, b, ref->stride_y,
                                size, size);
    }
    if (use_simd && denominator == 2) {
        const uint8_t *a = src->y + y * src->stride_y + x;
        const uint8_t *b = ref->y + by * ref->stride_y + bx;
        return sad_rows_half_pixel_sse2(
            a, src->stride_y, b, ref->stride_y, size, size, fx, fy);
    }
#else
    (void)use_simd;
#endif
    uint64_t sad = 0;
    for (int yy = 0; yy < size; ++yy) {
        const uint8_t *a = src->y + (y + yy) * src->stride_y + x;
        const uint8_t *r0 = ref->y + (by + yy) * ref->stride_y + bx;
        const uint8_t *r1 = r0 + ref->stride_y;
        for (int xx = 0; xx < size; ++xx) {
            int prediction;
            if (!fx && !fy) prediction = r0[xx];
            else if (!fy)
                prediction = (r0[xx] * inv_x + r0[xx + 1] * fx + denominator / 2) /
                             denominator;
            else if (!fx)
                prediction = (r0[xx] * inv_y + r1[xx] * fy + denominator / 2) /
                             denominator;
            else {
                int top = r0[xx] * inv_x + r0[xx + 1] * fx;
                int bottom = r1[xx] * inv_x + r1[xx + 1] * fx;
                prediction = (top * inv_y + bottom * fy + round) /
                             (denominator * denominator);
            }
            sad += (unsigned)abs((int)a[xx] - prediction);
        }
    }
    return sad;
}

static uint64_t find_motion_block(const HLV1Frame *src,
                                  const HLV1Frame *ref,
                                  int x, int y, int size, int radius,
                                  int denominator, int legacy_even,
                                  int use_simd, HLV1EncoderWork *work,
                                  int *best_mvx, int *best_mvy) {
    uint64_t best = UINT64_MAX;
    *best_mvx = *best_mvy = 0;
    int coarse_step = legacy_even ? 2 : denominator;
    int limit = radius * denominator;
    if (legacy_even) limit = radius & ~1;
    for (int mvy = -limit; mvy <= limit; mvy += coarse_step) {
        for (int mvx = -limit; mvx <= limit; mvx += coarse_step) {
            if (!motion_valid(ref, x, y, size, mvx, mvy, denominator)) continue;
            uint64_t sad = motion_sad_luma(src, ref, x, y, size,
                                           mvx, mvy, denominator,
                                           use_simd, work);
            if (sad < best) {
                best = sad;
                *best_mvx = mvx;
                *best_mvy = mvy;
            }
        }
    }
    /* v6 adds a cheap half-pixel refinement around the best full-pixel point. */
    if (denominator == 2) {
        int center_x = *best_mvx, center_y = *best_mvy;
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (!ox && !oy) continue;
                int mvx = center_x + ox, mvy = center_y + oy;
                if (abs(mvx) > limit || abs(mvy) > limit) continue;
                if (!motion_valid(ref, x, y, size, mvx, mvy, denominator)) continue;
                uint64_t sad = motion_sad_luma(src, ref, x, y, size,
                                               mvx, mvy, denominator,
                                               use_simd, work);
                if (sad < best) {
                    best = sad;
                    *best_mvx = mvx;
                    *best_mvy = mvy;
                }
            }
        }
    }
    return best;
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

static uint64_t motion_sad_luma_rect(const HLV1Frame *src,
                                     const HLV1Frame *ref,
                                     int x, int y, int w, int h,
                                     int mvx, int mvy, int denominator,
                                     int use_simd, HLV1EncoderWork *work) {
    int bx = floor_div(x * denominator + mvx, denominator);
    int by = floor_div(y * denominator + mvy, denominator);
    int fx = x * denominator + mvx - bx * denominator;
    int fy = y * denominator + mvy - by * denominator;
    int inv_x = denominator - fx, inv_y = denominator - fy;
    int round = denominator * denominator / 2;
    if (work) ++work->motion_sad_evaluations;
    count_sad_samples(work, fx, fy, (uint64_t)w * (uint64_t)h);
#if HLV1_ENCODER_SSE2
    if (use_simd && !fx && !fy) {
        const uint8_t *a = src->y + y * src->stride_y + x;
        const uint8_t *b = ref->y + by * ref->stride_y + bx;
        return sad_rows_u8_sse2(a, src->stride_y, b, ref->stride_y,
                                w, h);
    }
    if (use_simd && denominator == 2) {
        const uint8_t *a = src->y + y * src->stride_y + x;
        const uint8_t *b = ref->y + by * ref->stride_y + bx;
        return sad_rows_half_pixel_sse2(
            a, src->stride_y, b, ref->stride_y, w, h, fx, fy);
    }
#else
    (void)use_simd;
#endif
    uint64_t sad = 0;
    for (int yy = 0; yy < h; ++yy) {
        const uint8_t *a = src->y + (y + yy) * src->stride_y + x;
        const uint8_t *r0 = ref->y + (by + yy) * ref->stride_y + bx;
        const uint8_t *r1 = r0 + ref->stride_y;
        for (int xx = 0; xx < w; ++xx) {
            int prediction;
            if (!fx && !fy) prediction = r0[xx];
            else if (!fy)
                prediction = (r0[xx] * inv_x + r0[xx + 1] * fx +
                              denominator / 2) / denominator;
            else if (!fx)
                prediction = (r0[xx] * inv_y + r1[xx] * fy +
                              denominator / 2) / denominator;
            else {
                int top = r0[xx] * inv_x + r0[xx + 1] * fx;
                int bottom = r1[xx] * inv_x + r1[xx + 1] * fx;
                prediction = (top * inv_y + bottom * fy + round) /
                             (denominator * denominator);
            }
            sad += (unsigned)abs((int)a[xx] - prediction);
        }
    }
    return sad;
}

static void find_motion_rect(const HLV1Frame *src, const HLV1Frame *ref,
                             int x, int y, int w, int h, int radius,
                             int denominator, int legacy_even,
                             int use_simd, HLV1EncoderWork *work,
                             int *best_mvx, int *best_mvy) {
    uint64_t best = UINT64_MAX;
    *best_mvx = *best_mvy = 0;
    int coarse_step = legacy_even ? 2 : denominator;
    int limit = radius * denominator;
    if (legacy_even) limit = radius & ~1;
    for (int mvy = -limit; mvy <= limit; mvy += coarse_step)
        for (int mvx = -limit; mvx <= limit; mvx += coarse_step) {
            if (!motion_valid_rect(ref, x, y, w, h, mvx, mvy, denominator))
                continue;
            uint64_t sad = motion_sad_luma_rect(src, ref, x, y, w, h,
                                                mvx, mvy, denominator,
                                                use_simd, work);
            if (sad < best) {
                best = sad;
                *best_mvx = mvx;
                *best_mvy = mvy;
            }
        }
    if (denominator == 2) {
        int center_x = *best_mvx, center_y = *best_mvy;
        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) {
                if (!ox && !oy) continue;
                int mvx = center_x + ox, mvy = center_y + oy;
                if (abs(mvx) > limit || abs(mvy) > limit ||
                    !motion_valid_rect(ref, x, y, w, h, mvx, mvy, denominator))
                    continue;
                uint64_t sad = motion_sad_luma_rect(src, ref, x, y, w, h,
                                                    mvx, mvy, denominator,
                                                    use_simd, work);
                if (sad < best) {
                    best = sad;
                    *best_mvx = mvx;
                    *best_mvy = mvy;
                }
            }
    }
}

static void insert_motion_choice(MotionChoice *choices, int *count, int cap,
                                 int mvx, int mvy, uint64_t sad) {
    for (int i = 0; i < *count; ++i) {
        if (choices[i].mvx == mvx && choices[i].mvy == mvy) {
            if (sad < choices[i].sad) choices[i].sad = sad;
            return;
        }
    }
    int pos = 0;
    while (pos < *count && choices[pos].sad <= sad) ++pos;
    if (*count >= cap && pos >= cap) return;
    int new_count = *count < cap ? *count + 1 : *count;
    for (int i = new_count - 1; i > pos; --i)
        choices[i] = choices[i - 1];
    choices[pos].mvx = mvx;
    choices[pos].mvy = mvy;
    choices[pos].sad = sad;
    *count = new_count;
}

static int find_motion_choice(const MotionChoice *choices, int count,
                              int mvx, int mvy, uint64_t *sad) {
    for (int i = 0; i < count; ++i) {
        if (choices[i].mvx == mvx && choices[i].mvy == mvy) {
            if (sad) *sad = choices[i].sad;
            return 1;
        }
    }
    return 0;
}

static int collect_motion_choices(const HLV1Frame *src, const HLV1Frame *ref,
                                  int x, int y, int size, int radius,
                                  int denominator, int legacy_even,
                                  int requested, int global_mvx,
                                  int global_mvy, int use_global,
                                  int use_simd, HLV1EncoderWork *work,
                                  MotionChoice *choices, int capacity) {
    if (requested <= 1) {
        int mvx, mvy;
        uint64_t sad = find_motion_block(
            src, ref, x, y, size, radius, denominator, legacy_even,
            use_simd, work, &mvx, &mvy);
        choices[0] = (MotionChoice){mvx, mvy, sad};
        return 1;
    }
    int count = 0;
    int keep = HLV1_MIN(requested, capacity);
    int limit = radius * denominator;
    int coarse_step = legacy_even ? 2 : denominator;
    if (legacy_even) limit = radius & ~1;
    for (int mvy = -limit; mvy <= limit; mvy += coarse_step)
        for (int mvx = -limit; mvx <= limit; mvx += coarse_step) {
            if (!motion_valid(ref, x, y, size, mvx, mvy, denominator)) continue;
            uint64_t sad = motion_sad_luma(src, ref, x, y, size,
                                           mvx, mvy, denominator,
                                           use_simd, work);
            insert_motion_choice(choices, &count, keep, mvx, mvy, sad);
        }
    if (denominator == 2 && count) {
        int cx = choices[0].mvx, cy = choices[0].mvy;
        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) {
                if (!ox && !oy) continue;
                int mvx = cx + ox, mvy = cy + oy;
                if (abs(mvx) > limit || abs(mvy) > limit ||
                    !motion_valid(ref, x, y, size, mvx, mvy, denominator))
                    continue;
                uint64_t sad = motion_sad_luma(src, ref, x, y, size,
                                               mvx, mvy, denominator,
                                               use_simd, work);
                insert_motion_choice(choices, &count, keep, mvx, mvy, sad);
            }
    }
    /* Cheap predictors can win RDO despite not being among the lowest-SAD
       positions, because their motion-vector syntax is much shorter. */
    if (count < capacity && motion_valid(ref, x, y, size, 0, 0, denominator))
        insert_motion_choice(choices, &count, capacity, 0, 0,
            motion_sad_luma(src, ref, x, y, size, 0, 0, denominator,
                            use_simd, work));
    if (use_global && count < capacity &&
        motion_valid(ref, x, y, size, global_mvx, global_mvy, denominator))
        insert_motion_choice(choices, &count, capacity, global_mvx, global_mvy,
            motion_sad_luma(src, ref, x, y, size,
                            global_mvx, global_mvy, denominator, use_simd,
                            work));
    if (denominator == 2 && count < capacity) {
        int mvx = choices[0].mvx & ~1;
        int mvy = choices[0].mvy & ~1;
        uint64_t sad = 0;
        if (motion_valid(ref, x, y, size, mvx, mvy, denominator) &&
            !find_motion_choice(choices, count, mvx, mvy, &sad))
            insert_motion_choice(choices, &count, capacity, mvx, mvy,
                motion_sad_luma(src, ref, x, y, size,
                                mvx, mvy, denominator, use_simd, work));
    }
    return count;
}

static int sample_fractional(const uint8_t *src, int stride,
                             int x_num, int y_num, int denominator) {
    int x = floor_div(x_num, denominator);
    int y = floor_div(y_num, denominator);
    int fx = x_num - x * denominator;
    int fy = y_num - y * denominator;
    const uint8_t *row = src + y * stride + x;
    if (!fx && !fy) return row[0];
    if (!fy)
        return (row[0] * (denominator - fx) + row[1] * fx + denominator / 2) /
               denominator;
    if (!fx)
        return (row[0] * (denominator - fy) + row[stride] * fy + denominator / 2) /
               denominator;
    int top = row[0] * (denominator - fx) + row[1] * fx;
    int bottom = row[stride] * (denominator - fx) + row[stride + 1] * fx;
    return (top * (denominator - fy) + bottom * fy +
            denominator * denominator / 2) / (denominator * denominator);
}

static uint64_t global_motion_sad(const HLV1Frame *src, const HLV1Frame *ref,
                                  int mvx, int mvy, int denominator,
                                  int radius, HLV1EncoderWork *work) {
    int margin = radius + 2;
    uint64_t sad = 0;
    uint64_t samples = 0;
    if (work) ++work->global_sad_evaluations;
    for (int y = margin; y < src->padded_height - margin; y += 4) {
        const uint8_t *row = src->y + y * src->stride_y;
        for (int x = margin; x < src->padded_width - margin; x += 4) {
            int p = sample_fractional(ref->y, ref->stride_y,
                                      x * denominator + mvx,
                                      y * denominator + mvy, denominator);
            sad += (unsigned)abs((int)row[x] - p);
            ++samples;
        }
    }
    int base_x = floor_div(mvx, denominator);
    int base_y = floor_div(mvy, denominator);
    count_sad_samples(work, mvx - base_x * denominator,
                      mvy - base_y * denominator, samples);
    return sad;
}

static uint64_t find_global_motion(const HLV1Frame *src, const HLV1Frame *ref,
                                   int radius, int denominator,
                                   int *best_mvx, int *best_mvy,
                                   uint64_t *zero_sad,
                                   HLV1EncoderWork *work) {
    int limit = radius * denominator;
    uint64_t best = UINT64_MAX;
    *best_mvx = *best_mvy = 0;
    uint64_t zero = global_motion_sad(
        src, ref, 0, 0, denominator, radius, work);
    if (zero_sad) *zero_sad = zero;
    for (int mvy = -limit; mvy <= limit; mvy += denominator) {
        for (int mvx = -limit; mvx <= limit; mvx += denominator) {
            uint64_t sad = !mvx && !mvy ? zero :
                global_motion_sad(src, ref, mvx, mvy,
                                  denominator, radius, work);
            if (sad < best) {
                best = sad;
                *best_mvx = mvx;
                *best_mvy = mvy;
            }
        }
    }
    if (denominator == 2) {
        int cx = *best_mvx, cy = *best_mvy;
        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) {
                if (!ox && !oy) continue;
                int mvx = cx + ox, mvy = cy + oy;
                if (abs(mvx) > limit || abs(mvy) > limit) continue;
                uint64_t sad = global_motion_sad(src, ref, mvx, mvy,
                                                 denominator, radius, work);
                if (sad < best) {
                    best = sad;
                    *best_mvx = mvx;
                    *best_mvy = mvy;
                }
            }
    }
    return best;
}

/* --- Encoder object lifetime and configuration ------------------------- */
static double mean_luma_difference(const HLV1Frame *a, const HLV1Frame *b) {
    uint64_t sum = 0;
    size_t n = (size_t)a->padded_width * a->padded_height;
    for (int y = 0; y < a->padded_height; ++y)
        for (int x = 0; x < a->padded_width; ++x)
            sum += (unsigned)abs((int)a->y[y * a->stride_y + x] - b->y[y * b->stride_y + x]);
    return (double)sum / (double)n;
}

static int candidate_init(Candidate *c, int mode, HLV1EncoderWork *work) {
    memset(c, 0, sizeof *c);
    c->mode = mode;
    c->score = HUGE_VAL;
    encoder_bw_init(&c->bits, work);
    if (work) ++work->candidate_initializations;
    return HLV1_OK;
}
static void candidate_free(Candidate *c) { hlv1_bw_free(&c->bits); }

HLV1Encoder *hlv1_encoder_create(const HLV1Header *header, double scene_cut) {
    unsigned version = hlv1_stream_version(header);
    if (!header || !header->width || !header->height || !header->gop ||
        version < HLV1_MIN_VERSION || version > HLV1_MAX_VERSION)
        return NULL;
    HLV1Encoder *e = (HLV1Encoder *)calloc(1, sizeof *e);
    if (!e) return NULL;
    e->header = *header;
    e->scene_cut = scene_cut;
    e->chroma_scale = 1.35;
    e->lambda_scale = 1.0;
    e->decode_cycle_weight = 0.0;
    e->ac_deadzone = 0.5;
    e->luma_weight = 4;
    e->motion_candidates = 1;
    e->use_simd = encoder_sse2_available();
    e->adaptive_keyframe_bias = 1.0;
    hlv1_quality_to_qsteps(header->quality, &e->q_y, &e->q_uv);
    if (hlv1_frame_alloc(&e->previous, header->width, header->height) < 0 ||
        hlv1_frame_alloc(&e->current, header->width, header->height) < 0) {
        hlv1_encoder_destroy(e); return NULL;
    }
    if (hlv1_stream_version(header) >= HLV1_STREAM_VERSION_11) {
        e->mv_cols = e->current.padded_width / 16;
        size_t bytes = (size_t)e->mv_cols * sizeof(int16_t);
        e->mv_top_x = (int16_t *)malloc(bytes);
        e->mv_top_y = (int16_t *)malloc(bytes);
        e->mv_cur_x = (int16_t *)malloc(bytes);
        e->mv_cur_y = (int16_t *)malloc(bytes);
        if (!e->mv_top_x || !e->mv_top_y || !e->mv_cur_x || !e->mv_cur_y) {
            hlv1_encoder_destroy(e); return NULL;
        }
    }
    return e;
}

HLV1Encoder *hlv1_encoder_clone(const HLV1Encoder *src) {
    if (!src) return NULL;
    HLV1Encoder *dst = (HLV1Encoder *)calloc(1, sizeof *dst);
    if (!dst) return NULL;

    dst->header = src->header;
    dst->scene_cut = src->scene_cut;
    dst->q_y = src->q_y;
    dst->q_uv = src->q_uv;
    dst->q_shift = src->q_shift;
    dst->chroma_scale = src->chroma_scale;
    dst->lambda_scale = src->lambda_scale;
    dst->decode_cycle_weight = src->decode_cycle_weight;
    dst->ac_deadzone = src->ac_deadzone;
    dst->luma_weight = src->luma_weight;
    dst->motion_candidates = src->motion_candidates;
    dst->use_simd = src->use_simd;
    dst->adaptive_min_key_interval = src->adaptive_min_key_interval;
    dst->adaptive_keyframe_bias = src->adaptive_keyframe_bias;
    dst->frames_since_key = src->frames_since_key;
    dst->frame_index = src->frame_index;
    dst->estimated_decode_cycles = src->estimated_decode_cycles;
    dst->have_previous = src->have_previous;
    dst->stats = src->stats;
    dst->mv_cols = src->mv_cols;

    if (hlv1_frame_alloc(&dst->previous, src->header.width,
                         src->header.height) < 0 ||
        hlv1_frame_alloc(&dst->current, src->header.width,
                         src->header.height) < 0) {
        hlv1_encoder_destroy(dst);
        return NULL;
    }
    size_t y_size = (size_t)src->previous.stride_y *
                    src->previous.padded_height;
    size_t c_size = (size_t)src->previous.stride_u *
                    (src->previous.padded_height / 2);
    memcpy(dst->previous.y, src->previous.y, y_size);
    memcpy(dst->previous.u, src->previous.u, c_size);
    memcpy(dst->previous.v, src->previous.v, c_size);
    memcpy(dst->current.y, src->current.y, y_size);
    memcpy(dst->current.u, src->current.u, c_size);
    memcpy(dst->current.v, src->current.v, c_size);

    if (src->mv_cols > 0) {
        size_t bytes = (size_t)src->mv_cols * sizeof(int16_t);
        dst->mv_top_x = (int16_t *)malloc(bytes);
        dst->mv_top_y = (int16_t *)malloc(bytes);
        dst->mv_cur_x = (int16_t *)malloc(bytes);
        dst->mv_cur_y = (int16_t *)malloc(bytes);
        if (!dst->mv_top_x || !dst->mv_top_y ||
            !dst->mv_cur_x || !dst->mv_cur_y) {
            hlv1_encoder_destroy(dst);
            return NULL;
        }
        memcpy(dst->mv_top_x, src->mv_top_x, bytes);
        memcpy(dst->mv_top_y, src->mv_top_y, bytes);
        memcpy(dst->mv_cur_x, src->mv_cur_x, bytes);
        memcpy(dst->mv_cur_y, src->mv_cur_y, bytes);
    }
    return dst;
}

static int set_effective_quantization(HLV1Encoder *e, int q_y, int q_uv) {
    if (!e || q_y < 1 || q_y > HLV1_MAX_QSTEP ||
        q_uv < 1 || q_uv > HLV1_MAX_QSTEP)
        return HLV1_ERR_ARGUMENT;
    unsigned shift = 0;
    int maximum = HLV1_MAX(q_y, q_uv);
    while (maximum > 255 && shift < 3) {
        maximum = (maximum + 1) >> 1;
        ++shift;
    }
    int rounding = shift ? 1 << (shift - 1) : 0;
    int base_y = HLV1_CLAMP((q_y + rounding) >> shift, 1, 255);
    int base_uv = HLV1_CLAMP((q_uv + rounding) >> shift, 1, 255);
    e->q_shift = shift;
    e->q_y = base_y << shift;
    e->q_uv = base_uv << shift;
    return HLV1_OK;
}

int hlv1_encoder_set_chroma_scale(HLV1Encoder *e, double scale) {
    if (!e || !isfinite(scale) || scale < 0.25 || scale > 4.0)
        return HLV1_ERR_ARGUMENT;
    e->chroma_scale = scale;
    int requested_uv = HLV1_CLAMP((int)llround(e->q_y * scale), 1, HLV1_MAX_QSTEP);
    return set_effective_quantization(e, e->q_y, requested_uv);
}

int hlv1_encoder_set_quantization(HLV1Encoder *e, int q_y, int q_uv) {
    int r = set_effective_quantization(e, q_y, q_uv);
    if (r < 0) return r;
    e->chroma_scale = (double)e->q_uv / (double)e->q_y;
    return HLV1_OK;
}

int hlv1_encoder_set_rd_parameters(HLV1Encoder *e,
                                   double lambda_scale, int luma_weight) {
    if (!e || !isfinite(lambda_scale) || lambda_scale <= 0.0 ||
        lambda_scale > 16.0 || luma_weight < 1 || luma_weight > 16)
        return HLV1_ERR_ARGUMENT;
    e->lambda_scale = lambda_scale;
    e->luma_weight = luma_weight;
    return HLV1_OK;
}

int hlv1_encoder_set_decode_cycle_weight(HLV1Encoder *e,
                                         double bits_per_cycle) {
    if (!e || !isfinite(bits_per_cycle) ||
        bits_per_cycle < 0.0 || bits_per_cycle > 4.0)
        return HLV1_ERR_ARGUMENT;
    e->decode_cycle_weight = bits_per_cycle;
    return HLV1_OK;
}

int hlv1_encoder_set_ac_deadzone(HLV1Encoder *e, double deadzone) {
    if (!e || !isfinite(deadzone) || deadzone < 0.5 || deadzone > 2.0)
        return HLV1_ERR_ARGUMENT;
    e->ac_deadzone = deadzone;
    return HLV1_OK;
}

int hlv1_encoder_set_motion_candidates(HLV1Encoder *e, int candidates) {
    if (!e || candidates < 1 || candidates > 8) return HLV1_ERR_ARGUMENT;
    e->motion_candidates = candidates;
    return HLV1_OK;
}

int hlv1_encoder_set_simd(HLV1Encoder *e, int enabled) {
    if (!e || (enabled != 0 && enabled != 1))
        return HLV1_ERR_ARGUMENT;
    e->use_simd = enabled && encoder_sse2_available();
    return HLV1_OK;
}

int hlv1_encoder_simd_enabled(const HLV1Encoder *e) {
    return e ? e->use_simd : 0;
}

int hlv1_encoder_set_adaptive_gop(HLV1Encoder *e,
                                  unsigned minimum_key_interval,
                                  double keyframe_bias) {
    if (!e || minimum_key_interval >= e->header.gop ||
        keyframe_bias < 1.0 || keyframe_bias > 1.25)
        return HLV1_ERR_ARGUMENT;
    e->adaptive_min_key_interval = minimum_key_interval;
    e->adaptive_keyframe_bias = keyframe_bias;
    return HLV1_OK;
}

void hlv1_encoder_destroy(HLV1Encoder *e) {
    if (!e) return;
    hlv1_frame_free(&e->previous); hlv1_frame_free(&e->current);
    free(e->mv_top_x); free(e->mv_top_y);
    free(e->mv_cur_x); free(e->mv_cur_y);
    free(e);
}


/* --- Macroblock RDO -----------------------------------------------------
 * Each legal predictor is encoded into a private bit writer and reconstructed.
 * The lowest distortion + lambda*(rate + decode work) candidate is appended
 * to the frame. */
static int encode_inter_mb_candidate(HLV1Encoder *e,
                                     const MB *src, int x, int y,
                                     unsigned version, double lambda_bits,
                                     int denominator, int mvx, int mvy,
                                     int global_mvx, int global_mvy,
                                     int predictor_mvx, int predictor_mvy,
                                     int use_global, Candidate *out) {
    MB pred;
    motion_predict(&e->previous, x, y, mvx, mvy, denominator, &pred,
                   &e->stats.encoder_work);
    out->mode = HLV1_MODE_INTER;
    out->mvx = mvx;
    out->mvy = mvy;
    int r = put_mode(&out->bits, version, HLV1_FRAME_P,
                     HLV1_MODE_INTER, use_global);
    int coded_mvx = mvx, coded_mvy = mvy;
    if (version >= HLV1_STREAM_VERSION_11) {
        coded_mvx -= predictor_mvx;
        coded_mvy -= predictor_mvy;
    } else if (use_global) {
        coded_mvx -= global_mvx;
        coded_mvy -= global_mvy;
    }
    if (r >= 0)
        r = put_motion_vector(&out->bits, version, coded_mvx, coded_mvy);
    if (r >= 0)
        r = put_residual(&out->bits, version, src, &pred, &out->rec,
                         e->q_y, e->q_uv, e->ac_deadzone, NULL);
    if (r >= 0) r = hlv1_bw_finish(&out->bits);
    if (r < 0) return r;
    out->score = score_candidate(e, src, out, lambda_bits);
    return HLV1_OK;
}

static int encode_split_inter_candidate(HLV1Encoder *e,
                                        const HLV1Frame *input,
                                        const MB *src_mb, int x, int y,
                                        unsigned version, double lambda_bits,
                                        int global_mvx, int global_mvy,
                                        int use_global,
                                        Candidate *out) {
    int denominator = version >= HLV1_STREAM_VERSION_6 ? 2 : 1;
    int legacy_even = version < HLV1_STREAM_VERSION_5;
    int r = put_mode(&out->bits, version, HLV1_FRAME_P,
                     HLV1_MODE_SPLIT_INTER, use_global);
    if (r >= 0 && version >= 15)
        r = hlv1_bw_put(&out->bits, 0, 1); /* four 8x8 blocks */
    if (r < 0) return r;
    out->partition = 0;
    memset(&out->rec, 0, sizeof out->rec);

    for (int sy = 0; sy < 16; sy += 8) {
        for (int sx = 0; sx < 16; sx += 8) {
            int gx = x + sx, gy = y + sy;
            SB8 src, skip_rec, inter_rec;
            memset(&inter_rec, 0, sizeof inter_rec);
            extract_sb8(input, gx, gy, &src);
            motion_predict_sb8(&e->previous, gx, gy, 0, 0, denominator,
                               &skip_rec, &e->stats.encoder_work);

            HLV1BitWriter skip_bits, inter_bits;
            encoder_bw_init(&skip_bits, &e->stats.encoder_work);
            encoder_bw_init(&inter_bits, &e->stats.encoder_work);
            r = hlv1_bw_put(&skip_bits, 0, 1);
            if (r >= 0) r = hlv1_bw_finish(&skip_bits);
            if (r < 0) { hlv1_bw_free(&skip_bits); hlv1_bw_free(&inter_bits); return r; }
            double skip_score = (double)weighted_sse_sb8(
                                    &src, &skip_rec, e->luma_weight,
                                    e->use_simd,
                                    &e->stats.encoder_work) +
                                lambda_bits * skip_bits.bit_count;

            MotionChoice choices[12];
            int choice_count = collect_motion_choices(
                input, &e->previous, gx, gy, 8, e->header.search_radius,
                denominator, legacy_even, e->motion_candidates,
                global_mvx, global_mvy, use_global, e->use_simd,
                &e->stats.encoder_work,
                choices, 12);
            double inter_score = HUGE_VAL;
            for (int choice = 0; choice < choice_count; ++choice) {
                HLV1BitWriter trial_bits;
                SB8 trial_pred, trial_rec;
                encoder_bw_init(&trial_bits, &e->stats.encoder_work);
                int mvx = choices[choice].mvx;
                int mvy = choices[choice].mvy;
                motion_predict_sb8(&e->previous, gx, gy, mvx, mvy,
                                   denominator, &trial_pred,
                                   &e->stats.encoder_work);
                int tr = hlv1_bw_put(&trial_bits, 1, 1);
                int coded_mvx = mvx, coded_mvy = mvy;
                if (use_global) {
                    coded_mvx -= global_mvx;
                    coded_mvy -= global_mvy;
                }
                if (tr >= 0)
                    tr = put_motion_vector(&trial_bits, version,
                                           coded_mvx, coded_mvy);
                if (tr >= 0)
                    tr = put_residual_sb8(&trial_bits, version, &src,
                                          &trial_pred, &trial_rec,
                                          e->q_y, e->q_uv, e->ac_deadzone, NULL);
                if (tr >= 0) tr = hlv1_bw_finish(&trial_bits);
                if (tr < 0) {
                    hlv1_bw_free(&trial_bits);
                    hlv1_bw_free(&skip_bits);
                    hlv1_bw_free(&inter_bits);
                    return tr;
                }
                double trial_score =
                    (double)weighted_sse_sb8(
                        &src, &trial_rec, e->luma_weight, e->use_simd,
                        &e->stats.encoder_work) +
                    lambda_bits * trial_bits.bit_count;
                if (trial_score < inter_score) {
                    hlv1_bw_free(&inter_bits);
                    inter_bits = trial_bits;
                    memset(&trial_bits, 0, sizeof trial_bits);
                    inter_rec = trial_rec;
                    inter_score = trial_score;
                }
                hlv1_bw_free(&trial_bits);
            }
            if (choice_count <= 0) {
                hlv1_bw_free(&skip_bits);
                hlv1_bw_free(&inter_bits);
                return HLV1_ERR_RANGE;
            }

            if (skip_score <= inter_score) {
                r = hlv1_bw_append(&out->bits, &skip_bits);
                if (r >= 0) store_sb8_to_mb(&out->rec, sx, sy, &skip_rec);
            } else {
                r = hlv1_bw_append(&out->bits, &inter_bits);
                if (r >= 0) store_sb8_to_mb(&out->rec, sx, sy, &inter_rec);
            }
            hlv1_bw_free(&skip_bits);
            hlv1_bw_free(&inter_bits);
            if (r < 0) return r;
        }
    }
    r = hlv1_bw_finish(&out->bits);
    if (r < 0) return r;
    out->score = (double)weighted_sse(src_mb, &out->rec, e->luma_weight,
                                      e->use_simd,
                                      &e->stats.encoder_work) +
                 lambda_bits * out->bits.bit_count;
    return HLV1_OK;
}

static int encode_rect_inter_candidate(HLV1Encoder *e,
                                       const HLV1Frame *input,
                                       const MB *src_mb, int x, int y,
                                       unsigned version, double lambda_bits,
                                       int global_mvx, int global_mvy,
                                       int use_global, int vertical,
                                       Candidate *out) {
    int denominator = version >= HLV1_STREAM_VERSION_6 ? 2 : 1;
    int legacy_even = version < HLV1_STREAM_VERSION_5;
    int best_mvx[2] = {0, 0}, best_mvy[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        int sx = vertical ? i * 8 : 0;
        int sy = vertical ? 0 : i * 8;
        int w = vertical ? 8 : 16;
        int h = vertical ? 16 : 8;
        find_motion_rect(input, &e->previous, x + sx, y + sy, w, h,
                         e->header.search_radius, denominator, legacy_even,
                         e->use_simd, &e->stats.encoder_work,
                         &best_mvx[i], &best_mvy[i]);
    }

    out->score = HUGE_VAL;
    out->mode = HLV1_MODE_SPLIT_INTER;
    out->partition = vertical ? 2 : 1;
    /* Evaluate skip/inter independently for the two halves.  The residual is
       then coded once for the complete macroblock, avoiding four separate
       residual headers as in the 8x8 partition. */
    for (int combination = 0; combination < 4; ++combination) {
        Candidate trial;
        candidate_init(&trial, HLV1_MODE_SPLIT_INTER,
                       &e->stats.encoder_work);
        trial.partition = out->partition;
        int r = put_mode(&trial.bits, version, HLV1_FRAME_P,
                         HLV1_MODE_SPLIT_INTER, use_global);
        if (r >= 0)
            r = hlv1_bw_put(&trial.bits, vertical ? 3U : 2U, 2); /* 11/10 */
        memset(&trial.rec, 0, sizeof trial.rec);
        for (int i = 0; r >= 0 && i < 2; ++i) {
            int sx = vertical ? i * 8 : 0;
            int sy = vertical ? 0 : i * 8;
            int w = vertical ? 8 : 16;
            int h = vertical ? 16 : 8;
            int use_inter = (combination >> i) & 1;
            int mvx = use_inter ? best_mvx[i] : 0;
            int mvy = use_inter ? best_mvy[i] : 0;
            r = hlv1_bw_put(&trial.bits, (uint32_t)use_inter, 1);
            if (r >= 0 && use_inter) {
                int coded_mvx = mvx, coded_mvy = mvy;
                if (use_global) {
                    coded_mvx -= global_mvx;
                    coded_mvy -= global_mvy;
                }
                r = put_motion_vector(&trial.bits, version,
                                      coded_mvx, coded_mvy);
            }
            if (r >= 0)
                motion_predict_rect_to_mb(&e->previous, x, y, sx, sy, w, h,
                                          mvx, mvy, denominator, &trial.rec,
                                          &e->stats.encoder_work);
        }
        if (r >= 0)
            r = put_residual(&trial.bits, version, src_mb, &trial.rec,
                             &trial.rec, e->q_y, e->q_uv,
                             e->ac_deadzone, NULL);
        if (r >= 0) r = hlv1_bw_finish(&trial.bits);
        if (r < 0) {
            candidate_free(&trial);
            return r;
        }
        trial.score = score_candidate(e, src_mb, &trial, lambda_bits);
        if (trial.score < out->score) {
            candidate_free(out);
            *out = trial;
            memset(&trial, 0, sizeof trial);
        }
        candidate_free(&trial);
    }
    return HLV1_OK;
}

/* --- Frame encoding and reference-state commit ------------------------- */
static int encoder_encode_internal(HLV1Encoder *e, const HLV1Frame *input,
                                   HLV1Packet *packet,
                                   const HLV1Frame **reconstructed,
                                   int forced_frame_type) {
    if (!e || !input || !packet || !reconstructed || input->width != e->header.width || input->height != e->header.height)
        return HLV1_ERR_ARGUMENT;
    memset(packet, 0, sizeof *packet);
    int key;
    if (forced_frame_type == HLV1_FRAME_KEY) {
        key = 1;
    } else if (forced_frame_type == HLV1_FRAME_P) {
        if (!e->have_previous) return HLV1_ERR_ARGUMENT;
        key = 0;
    } else {
        key = !e->have_previous || (e->frame_index % e->header.gop == 0);
        if (!key && mean_luma_difference(input, &e->previous) >= e->scene_cut)
            key = 1;
    }
    int frame_type = key ? HLV1_FRAME_KEY : HLV1_FRAME_P;
    unsigned version = hlv1_stream_version(&e->header);
    HLV1BitWriter frame_bits;
    encoder_bw_init(&frame_bits, &e->stats.encoder_work);
    double lambda_bits = e->lambda_scale *
                         HLV1_MAX(0.5, 0.12 * e->q_y * e->q_y);
    int denominator = version >= HLV1_STREAM_VERSION_6 ? 2 : 1;
    int legacy_even = version < HLV1_STREAM_VERSION_5;
    int global_mvx = 0, global_mvy = 0;
    int use_global = 0;
    int r;
    if (!key && version >= HLV1_STREAM_VERSION_7) {
        uint64_t zero_sad = 0;
        uint64_t best_sad = find_global_motion(input, &e->previous,
                                               e->header.search_radius,
                                               denominator,
                                               &global_mvx, &global_mvy,
                                               &zero_sad,
                                               &e->stats.encoder_work);
        use_global = (global_mvx || global_mvy) &&
                     zero_sad && best_sad < zero_sad - zero_sad / 20;
        if ((r = hlv1_bw_put(&frame_bits, (uint32_t)use_global, 1)) < 0 ||
            (use_global &&
             (r = put_motion_vector(&frame_bits, version,
                                    global_mvx, global_mvy)) < 0)) {
            hlv1_bw_free(&frame_bits);
            return r;
        }
    }

    int fallback_mvx = use_global ? global_mvx : 0;
    int fallback_mvy = use_global ? global_mvy : 0;
    if (!key && version >= HLV1_STREAM_VERSION_11) {
        for (int i = 0; i < e->mv_cols; ++i) {
            e->mv_top_x[i] = (int16_t)fallback_mvx;
            e->mv_top_y[i] = (int16_t)fallback_mvy;
        }
    }

    for (int y = 0; y < e->current.padded_height; y += 16) {
        if (!key && version >= HLV1_STREAM_VERSION_11) {
            for (int i = 0; i < e->mv_cols; ++i) {
                e->mv_cur_x[i] = (int16_t)fallback_mvx;
                e->mv_cur_y[i] = (int16_t)fallback_mvy;
            }
        }
        for (int x = 0; x < e->current.padded_width; x += 16) {
            if (version >= HLV1_STREAM_VERSION_13 &&
                (r = align_writer_zero(&frame_bits)) < 0) {
                hlv1_bw_free(&frame_bits);
                return r;
            }
            int predictor_mvx = fallback_mvx, predictor_mvy = fallback_mvy;
            int mv_column = x / 16;
            if (!key && version >= HLV1_STREAM_VERSION_11)
                motion_vector_predictor(e->mv_top_x, e->mv_top_y,
                                        e->mv_cur_x, e->mv_cur_y,
                                        mv_column, e->mv_cols,
                                        fallback_mvx, fallback_mvy,
                                        &predictor_mvx, &predictor_mvy);
            MB src; extract_mb(input, x, y, &src);
            Candidate c[16];
            for (int i = 0; i < 16; ++i)
                candidate_init(&c[i], i, &e->stats.encoder_work);
            int count = 0;

            MB pred;
            int intra_count = version >= HLV1_STREAM_VERSION_10 ? 4 : 1;
            for (int intra_mode = 0; intra_mode < intra_count; ++intra_mode) {
                intra_predict(&e->current, x, y, intra_mode, &pred);
                Candidate *ci = &c[count++];
                ci->mode = HLV1_MODE_INTRA_DC;
                ci->intra_mode = intra_mode;
                if ((r = put_mode(&ci->bits, version, frame_type,
                                  HLV1_MODE_INTRA_DC, use_global)) < 0)
                    goto fail_mb;
                if (version >= HLV1_STREAM_VERSION_10 &&
                    (r = hlv1_bw_put(&ci->bits,
                                     (uint32_t)intra_mode, 2)) < 0)
                    goto fail_mb;
                if ((r = put_residual(&ci->bits, version, &src, &pred,
                                      &ci->rec, e->q_y, e->q_uv,
                                      e->ac_deadzone, NULL)) < 0)
                    goto fail_mb;
                hlv1_bw_finish(&ci->bits);
                ci->score = (double)weighted_sse(
                                &src, &ci->rec, e->luma_weight,
                                e->use_simd,
                                &e->stats.encoder_work) +
                            lambda_bits * ci->bits.bit_count;
            }

            uint8_t means[3]; fill_predict(&src, &pred, means);
            Candidate *cf = &c[count++]; cf->mode = HLV1_MODE_FILL;
            if ((r = put_mode(&cf->bits, version, frame_type,
                              HLV1_MODE_FILL, use_global)) < 0) goto fail_mb;
            for (int i = 0; i < 3; ++i) hlv1_bw_put(&cf->bits, means[i], 8);
            if ((r = put_residual(&cf->bits, version, &src, &pred, &cf->rec, e->q_y, e->q_uv, e->ac_deadzone, NULL)) < 0) goto fail_mb;
            hlv1_bw_finish(&cf->bits);
            cf->score = (double)weighted_sse(
                            &src, &cf->rec, e->luma_weight, e->use_simd,
                            &e->stats.encoder_work) +
                        lambda_bits * cf->bits.bit_count;

            if (version >= HLV1_STREAM_VERSION_12) {
                int maximum_palette =
                    version >= HLV1_STREAM_VERSION_13 ? 8 : 4;
                int minimum_palette = palette_minimum_colors(
                    &src, &e->stats.encoder_work);
                for (int palette_size = 2;
                     palette_size <= maximum_palette;
                     palette_size *= 2) {
                    Candidate *cp = &c[count++];
                    if (palette_size < minimum_palette) {
                        cp->mode = HLV1_MODE_PALETTE;
                        cp->score = HUGE_VAL;
                        ++e->stats.encoder_work.palette_prefilter_rejections;
                        continue;
                    }
                    if ((r = encode_palette_candidate(&src, version, frame_type,
                                                      use_global, palette_size,
                                                      e->q_y, e->q_uv,
                                                      lambda_bits, e->luma_weight,
                                                      e->use_simd,
                                                      &e->stats.encoder_work,
                                                      cp)) < 0)
                        goto fail_mb;
                }
            }
            if (version >= HLV1_STREAM_VERSION_13) {
                Candidate *cg = &c[count++];
                if ((r = encode_gradient_candidate(e, &src, version, frame_type,
                                                   use_global, lambda_bits, cg)) < 0)
                    goto fail_mb;
                Candidate *cl = &c[count++];
                if ((r = encode_literal_candidate(&src, version, frame_type,
                                                  use_global, cl)) < 0)
                    goto fail_mb;
            }

            if (!key) {
                Candidate *cs = &c[count++]; cs->mode = HLV1_MODE_SKIP;
                motion_predict(&e->previous, x, y, 0, 0, denominator,
                               &cs->rec, &e->stats.encoder_work);
                if ((r = put_mode(&cs->bits, version, frame_type,
                                  HLV1_MODE_SKIP, use_global)) < 0) goto fail_mb;
                hlv1_bw_finish(&cs->bits);
                cs->score = (double)weighted_sse(
                                &src, &cs->rec, e->luma_weight,
                                e->use_simd,
                                &e->stats.encoder_work) +
                            lambda_bits * cs->bits.bit_count;

                if (use_global &&
                    motion_valid(&e->previous, x, y, 16,
                                 global_mvx, global_mvy, denominator)) {
                    Candidate *cg = &c[count++];
                    cg->mode = HLV1_MODE_GLOBAL;
                    cg->mvx = global_mvx;
                    cg->mvy = global_mvy;
                    motion_predict(&e->previous, x, y, global_mvx, global_mvy,
                                   denominator, &pred,
                                   &e->stats.encoder_work);
                    if ((r = put_mode(&cg->bits, version, frame_type,
                                      HLV1_MODE_GLOBAL, use_global)) < 0) goto fail_mb;
                    if ((r = put_residual(&cg->bits, version, &src, &pred,
                                          &cg->rec, e->q_y, e->q_uv,
                                          e->ac_deadzone, NULL)) < 0) goto fail_mb;
                    hlv1_bw_finish(&cg->bits);
                    cg->score = (double)weighted_sse(
                                    &src, &cg->rec, e->luma_weight,
                                    e->use_simd,
                                    &e->stats.encoder_work) +
                                lambda_bits * cg->bits.bit_count;
                }

                MotionChoice choices[12];
                int choice_count = collect_motion_choices(
                    input, &e->previous, x, y, 16, e->header.search_radius,
                    denominator, legacy_even, e->motion_candidates,
                    global_mvx, global_mvy, use_global, e->use_simd,
                    &e->stats.encoder_work,
                    choices, 12);
                Candidate *cm = &c[count++];
                for (int choice = 0; choice < choice_count; ++choice) {
                    Candidate trial;
                    candidate_init(&trial, HLV1_MODE_INTER,
                                   &e->stats.encoder_work);
                    r = encode_inter_mb_candidate(
                        e, &src, x, y, version, lambda_bits, denominator,
                        choices[choice].mvx, choices[choice].mvy,
                        global_mvx, global_mvy,
                        predictor_mvx, predictor_mvy, use_global, &trial);
                    if (r < 0) {
                        candidate_free(&trial);
                        goto fail_mb;
                    }
                    if (trial.score < cm->score) {
                        candidate_free(cm);
                        *cm = trial;
                        memset(&trial, 0, sizeof trial);
                    }
                    candidate_free(&trial);
                }
                if (choice_count <= 0) { r = HLV1_ERR_RANGE; goto fail_mb; }

                if (version >= HLV1_STREAM_VERSION_3) {
                    Candidate *ct = &c[count++];
                    ct->mode = HLV1_MODE_SPLIT_INTER;
                    if ((r = encode_split_inter_candidate(e, input, &src, x, y,
                                                          version, lambda_bits,
                                                          global_mvx, global_mvy,
                                                          use_global,
                                                          ct)) < 0)
                        goto fail_mb;
                    if (version >= 15) {
                        Candidate *ch = &c[count++];
                        if ((r = encode_rect_inter_candidate(
                                e, input, &src, x, y, version, lambda_bits,
                                global_mvx, global_mvy, use_global, 0, ch)) < 0)
                            goto fail_mb;
                        Candidate *cv = &c[count++];
                        if ((r = encode_rect_inter_candidate(
                                e, input, &src, x, y, version, lambda_bits,
                                global_mvx, global_mvy, use_global, 1, cv)) < 0)
                            goto fail_mb;
                    }
                }
            }

            Candidate *best = &c[0];
            for (int i = 0; i < count; ++i) {
                if (c[i].bits.bit_count)
                    c[i].score = score_candidate(e, &src, &c[i],
                                                 lambda_bits);
                if (c[i].score < best->score) best = &c[i];
            }
            if ((r = hlv1_bw_append(&frame_bits, &best->bits)) < 0) goto fail_mb;
            store_mb(&e->current, x, y, &best->rec);
            e->estimated_decode_cycles += best->estimated_decode_cycles;
            e->stats.estimated_decode_cycles += best->estimated_decode_cycles;
            if (!key && version >= HLV1_STREAM_VERSION_11) {
                int selected_mvx = fallback_mvx;
                int selected_mvy = fallback_mvy;
                if (best->mode == HLV1_MODE_SKIP) {
                    selected_mvx = selected_mvy = 0;
                } else if (best->mode == HLV1_MODE_GLOBAL) {
                    selected_mvx = global_mvx;
                    selected_mvy = global_mvy;
                } else if (best->mode == HLV1_MODE_INTER) {
                    selected_mvx = best->mvx;
                    selected_mvy = best->mvy;
                }
                e->mv_cur_x[mv_column] = (int16_t)selected_mvx;
                e->mv_cur_y[mv_column] = (int16_t)selected_mvy;
            }
            e->stats.macroblocks++;
            if (best->mode == HLV1_MODE_SKIP) e->stats.skipped++;
            else if (best->mode == HLV1_MODE_INTER) e->stats.inter++;
            else if (best->mode == HLV1_MODE_GLOBAL) e->stats.global++;
            else if (best->mode == HLV1_MODE_SPLIT_INTER) e->stats.split_inter++;
            else if (best->mode == HLV1_MODE_FILL) e->stats.fill++;
            else if (best->mode == HLV1_MODE_PALETTE) {
                e->stats.palette++;
                if (best->palette_size == 2) e->stats.palette_2++;
                else if (best->palette_size == 4) e->stats.palette_4++;
                else if (best->palette_size == 8) e->stats.palette_8++;
            }
            else if (best->mode == HLV1_MODE_GRADIENT) e->stats.gradient++;
            else if (best->mode == HLV1_MODE_LITERAL) e->stats.literal++;
            else if (best->intra_mode == HLV1_INTRA_VERTICAL)
                e->stats.intra_vertical++;
            else if (best->intra_mode == HLV1_INTRA_HORIZONTAL)
                e->stats.intra_horizontal++;
            else if (best->intra_mode == HLV1_INTRA_PLANE)
                e->stats.intra_plane++;
            else e->stats.intra_dc++;
            for (int i = 0; i < count; ++i) candidate_free(&c[i]);
            continue;

fail_mb:
            for (int i = 0; i < count; ++i) candidate_free(&c[i]);
            hlv1_bw_free(&frame_bits); return r;
        }
        if (!key && version >= HLV1_STREAM_VERSION_11) {
            int16_t *swap;
            swap = e->mv_top_x; e->mv_top_x = e->mv_cur_x; e->mv_cur_x = swap;
            swap = e->mv_top_y; e->mv_top_y = e->mv_cur_y; e->mv_cur_y = swap;
        }
    }

    r = hlv1_bw_finish(&frame_bits);
    if (r < 0) { hlv1_bw_free(&frame_bits); return r; }
    packet->frame_type = (uint8_t)frame_type;
    packet->q_y = (uint8_t)(e->q_y >> e->q_shift);
    packet->q_uv = (uint8_t)(e->q_uv >> e->q_shift);
    packet->q_shift = (uint8_t)(version >= HLV1_STREAM_VERSION_4 ? e->q_shift : 0);
    if (version < HLV1_STREAM_VERSION_4 && e->q_shift) {
        hlv1_bw_free(&frame_bits);
        return HLV1_ERR_RANGE;
    }
    packet->bit_length = (uint32_t)frame_bits.bit_count;
    packet->payload_size = (uint32_t)frame_bits.size;
    packet->payload = frame_bits.data;
    frame_bits.data = NULL;
    hlv1_bw_free(&frame_bits);

    HLV1Frame tmp = e->previous; e->previous = e->current; e->current = tmp;
    e->have_previous = 1;
    e->frames_since_key = key ? 0U : e->frames_since_key + 1U;
    e->frame_index++;
    e->stats.frames++;
    e->stats.keyframes += key;
    e->stats.payload_bytes += packet->payload_size;
    *reconstructed = &e->previous;
    return HLV1_OK;
}


static uint64_t frame_weighted_sse(const HLV1Frame *a, const HLV1Frame *b,
                                   int luma_weight, int use_simd,
                                   HLV1EncoderWork *work) {
    if (work) {
        uint64_t luma = (uint64_t)a->width * (uint64_t)a->height;
        uint64_t chroma = (uint64_t)((a->width + 1) / 2) *
                          (uint64_t)((a->height + 1) / 2);
        work->rdo_sse_samples += luma + 2U * chroma;
    }
    uint64_t sse = 0;
    for (int y = 0; y < a->height; ++y)
        sse += squared_error_u8(a->y + y * a->stride_y,
                                b->y + y * b->stride_y,
                                (size_t)a->width, use_simd) *
               (unsigned)luma_weight;
    int cw = (a->width + 1) / 2;
    int ch = (a->height + 1) / 2;
    for (int y = 0; y < ch; ++y) {
        sse += squared_error_u8(a->u + y * a->stride_u,
                                b->u + y * b->stride_u,
                                (size_t)cw, use_simd);
        sse += squared_error_u8(a->v + y * a->stride_v,
                                b->v + y * b->stride_v,
                                (size_t)cw, use_simd);
    }
    return sse;
}

/* Swap whole encoder states after trial K/P encodes without copying the two
 * padded reference frames again. */
static void encoder_swap_state(HLV1Encoder *a, HLV1Encoder *b) {
    HLV1Encoder temp = *a;
    *a = *b;
    *b = temp;
}

static void add_encoder_work_delta(HLV1EncoderWork *dst,
                                   const HLV1EncoderWork *src,
                                   const HLV1EncoderWork *baseline) {
#define ADD_WORK_DELTA(name) dst->name += src->name - baseline->name
    ADD_WORK_DELTA(motion_sad_evaluations);
    ADD_WORK_DELTA(global_sad_evaluations);
    ADD_WORK_DELTA(sad_integer_samples);
    ADD_WORK_DELTA(sad_hv_samples);
    ADD_WORK_DELTA(sad_bilinear_samples);
    ADD_WORK_DELTA(prediction_copied_samples);
    ADD_WORK_DELTA(prediction_hv_samples);
    ADD_WORK_DELTA(prediction_bilinear_samples);
    ADD_WORK_DELTA(rdo_sse_samples);
    ADD_WORK_DELTA(forward_wht_blocks);
    ADD_WORK_DELTA(inverse_wht_blocks);
    ADD_WORK_DELTA(zero_residual_fast_blocks);
    ADD_WORK_DELTA(dc_only_fast_blocks);
    ADD_WORK_DELTA(quantized_coefficients);
    ADD_WORK_DELTA(palette_distance_evaluations);
    ADD_WORK_DELTA(palette_prefilter_samples);
    ADD_WORK_DELTA(palette_prefilter_bins);
    ADD_WORK_DELTA(palette_prefilter_rejections);
    ADD_WORK_DELTA(candidate_initializations);
    ADD_WORK_DELTA(residual_candidates);
    ADD_WORK_DELTA(bitwriter_put_calls);
    ADD_WORK_DELTA(bitwriter_requested_bits);
    ADD_WORK_DELTA(bitwriter_append_calls);
    ADD_WORK_DELTA(bitwriter_appended_bits);
    ADD_WORK_DELTA(bitwriter_byte_copyable_bytes);
    ADD_WORK_DELTA(bitwriter_bulk_copy_bytes);
    ADD_WORK_DELTA(bitwriter_bulk_shift_bytes);
    ADD_WORK_DELTA(bitwriter_buffer_grows);
#undef ADD_WORK_DELTA
}

int hlv1_encoder_encode(HLV1Encoder *e, const HLV1Frame *input,
                        HLV1Packet *packet, const HLV1Frame **reconstructed) {
    if (!e || !input || !packet || !reconstructed)
        return HLV1_ERR_ARGUMENT;
    if (!e->adaptive_min_key_interval || !e->have_previous)
        return encoder_encode_internal(e, input, packet, reconstructed, -1);

    /* Respect the maximum interval exactly.  Before the minimum interval,
       retain P frames unless a very strong scene cut is detected. */
    if (e->frames_since_key + 1U >= e->header.gop)
        return encoder_encode_internal(e, input, packet, reconstructed,
                                       HLV1_FRAME_KEY);
    double temporal_mad = mean_luma_difference(input, &e->previous);
    if (e->frames_since_key < e->adaptive_min_key_interval &&
        temporal_mad < e->scene_cut * 1.5)
        return encoder_encode_internal(e, input, packet, reconstructed,
                                       HLV1_FRAME_P);

    HLV1Encoder *p_encoder = hlv1_encoder_clone(e);
    HLV1Encoder *k_encoder = hlv1_encoder_clone(e);
    if (!p_encoder || !k_encoder) {
        hlv1_encoder_destroy(p_encoder);
        hlv1_encoder_destroy(k_encoder);
        return HLV1_ERR_MEMORY;
    }
    HLV1Packet p_packet = {0}, k_packet = {0};
    const HLV1Frame *p_rec = NULL, *k_rec = NULL;
    int r = encoder_encode_internal(p_encoder, input, &p_packet, &p_rec,
                                    HLV1_FRAME_P);
    if (r >= 0)
        r = encoder_encode_internal(k_encoder, input, &k_packet, &k_rec,
                                    HLV1_FRAME_KEY);
    if (r < 0) {
        hlv1_packet_free(&p_packet);
        hlv1_packet_free(&k_packet);
        hlv1_encoder_destroy(p_encoder);
        hlv1_encoder_destroy(k_encoder);
        return r;
    }

    double lambda_bits = e->lambda_scale *
                         HLV1_MAX(0.5, 0.12 * e->q_y * e->q_y);
    uint64_t p_decode_cycles =
        p_encoder->estimated_decode_cycles - e->estimated_decode_cycles;
    uint64_t k_decode_cycles =
        k_encoder->estimated_decode_cycles - e->estimated_decode_cycles;
    double p_score = (double)frame_weighted_sse(input, p_rec,
                                                e->luma_weight,
                                                e->use_simd,
                                                &p_encoder->stats.encoder_work) +
                     lambda_bits *
                         ((double)p_packet.bit_length +
                          e->decode_cycle_weight * (double)p_decode_cycles);
    double k_score = (double)frame_weighted_sse(input, k_rec,
                                                e->luma_weight,
                                                e->use_simd,
                                                &k_encoder->stats.encoder_work) +
                     lambda_bits *
                         ((double)k_packet.bit_length +
                          e->decode_cycle_weight * (double)k_decode_cycles);
    int choose_key = k_score <= p_score * e->adaptive_keyframe_bias;

    HLV1Encoder *chosen = choose_key ? k_encoder : p_encoder;
    HLV1Encoder *rejected = choose_key ? p_encoder : k_encoder;
    HLV1Packet *chosen_packet = choose_key ? &k_packet : &p_packet;
    HLV1Packet *rejected_packet = choose_key ? &p_packet : &k_packet;
    add_encoder_work_delta(&chosen->stats.encoder_work,
                           &rejected->stats.encoder_work,
                           &e->stats.encoder_work);

    *packet = *chosen_packet;
    memset(chosen_packet, 0, sizeof *chosen_packet);
    hlv1_packet_free(rejected_packet);
    encoder_swap_state(e, chosen);
    hlv1_encoder_destroy(chosen);   /* destroys the old base state */
    hlv1_encoder_destroy(rejected);
    *reconstructed = &e->previous;
    return HLV1_OK;
}

const HLV1Stats *hlv1_encoder_stats(const HLV1Encoder *e) { return e ? &e->stats : NULL; }
