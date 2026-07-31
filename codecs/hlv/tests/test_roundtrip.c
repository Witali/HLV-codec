/*
 * Bit-exact encoder/decoder regression tests for stable syntax v14/v15.
 * Synthetic frames isolate mode syntax, motion, palette coding, extended
 * quantization, encoder cloning, and adaptive keyframe decisions.
 */
#include "hlv1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void make_motion_frame(HLV1Frame *f, int index) {
    for (int y = 0; y < f->padded_height; ++y) {
        for (int x = 0; x < f->padded_width; ++x) {
            int xx = (x + index * 2) % f->padded_width;
            f->y[y * f->stride_y + x] = (uint8_t)((xx * 3 + y * 2 + ((x/16+y/16)&1)*30) & 255);
        }
    }
    for (int y = 0; y < f->padded_height/2; ++y)
        for (int x = 0; x < f->padded_width/2; ++x) {
            f->u[y*f->stride_u+x] = (uint8_t)(100 + ((x + index) & 31));
            f->v[y*f->stride_v+x] = (uint8_t)(140 + ((y - index) & 31));
        }
}

static void make_constant_frame(HLV1Frame *f, uint8_t yv, uint8_t uv, uint8_t vv) {
    memset(f->y, yv, (size_t)f->stride_y * f->padded_height);
    memset(f->u, uv, (size_t)f->stride_u * (f->padded_height / 2));
    memset(f->v, vv, (size_t)f->stride_v * (f->padded_height / 2));
}

static int same_frame(const HLV1Frame *a, const HLV1Frame *b) {
    size_t ys = (size_t)a->stride_y * a->padded_height;
    size_t cs = (size_t)a->stride_u * (a->padded_height/2);
    return !memcmp(a->y,b->y,ys) && !memcmp(a->u,b->u,cs) && !memcmp(a->v,b->v,cs);
}

static uint64_t frame_sse(const HLV1Frame *a, const HLV1Frame *b) {
    uint64_t sse = 0;
    for (int y = 0; y < a->height; ++y)
        for (int x = 0; x < a->width; ++x) {
            int d = (int)a->y[y * a->stride_y + x] - b->y[y * b->stride_y + x];
            sse += (uint64_t)(d * d);
        }
    int cw = (a->width + 1) / 2, ch = (a->height + 1) / 2;
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) {
            int du = (int)a->u[y * a->stride_u + x] - b->u[y * b->stride_u + x];
            int dv = (int)a->v[y * a->stride_v + x] - b->v[y * b->stride_v + x];
            sse += (uint64_t)(du * du + dv * dv);
        }
    return sse;
}

static int roundtrip_one(HLV1Encoder *e, HLV1Decoder *d, HLV1Frame *input,
                         int version, int index) {
    HLV1Packet p;
    const HLV1Frame *er, *dr;
    int r = hlv1_encoder_encode(e, input, &p, &er);
    if (r < 0) {
        fprintf(stderr, "enc v%d %s frame %d\n", version, hlv1_strerror(r), index);
        return 1;
    }
    r = hlv1_decoder_decode(d, &p, &dr);
    hlv1_packet_free(&p);
    if (r < 0) {
        fprintf(stderr, "dec v%d %s frame %d\n", version, hlv1_strerror(r), index);
        return 1;
    }
    if (!same_frame(er, dr)) {
        fprintf(stderr, "mismatch v%d frame %d\n", version, index);
        return 1;
    }
    return 0;
}

static int test_version(int version) {
    HLV1Header h = {64,48,15,1,0,4,55,4,0,(uint8_t)version};
    HLV1Encoder *e = hlv1_encoder_create(&h, 1000.0);
    HLV1Decoder *d = hlv1_decoder_create(&h);
    HLV1Frame input;
    if (!e || !d || hlv1_frame_alloc(&input,h.width,h.height)<0) return 2;
    if (version >= HLV1_STREAM_VERSION_9 &&
        hlv1_encoder_set_quantization(e, 128, 172) < 0)
        return 2;

    /* Constant key frame should exercise FILL and its residual syntax. */
    make_constant_frame(&input, 73, 112, 151);
    if (roundtrip_one(e, d, &input, version, 0)) return 1;
    const HLV1Stats *s = hlv1_encoder_stats(e);
    if (!s || !s->fill) {
        fprintf(stderr, "FILL not exercised in v%d\n", version);
        return 1;
    }

    /* v14 emits SKIP macroblocks; v15 collapses the same reconstruction to
       one zero-video-payload REPEAT packet. */
    if (roundtrip_one(e, d, &input, version, 1)) return 1;
    s = hlv1_encoder_stats(e);
    if (!s || (version >= HLV1_STREAM_VERSION_15
                   ? !s->repeated_frames
                   : !s->skipped)) {
        fprintf(stderr, "SKIP/REPEAT not exercised in v%d\n", version);
        return 1;
    }

    /* Moving textured frames exercise INTER/INTRA and non-zero residuals. */
    for (int i = 2; i < 14; ++i) {
        make_motion_frame(&input, i - 2);
        if (roundtrip_one(e, d, &input, version, i)) return 1;
    }
    s = hlv1_encoder_stats(e);
    if (version >= HLV1_STREAM_VERSION_14 &&
        (!s->encoder_work.motion_sad_evaluations ||
         !(s->encoder_work.sad_integer_samples +
           s->encoder_work.sad_hv_samples +
           s->encoder_work.sad_bilinear_samples) ||
         !(s->encoder_work.prediction_copied_samples +
           s->encoder_work.prediction_hv_samples +
           s->encoder_work.prediction_bilinear_samples) ||
         !s->encoder_work.rdo_sse_samples ||
         !s->encoder_work.forward_wht_blocks ||
         !s->encoder_work.inverse_wht_blocks ||
         !(s->encoder_work.zero_residual_fast_blocks +
           s->encoder_work.dc_only_fast_blocks) ||
         !s->encoder_work.quantized_coefficients ||
         !s->encoder_work.palette_distance_evaluations ||
         !s->encoder_work.candidate_initializations ||
         !s->encoder_work.residual_candidates ||
         !s->encoder_work.bitwriter_put_calls ||
         !s->encoder_work.bitwriter_append_calls ||
         !s->encoder_work.bitwriter_buffer_grows)) {
        fprintf(stderr, "encoder work counters not exercised in v%d\n", version);
        return 1;
    }

    hlv1_frame_free(&input);
    hlv1_encoder_destroy(e);
    hlv1_decoder_destroy(d);
    return 0;
}


static void make_texture_frame(HLV1Frame *f) {
    for (int y = 0; y < f->padded_height; ++y)
        for (int x = 0; x < f->padded_width; ++x)
            f->y[y * f->stride_y + x] = (uint8_t)((x * 17 + y * 29 + (x ^ y) * 3) & 255);
    for (int y = 0; y < f->padded_height / 2; ++y)
        for (int x = 0; x < f->padded_width / 2; ++x) {
            f->u[y * f->stride_u + x] = (uint8_t)((70 + x * 11 + y * 3) & 255);
            f->v[y * f->stride_v + x] = (uint8_t)((150 + x * 5 + y * 13) & 255);
        }
}

static void make_partitioned_motion(const HLV1Frame *src, HLV1Frame *dst) {
    for (int y = 0; y < dst->padded_height; ++y) {
        int dy = ((y / 8) & 1) ? -2 : 2;
        for (int x = 0; x < dst->padded_width; ++x) {
            int dx = ((x / 8) & 1) ? -2 : 2;
            dst->y[y * dst->stride_y + x] = src->y[(y + dy) * src->stride_y + x + dx];
        }
    }
    for (int y = 0; y < dst->padded_height / 2; ++y) {
        int dy = ((y / 4) & 1) ? -1 : 1;
        for (int x = 0; x < dst->padded_width / 2; ++x) {
            int dx = ((x / 4) & 1) ? -1 : 1;
            dst->u[y * dst->stride_u + x] = src->u[(y + dy) * src->stride_u + x + dx];
            dst->v[y * dst->stride_v + x] = src->v[(y + dy) * src->stride_v + x + dx];
        }
    }
}

static int test_joint_split_v15(void) {
    HLV1Header h = {32,32,15,1,0,100,100,4,0,HLV1_STREAM_VERSION_15};
    HLV1Encoder *e = hlv1_encoder_create(&h, 1000.0);
    HLV1Decoder *d = hlv1_decoder_create(&h);
    HLV1Frame base, moved;
    if (!e || !d || hlv1_frame_alloc(&base,32,32)<0 ||
        hlv1_frame_alloc(&moved,32,32)<0) return 2;
    make_texture_frame(&base);
    if (roundtrip_one(e, d, &base, 15, 0)) return 1;
    make_partitioned_motion(&base, &moved);
    if (roundtrip_one(e, d, &moved, 15, 1)) return 1;
    const HLV1Stats *st = hlv1_encoder_stats(e);
    if (!st || !st->split_joint) {
        fprintf(stderr, "SPLIT_JOINT not exercised in v15\n");
        return 1;
    }
    hlv1_frame_free(&base);
    hlv1_frame_free(&moved);
    hlv1_encoder_destroy(e);
    hlv1_decoder_destroy(d);
    return 0;
}



static void make_partitioned_motion_odd(const HLV1Frame *src, HLV1Frame *dst) {
    for (int y = 0; y < dst->padded_height; ++y) {
        int dy = ((y / 8) & 1) ? -1 : 1;
        for (int x = 0; x < dst->padded_width; ++x) {
            int dx = ((x / 8) & 1) ? -1 : 1;
            dst->y[y * dst->stride_y + x] = src->y[(y + dy) * src->stride_y + x + dx];
        }
    }
    /* Constant chroma isolates the luma odd-pixel vector syntax. */
    memset(dst->u, 111, (size_t)dst->stride_u * (dst->padded_height / 2));
    memset(dst->v, 149, (size_t)dst->stride_v * (dst->padded_height / 2));
}

static int test_odd_motion_v5(void) {
    HLV1Header h = {32,32,25,1,0,100,100,4,0,HLV1_STREAM_VERSION_5};
    HLV1Encoder *e = hlv1_encoder_create(&h, 1000.0);
    HLV1Decoder *d = hlv1_decoder_create(&h);
    HLV1Frame base, moved;
    if (!e || !d || hlv1_frame_alloc(&base,32,32)<0 ||
        hlv1_frame_alloc(&moved,32,32)<0) return 2;
    make_texture_frame(&base);
    memset(base.u, 111, (size_t)base.stride_u * (base.padded_height / 2));
    memset(base.v, 149, (size_t)base.stride_v * (base.padded_height / 2));
    if (roundtrip_one(e, d, &base, 5, 0)) return 1;
    make_partitioned_motion_odd(&base, &moved);
    if (roundtrip_one(e, d, &moved, 5, 1)) return 1;
    const HLV1Stats *st = hlv1_encoder_stats(e);
    if (!st || (!st->split_inter && !st->inter)) {
        fprintf(stderr, "odd motion not exercised in v5\n");
        return 1;
    }
    hlv1_frame_free(&base); hlv1_frame_free(&moved);
    hlv1_encoder_destroy(e); hlv1_decoder_destroy(d);
    return 0;
}

static void make_half_pixel_motion(const HLV1Frame *src, HLV1Frame *dst) {
    for (int y = 0; y < dst->padded_height; ++y)
        for (int x = 0; x < dst->padded_width; ++x) {
            int nx = x + 1 < src->padded_width ? x + 1 : x;
            dst->y[y * dst->stride_y + x] =
                (uint8_t)((src->y[y * src->stride_y + x] +
                           src->y[y * src->stride_y + nx] + 1) >> 1);
        }
    for (int y = 0; y < dst->padded_height / 2; ++y)
        for (int x = 0; x < dst->padded_width / 2; ++x) {
            int nx = x + 1 < src->padded_width / 2 ? x + 1 : x;
            dst->u[y * dst->stride_u + x] =
                (uint8_t)((3 * src->u[y * src->stride_u + x] +
                           src->u[y * src->stride_u + nx] + 2) >> 2);
            dst->v[y * dst->stride_v + x] =
                (uint8_t)((3 * src->v[y * src->stride_v + x] +
                           src->v[y * src->stride_v + nx] + 2) >> 2);
        }
}

static uint64_t encode_half_motion_error(int version) {
    HLV1Header h = {64,32,25,1,0,100,100,4,0,(uint8_t)version};
    HLV1Encoder *e = hlv1_encoder_create(&h, 1000.0);
    HLV1Frame base, moved;
    if (!e || hlv1_encoder_set_quantization(e, 64, 80) < 0 ||
        hlv1_frame_alloc(&base,64,32)<0 ||
        hlv1_frame_alloc(&moved,64,32)<0) return UINT64_MAX;
    make_texture_frame(&base);
    make_half_pixel_motion(&base, &moved);
    HLV1Packet p; const HLV1Frame *rec;
    if (hlv1_encoder_encode(e, &base, &p, &rec) < 0) return UINT64_MAX;
    hlv1_packet_free(&p);
    if (hlv1_encoder_encode(e, &moved, &p, &rec) < 0) return UINT64_MAX;
    uint64_t error = frame_sse(&moved, rec);
    hlv1_packet_free(&p);
    hlv1_frame_free(&base); hlv1_frame_free(&moved);
    hlv1_encoder_destroy(e);
    return error;
}

static int test_half_motion_v6(void) {
    uint64_t v5 = encode_half_motion_error(HLV1_STREAM_VERSION_5);
    uint64_t v6 = encode_half_motion_error(HLV1_STREAM_VERSION_6);
    if (v5 == UINT64_MAX || v6 == UINT64_MAX || v6 >= v5) {
        fprintf(stderr, "half-pixel motion did not improve: v5=%llu v6=%llu\n",
                (unsigned long long)v5, (unsigned long long)v6);
        return 1;
    }
    return 0;
}

static int test_extended_quant_v4(void) {
    HLV1Header h = {32,32,25,1,0,10,55,4,0,HLV1_STREAM_VERSION_4};
    HLV1Encoder *e = hlv1_encoder_create(&h, 1000.0);
    HLV1Decoder *d = hlv1_decoder_create(&h);
    HLV1Frame input;
    if (!e || !d || hlv1_frame_alloc(&input,32,32)<0) return 2;
    if (hlv1_encoder_set_quantization(e, 2040, 1536) < 0) return 1;
    make_texture_frame(&input);
    HLV1Packet p; const HLV1Frame *er, *dr;
    int r = hlv1_encoder_encode(e, &input, &p, &er);
    if (r < 0 || p.q_shift == 0 || ((int)p.q_y << p.q_shift) > HLV1_MAX_QSTEP ||
        ((int)p.q_uv << p.q_shift) > HLV1_MAX_QSTEP) return 1;
    r = hlv1_decoder_decode(d, &p, &dr);
    hlv1_packet_free(&p);
    if (r < 0 || !same_frame(er, dr)) return 1;
    hlv1_frame_free(&input);
    hlv1_encoder_destroy(e);
    hlv1_decoder_destroy(d);
    return 0;
}

static int test_quality_qstep_range(void) {
    HLV1Header h = {16,16,15,1,0,1,55,0,0,HLV1_VERSION};
    HLV1Encoder *e = hlv1_encoder_create(&h, 1000.0);
    if (!e) return 1;
    if (hlv1_encoder_set_quantization(e, 1, 255) < 0 ||
        hlv1_encoder_set_quantization(e, 2040, 1536) < 0 ||
        hlv1_encoder_set_quantization(e, 0, 1) >= 0 ||
        hlv1_encoder_set_quantization(e, 1, HLV1_MAX_QSTEP + 1) >= 0) {
        hlv1_encoder_destroy(e);
        return 1;
    }
    hlv1_encoder_destroy(e);
    int prev_y = 256, prev_uv = 256;
    for (int quality = 1; quality <= 100; ++quality) {
        int qy = 0, quv = 0;
        if (hlv1_quality_to_qsteps(quality, &qy, &quv) < 0) return 1;
        if (qy < 1 || qy > 255 || quv < 1 || quv > 255) return 1;
        if (qy > prev_y || quv > prev_uv) return 1;
        prev_y = qy;
        prev_uv = quv;
    }
    return 0;
}

static void make_palette_frame(HLV1Frame *frame, int phase) {
    static const uint8_t colors[2][3] = {{35, 90, 170}, {220, 175, 70}};
    for (int y = 0; y < frame->padded_height; ++y)
        for (int x = 0; x < frame->padded_width; ++x) {
            int index = (((x + phase) / 4) ^ ((y + phase) / 4)) & 1;
            frame->y[y * frame->stride_y + x] = colors[index][0];
        }
    for (int y = 0; y < frame->padded_height / 2; ++y)
        for (int x = 0; x < frame->padded_width / 2; ++x) {
            int index = ((((x * 2 + phase) / 4) ^ ((y * 2 + phase) / 4)) & 1);
            frame->u[y * frame->stride_u + x] = colors[index][1];
            frame->v[y * frame->stride_v + x] = colors[index][2];
        }
}

static int test_palette_v12(void) {
    HLV1Header h = {32,32,25,1,0,100,55,4,0,HLV1_STREAM_VERSION_12};
    HLV1Encoder *encoder = hlv1_encoder_create(&h, 1000.0);
    HLV1Decoder *decoder = hlv1_decoder_create(&h);
    HLV1Frame input;
    if (!encoder || !decoder || hlv1_frame_alloc(&input,32,32) < 0) return 2;
    if (hlv1_encoder_set_quantization(encoder, 1, 1) < 0) return 2;
    make_palette_frame(&input, 0);
    if (roundtrip_one(encoder, decoder, &input, 12, 0)) return 1;
    const HLV1Stats *stats = hlv1_encoder_stats(encoder);
    if (!stats || !stats->palette) {
        fprintf(stderr, "PALETTE not exercised in v12\n");
        return 1;
    }
    hlv1_frame_free(&input);
    hlv1_encoder_destroy(encoder);
    hlv1_decoder_destroy(decoder);
    return 0;
}

static void make_palette8_frame(HLV1Frame *frame) {
    static const uint8_t colors[8][3] = {
        {20, 40, 220}, {50, 210, 45}, {80, 75, 180}, {110, 180, 90},
        {145, 95, 150}, {175, 155, 115}, {210, 120, 70}, {240, 200, 25}
    };
    for (int y = 0; y < frame->padded_height; ++y)
        for (int x = 0; x < frame->padded_width; ++x) {
            int index = ((x / 2) + (y / 2) * 3) & 7;
            frame->y[y * frame->stride_y + x] = colors[index][0];
        }
    for (int y = 0; y < frame->padded_height / 2; ++y)
        for (int x = 0; x < frame->padded_width / 2; ++x) {
            int index = (x + y * 3) & 7;
            frame->u[y * frame->stride_u + x] = colors[index][1];
            frame->v[y * frame->stride_v + x] = colors[index][2];
        }
}

static int test_palette8_v14(void) {
    HLV1Header h = {32,32,25,1,0,100,55,4,0,HLV1_VERSION};
    HLV1Encoder *encoder = hlv1_encoder_create(&h, 1000.0);
    HLV1Decoder *decoder = hlv1_decoder_create(&h);
    HLV1Frame input;
    if (!encoder || !decoder || hlv1_frame_alloc(&input,32,32) < 0) return 2;
    if (hlv1_encoder_set_quantization(encoder, 1, 1) < 0) return 2;
    make_palette8_frame(&input);
    if (roundtrip_one(encoder, decoder, &input, HLV1_VERSION, 0)) return 1;
    const HLV1Stats *stats = hlv1_encoder_stats(encoder);
    if (!stats || !stats->palette_8) {
        fprintf(stderr, "8-color PALETTE not exercised in v14\n");
        return 1;
    }
    hlv1_frame_free(&input);
    hlv1_encoder_destroy(encoder);
    hlv1_decoder_destroy(decoder);
    return 0;
}

static int test_literal_v14(void) {
    HLV1Header h = {32,32,25,1,0,100,75,4,0,HLV1_VERSION};
    HLV1Encoder *encoder = hlv1_encoder_create(&h, 1000.0);
    HLV1Decoder *decoder = hlv1_decoder_create(&h);
    HLV1Frame input;
    if (!encoder || !decoder || hlv1_frame_alloc(&input,32,32) < 0) return 2;
    if (hlv1_encoder_set_quantization(encoder, 8, 12) < 0 ||
        hlv1_encoder_set_decode_cycle_weight(encoder, 4.0) < 0)
        return 2;
    make_texture_frame(&input);
    if (roundtrip_one(encoder, decoder, &input, HLV1_VERSION, 0)) return 1;
    const HLV1Stats *stats = hlv1_encoder_stats(encoder);
    if (!stats || !stats->literal || !stats->estimated_decode_cycles) {
        fprintf(stderr, "LITERAL not exercised in v14\n");
        return 1;
    }
    hlv1_frame_free(&input);
    hlv1_encoder_destroy(encoder);
    hlv1_decoder_destroy(decoder);
    return 0;
}


static int test_adaptive_gop_v14(void) {
    HLV1Header h = {64,48,25,1,0,100,55,4,0,HLV1_STREAM_VERSION_14};
    HLV1Encoder *encoder = hlv1_encoder_create(&h, 1000.0);
    HLV1Decoder *decoder = hlv1_decoder_create(&h);
    HLV1Frame input;
    if (!encoder || !decoder || hlv1_frame_alloc(&input,64,48) < 0)
        return 2;
    if (hlv1_encoder_set_quantization(encoder, 128, 173) < 0 ||
        hlv1_encoder_set_adaptive_gop(encoder, 4, 1.0) < 0)
        return 2;

    HLV1Packet packet;
    const HLV1Frame *er = NULL, *dr = NULL;
    make_texture_frame(&input);
    int r = hlv1_encoder_encode(encoder, &input, &packet, &er);
    if (r < 0 || packet.frame_type != HLV1_FRAME_KEY)
        return 1;
    r = hlv1_decoder_decode(decoder, &packet, &dr);
    hlv1_packet_free(&packet);
    if (r < 0 || !same_frame(er, dr)) return 1;

    /* Continuous content must remain predictive. */
    for (int i = 0; i < 4; ++i) {
        r = hlv1_encoder_encode(encoder, &input, &packet, &er);
        if (r < 0 || packet.frame_type != HLV1_FRAME_P)
            return 1;
        r = hlv1_decoder_decode(decoder, &packet, &dr);
        hlv1_packet_free(&packet);
        if (r < 0 || !same_frame(er, dr)) return 1;
    }

    /* A completely different scene should become a new clean reference. */
    make_constant_frame(&input, 215, 70, 188);
    r = hlv1_encoder_encode(encoder, &input, &packet, &er);
    if (r < 0 || packet.frame_type != HLV1_FRAME_KEY) {
        fprintf(stderr, "adaptive GOP did not select scene-cut K frame\n");
        return 1;
    }
    r = hlv1_decoder_decode(decoder, &packet, &dr);
    hlv1_packet_free(&packet);
    if (r < 0 || !same_frame(er, dr)) return 1;

    hlv1_frame_free(&input);
    hlv1_encoder_destroy(encoder);
    hlv1_decoder_destroy(decoder);
    return 0;
}

static int test_encoder_clone(void) {
    HLV1Header h = {64,32,25,1,0,30,55,4,0,HLV1_VERSION};
    HLV1Encoder *original = hlv1_encoder_create(&h, 1000.0);
    HLV1Frame first, second;
    if (!original || hlv1_frame_alloc(&first,64,32) < 0 ||
        hlv1_frame_alloc(&second,64,32) < 0) return 2;
    make_texture_frame(&first);
    make_half_pixel_motion(&first, &second);
    HLV1Packet p0; const HLV1Frame *rec0;
    if (hlv1_encoder_encode(original, &first, &p0, &rec0) < 0) return 1;
    hlv1_packet_free(&p0);

    HLV1Encoder *clone = hlv1_encoder_clone(original);
    if (!clone) return 1;
    HLV1Packet a, b; const HLV1Frame *ra, *rb;
    if (hlv1_encoder_encode(original, &second, &a, &ra) < 0 ||
        hlv1_encoder_encode(clone, &second, &b, &rb) < 0) return 1;
    int failed = a.frame_type != b.frame_type || a.q_y != b.q_y ||
                 a.q_uv != b.q_uv || a.q_shift != b.q_shift ||
                 a.bit_length != b.bit_length ||
                 a.payload_size != b.payload_size ||
                 (a.payload_size && memcmp(a.payload, b.payload,
                                           a.payload_size)) ||
                 !same_frame(ra, rb);
    hlv1_packet_free(&a); hlv1_packet_free(&b);
    hlv1_frame_free(&first); hlv1_frame_free(&second);
    hlv1_encoder_destroy(original); hlv1_encoder_destroy(clone);
    if (failed) fprintf(stderr, "encoder clone diverged\n");
    return failed;
}

static int test_segmented_decode(void) {
    HLV1Header h = {64,48,15,1,0,4,55,4,0,HLV1_VERSION};
    HLV1Encoder *e = hlv1_encoder_create(&h, 1000.0);
    HLV1Decoder *d = hlv1_decoder_create(&h);
    HLV1Frame input;
    if (!e || !d || hlv1_frame_alloc(&input, h.width, h.height) < 0)
        return 1;
    make_motion_frame(&input, 3);

    HLV1Packet contiguous, segmented;
    const HLV1Frame *encoded = NULL, *decoded = NULL;
    if (hlv1_encoder_encode(e, &input, &contiguous, &encoded) < 0)
        return 1;
    FILE *file = tmpfile();
    if (!file || hlv1_packet_write(file, &contiguous) < 0) return 1;

    const size_t block_size = 7;
    size_t block_count = (contiguous.payload_size + block_size - 1) / block_size;
    uint8_t **blocks = (uint8_t **)calloc(block_count, sizeof *blocks);
    if (!blocks) return 1;
    for (size_t i = 0; i < block_count; ++i) {
        blocks[i] = (uint8_t *)malloc(block_size);
        if (!blocks[i]) return 1;
    }
    rewind(file);
    int result = hlv1_packet_read_blocks(file, &segmented, blocks,
                                         block_count, block_size);
    if (result == HLV1_OK)
        result = hlv1_decoder_decode_blocks(d, &segmented, &decoded);
    int failed = result != HLV1_OK || !same_frame(encoded, decoded);

    hlv1_packet_free(&segmented);
    hlv1_packet_free(&contiguous);
    for (size_t i = 0; i < block_count; ++i) free(blocks[i]);
    free(blocks);
    fclose(file);
    hlv1_frame_free(&input);
    hlv1_encoder_destroy(e);
    hlv1_decoder_destroy(d);
    if (failed) fprintf(stderr, "segmented packet decode failed\n");
    return failed;
}

static int test_skip_run_v15(void) {
    HLV1Header h14 = {64,16,25,1,0,100,55,4,0,HLV1_STREAM_VERSION_14};
    HLV1Header h15 = h14;
    h15.version = HLV1_STREAM_VERSION_15;
    HLV1Encoder *e14 = hlv1_encoder_create(&h14, 1000.0);
    HLV1Encoder *e15 = hlv1_encoder_create(&h15, 1000.0);
    HLV1Decoder *d15 = hlv1_decoder_create(&h15);
    HLV1Frame input;
    if (!e14 || !e15 || !d15 || hlv1_frame_alloc(&input, 64, 16) < 0)
        return 2;
    make_constant_frame(&input, 72, 105, 149);
    HLV1Packet key14 = {0}, key15 = {0};
    const HLV1Frame *rec14 = NULL, *rec15 = NULL, *decoded = NULL;
    int r = hlv1_encoder_encode(e14, &input, &key14, &rec14);
    if (r >= 0) r = hlv1_encoder_encode(e15, &input, &key15, &rec15);
    if (r >= 0) r = hlv1_decoder_decode(d15, &key15, &decoded);
    hlv1_packet_free(&key14);
    hlv1_packet_free(&key15);
    if (r < 0) return 1;

    for (int y = 0; y < 16; ++y)
        memset(input.y + y * input.stride_y + 48, 218, 16);
    for (int y = 0; y < 8; ++y) {
        memset(input.u + y * input.stride_u + 24, 61, 8);
        memset(input.v + y * input.stride_v + 24, 191, 8);
    }
    HLV1Packet p14 = {0}, p15 = {0};
    r = hlv1_encoder_encode(e14, &input, &p14, &rec14);
    if (r >= 0) r = hlv1_encoder_encode(e15, &input, &p15, &rec15);
    if (r >= 0) r = hlv1_decoder_decode(d15, &p15, &decoded);
    const HLV1Stats *stats = hlv1_encoder_stats(e15);
    int failed = r < 0 || p15.frame_type != HLV1_FRAME_P ||
                 !stats || !stats->skip_runs ||
                 p15.bit_length >= p14.bit_length ||
                 !same_frame(rec15, decoded) || !same_frame(rec14, rec15);
    hlv1_packet_free(&p14);
    hlv1_packet_free(&p15);
    hlv1_frame_free(&input);
    hlv1_encoder_destroy(e14);
    hlv1_encoder_destroy(e15);
    hlv1_decoder_destroy(d15);
    if (failed) fprintf(stderr, "v15 SKIP_RUN test failed\n");
    return failed;
}

static int test_rect_split_v15(void) {
    HLV1Header h = {48,16,25,1,0,100,100,4,0,HLV1_STREAM_VERSION_15};
    HLV1Encoder *encoder = hlv1_encoder_create(&h, 1000.0);
    HLV1Decoder *decoder = hlv1_decoder_create(&h);
    HLV1Frame base, moved;
    if (!encoder || !decoder || hlv1_frame_alloc(&base, 48, 16) < 0 ||
        hlv1_frame_alloc(&moved, 48, 16) < 0 ||
        hlv1_encoder_set_quantization(encoder, 1, 1) < 0)
        return 2;
    make_texture_frame(&base);
    HLV1Packet packet = {0};
    const HLV1Frame *encoded = NULL, *decoded = NULL;
    int r = hlv1_encoder_encode(encoder, &base, &packet, &encoded);
    if (r >= 0) r = hlv1_decoder_decode(decoder, &packet, &decoded);
    hlv1_packet_free(&packet);
    if (r < 0 || hlv1_frame_copy_visible(&moved, encoded) < 0)
        return 1;

    for (int y = 0; y < 16; ++y) {
        int dx = y < 8 ? 2 : -2;
        for (int x = 16; x < 32; ++x)
            moved.y[y * moved.stride_y + x] =
                encoded->y[y * encoded->stride_y + x + dx];
    }
    for (int y = 0; y < 8; ++y) {
        int dx = y < 4 ? 1 : -1;
        for (int x = 8; x < 16; ++x) {
            moved.u[y * moved.stride_u + x] =
                encoded->u[y * encoded->stride_u + x + dx];
            moved.v[y * moved.stride_v + x] =
                encoded->v[y * encoded->stride_v + x + dx];
        }
    }
    r = hlv1_encoder_encode(encoder, &moved, &packet, &encoded);
    if (r >= 0) r = hlv1_decoder_decode(decoder, &packet, &decoded);
    const HLV1Stats *stats = hlv1_encoder_stats(encoder);
    int failed = r < 0 || !stats || !stats->rect_split ||
                 !same_frame(encoded, decoded);
    hlv1_packet_free(&packet);
    hlv1_frame_free(&base);
    hlv1_frame_free(&moved);
    hlv1_encoder_destroy(encoder);
    hlv1_decoder_destroy(decoder);
    if (failed) fprintf(stderr, "RECT_SPLIT not exercised in v15\n");
    return failed;
}

int main(void) {
    if (test_quality_qstep_range()) {
        fprintf(stderr, "quality/qstep range test failed\n");
        return 1;
    }
    if (test_version(HLV1_STREAM_VERSION_14)) {
        fprintf(stderr, "v14 round-trip test failed\n"); return 1;
    }
    if (test_version(HLV1_STREAM_VERSION_15)) {
        fprintf(stderr, "v15 round-trip test failed\n"); return 1;
    }
    if (test_palette8_v14()) {
        fprintf(stderr, "v14 palette8 test failed\n"); return 1;
    }
    if (test_literal_v14()) {
        fprintf(stderr, "v14 literal test failed\n"); return 1;
    }
    if (test_adaptive_gop_v14()) {
        fprintf(stderr, "v14 adaptive GOP test failed\n"); return 1;
    }
    if (test_encoder_clone()) {
        fprintf(stderr, "encoder clone test failed\n"); return 1;
    }
    if (test_segmented_decode()) {
        fprintf(stderr, "segmented decode test failed\n"); return 1;
    }
    if (test_skip_run_v15()) return 1;
    if (test_joint_split_v15()) return 1;
    if (test_rect_split_v15()) return 1;
    puts("HLV v14/v15 round-trip including REPEAT/SKIP_RUN/LITERAL/PALETTE8/FILL/SPLIT: PASS");
    return 0;
}
