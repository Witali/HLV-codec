/*
 * Common HLV-1 implementation: container I/O, frame allocation, CRC,
 * bitstream primitives, quality mapping, and the integer 4x4 transform.
 */
#include "hlv1_internal.h"

#include <errno.h>

/* File and packet magic values deliberately differ so a lost packet boundary
 * cannot be mistaken for a sequence header. */
static const uint8_t HLV1_MAGIC[4] = {'H','L','V','1'};
static const uint8_t HLV1_FRAME_MAGIC[4] = {'F','R','M','1'};

const char *hlv1_strerror(int result) {
    switch (result) {
    case HLV1_OK: return "ok";
    case HLV1_EOF: return "end of file";
    case HLV1_ERR_ARGUMENT: return "invalid argument";
    case HLV1_ERR_MEMORY: return "out of memory";
    case HLV1_ERR_IO: return "I/O error";
    case HLV1_ERR_FORMAT: return "invalid or unsupported HLV-1 format";
    case HLV1_ERR_CRC: return "frame CRC mismatch";
    case HLV1_ERR_RANGE: return "value outside valid range";
    case HLV1_ERR_BITSTREAM: return "invalid or truncated bitstream";
    default: return "unknown error";
    }
}

/* --- CRC-32 ------------------------------------------------------------- */
static uint32_t crc_table[256];
static int crc_ready;

static void crc_init(void) {
    if (crc_ready) return;
    for (unsigned i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (unsigned j = 0; j < 8; ++j)
            c = (c >> 1) ^ (0xEDB88320U & (uint32_t)-(int32_t)(c & 1));
        crc_table[i] = c;
    }
    crc_ready = 1;
}

uint32_t hlv1_crc32(const uint8_t *data, size_t size) {
    crc_init();
    uint32_t c = 0xFFFFFFFFU;
    for (size_t i = 0; i < size; ++i)
        c = crc_table[(c ^ data[i]) & 0xFFU] ^ (c >> 8);
    return c ^ 0xFFFFFFFFU;
}

/* --- Fixed-size container headers -------------------------------------- */
int hlv1_header_write(FILE *file, const HLV1Header *h) {
    if (!file || !h || !h->width || !h->height || !h->fps_num || !h->fps_den)
        return HLV1_ERR_ARGUMENT;
    uint8_t b[HLV1_HEADER_SIZE] = {0};
    memcpy(b, HLV1_MAGIC, 4);
    unsigned version = hlv1_stream_version(h);
    if (version < HLV1_STREAM_VERSION_1 || version > HLV1_VERSION)
        return HLV1_ERR_ARGUMENT;
    b[4] = (uint8_t)version;
    b[5] = h->flags;
    hlv1_wr16(b + 6, h->width);
    hlv1_wr16(b + 8, h->height);
    hlv1_wr16(b + 10, h->fps_num);
    hlv1_wr16(b + 12, h->fps_den);
    hlv1_wr32(b + 14, h->frame_count);
    hlv1_wr16(b + 18, h->gop);
    b[20] = h->quality;
    b[21] = h->search_radius;
    b[22] = 16;
    b[23] = 0;
    hlv1_wr32(b + 24, 0);
    return fwrite(b, 1, sizeof b, file) == sizeof b ? HLV1_OK : HLV1_ERR_IO;
}

int hlv1_header_read(FILE *file, HLV1Header *h) {
    if (!file || !h) return HLV1_ERR_ARGUMENT;
    uint8_t b[HLV1_HEADER_SIZE];
    size_t got = fread(b, 1, sizeof b, file);
    if (got == 0 && feof(file)) return HLV1_EOF;
    if (got != sizeof b) return HLV1_ERR_IO;
    if (memcmp(b, HLV1_MAGIC, 4) ||
        b[4] < HLV1_STREAM_VERSION_1 || b[4] > HLV1_VERSION || b[22] != 16)
        return HLV1_ERR_FORMAT;
    memset(h, 0, sizeof *h);
    h->flags = b[5];
    h->version = b[4];
    h->width = hlv1_rd16(b + 6);
    h->height = hlv1_rd16(b + 8);
    h->fps_num = hlv1_rd16(b + 10);
    h->fps_den = hlv1_rd16(b + 12);
    h->frame_count = hlv1_rd32(b + 14);
    h->gop = hlv1_rd16(b + 18);
    h->quality = b[20];
    h->search_radius = b[21];
    if (!h->width || !h->height || !h->fps_num || !h->fps_den)
        return HLV1_ERR_FORMAT;
    return HLV1_OK;
}

int hlv1_packet_write(FILE *file, const HLV1Packet *p) {
    if (!file || !p || (!p->payload && p->payload_size)) return HLV1_ERR_ARGUMENT;
    uint8_t b[HLV1_FRAME_HEADER_SIZE] = {0};
    memcpy(b, HLV1_FRAME_MAGIC, 4);
    b[4] = p->frame_type;
    b[5] = p->q_y;
    b[6] = p->q_uv;
    b[7] = p->q_shift;
    hlv1_wr32(b + 8, p->bit_length);
    hlv1_wr32(b + 12, p->payload_size);
    hlv1_wr32(b + 16, hlv1_crc32(p->payload, p->payload_size));
    if (fwrite(b, 1, sizeof b, file) != sizeof b) return HLV1_ERR_IO;
    if (p->payload_size && fwrite(p->payload, 1, p->payload_size, file) != p->payload_size)
        return HLV1_ERR_IO;
    return HLV1_OK;
}

int hlv1_packet_read(FILE *file, HLV1Packet *p) {
    if (!file || !p) return HLV1_ERR_ARGUMENT;
    uint8_t b[HLV1_FRAME_HEADER_SIZE];
    memset(p, 0, sizeof *p);
    size_t got = fread(b, 1, sizeof b, file);
    if (got == 0 && feof(file)) return HLV1_EOF;
    if (got != sizeof b) return HLV1_ERR_IO;
    if (memcmp(b, HLV1_FRAME_MAGIC, 4)) return HLV1_ERR_FORMAT;
    p->frame_type = b[4];
    p->q_y = b[5];
    p->q_uv = b[6];
    p->q_shift = b[7];
    p->bit_length = hlv1_rd32(b + 8);
    p->payload_size = hlv1_rd32(b + 12);
    uint32_t expected_crc = hlv1_rd32(b + 16);
    if (p->frame_type > HLV1_FRAME_P || !p->q_y || !p->q_uv ||
        p->q_shift > 3 || p->bit_length > p->payload_size * 8ULL || p->payload_size > (1U << 30))
        return HLV1_ERR_FORMAT;
    if (p->payload_size) {
        p->payload = (uint8_t *)malloc(p->payload_size);
        if (!p->payload) return HLV1_ERR_MEMORY;
        if (fread(p->payload, 1, p->payload_size, file) != p->payload_size) {
            hlv1_packet_free(p);
            return HLV1_ERR_IO;
        }
    }
    if (hlv1_crc32(p->payload, p->payload_size) != expected_crc) {
        hlv1_packet_free(p);
        return HLV1_ERR_CRC;
    }
    return HLV1_OK;
}

void hlv1_packet_free(HLV1Packet *p) {
    if (!p) return;
    free(p->payload);
    memset(p, 0, sizeof *p);
}

/* --- Padded YUV420 frame storage --------------------------------------- */
int hlv1_frame_alloc(HLV1Frame *f, int width, int height) {
    if (!f || width <= 0 || height <= 0 || width > 65535 || height > 65535)
        return HLV1_ERR_ARGUMENT;
    memset(f, 0, sizeof *f);
    f->width = width;
    f->height = height;
    f->padded_width = (width + 15) & ~15;
    f->padded_height = (height + 15) & ~15;
    f->stride_y = f->padded_width;
    f->stride_u = f->padded_width / 2;
    f->stride_v = f->padded_width / 2;
    size_t y_size = (size_t)f->stride_y * f->padded_height;
    size_t c_size = (size_t)f->stride_u * (f->padded_height / 2);
    if (y_size > SIZE_MAX - 2 * c_size) return HLV1_ERR_RANGE;
    f->storage = (uint8_t *)malloc(y_size + 2 * c_size);
    if (!f->storage) return HLV1_ERR_MEMORY;
    f->y = f->storage;
    f->u = f->y + y_size;
    f->v = f->u + c_size;
    memset(f->y, 0, y_size);
    memset(f->u, 128, c_size);
    memset(f->v, 128, c_size);
    return HLV1_OK;
}

void hlv1_frame_free(HLV1Frame *f) {
    if (!f) return;
    free(f->storage);
    memset(f, 0, sizeof *f);
}

int hlv1_frame_copy_visible(HLV1Frame *dst, const HLV1Frame *src) {
    if (!dst || !src || dst->width != src->width || dst->height != src->height)
        return HLV1_ERR_ARGUMENT;
    for (int y = 0; y < src->height; ++y)
        memcpy(dst->y + y * dst->stride_y, src->y + y * src->stride_y, src->width);
    int cw = (src->width + 1) / 2;
    int ch = (src->height + 1) / 2;
    for (int y = 0; y < ch; ++y) {
        memcpy(dst->u + y * dst->stride_u, src->u + y * src->stride_u, cw);
        memcpy(dst->v + y * dst->stride_v, src->v + y * src->stride_v, cw);
    }
    return HLV1_OK;
}

/* --- User-facing quality mapping --------------------------------------- */
int hlv1_quality_to_qsteps(int quality, int *q_y, int *q_uv) {
    if (!q_y || !q_uv) return HLV1_ERR_ARGUMENT;
    quality = HLV1_CLAMP(quality, 1, 100);
    int y = (int)llround(pow(2.0, (100.0 - quality) / 12.5));
    if (y < 1) y = 1;
    int uv = (int)llround(y * 1.35);
    if (uv < 1) uv = 1;
    /* q_y/q_uv are serialized as uint8_t in stream v1/v2.  Clamping is
       normative: without it low quality values wrapped modulo 256 and made
       rate/distortion behaviour non-monotonic. */
    if (y > 255) y = 255;
    if (uv > 255) uv = 255;
    *q_y = y;
    *q_uv = uv;
    return HLV1_OK;
}

/* --- Normative MSB-first bit writer ------------------------------------ */
static int bw_reserve(HLV1BitWriter *bw, size_t add) {
    if (bw->size + add <= bw->capacity) return HLV1_OK;
    size_t cap = bw->capacity ? bw->capacity : 256;
    while (cap < bw->size + add) {
        if (cap > SIZE_MAX / 2) return HLV1_ERR_MEMORY;
        cap *= 2;
    }
    uint8_t *p = (uint8_t *)realloc(bw->data, cap);
    if (!p) return HLV1_ERR_MEMORY;
    bw->data = p;
    bw->capacity = cap;
    return HLV1_OK;
}

void hlv1_bw_init(HLV1BitWriter *bw) { memset(bw, 0, sizeof *bw); }
void hlv1_bw_free(HLV1BitWriter *bw) { if (bw) { free(bw->data); memset(bw, 0, sizeof *bw); } }

int hlv1_bw_put(HLV1BitWriter *bw, uint32_t value, unsigned count) {
    if (!bw || count > 32 || (count < 32 && value >= (1U << count))) return HLV1_ERR_ARGUMENT;
    if (!count) return HLV1_OK;
    bw->bit_count += count;
    while (count) {
        unsigned take = HLV1_MIN(count, 8U - bw->bits);
        unsigned shift = count - take;
        uint32_t part = (value >> shift) & ((1U << take) - 1U);
        bw->cache = (bw->cache << take) | part;
        bw->bits += take;
        count -= take;
        if (bw->bits == 8) {
            int r = bw_reserve(bw, 1);
            if (r < 0) { bw->error = r; return r; }
            bw->data[bw->size++] = (uint8_t)bw->cache;
            bw->cache = 0;
            bw->bits = 0;
        }
    }
    return HLV1_OK;
}

int hlv1_bw_put_ue(HLV1BitWriter *bw, uint32_t value) {
    uint32_t code = value + 1U;
    if (!code) return HLV1_ERR_RANGE;
#if defined(__GNUC__) || defined(__clang__)
    unsigned leading = 31U - (unsigned)__builtin_clz(code);
#else
    unsigned leading = 0; for (uint32_t t = code; t >>= 1;) ++leading;
#endif
    int r = hlv1_bw_put(bw, 0, leading);
    if (r < 0) return r;
    return hlv1_bw_put(bw, code, leading + 1);
}

int hlv1_bw_put_se(HLV1BitWriter *bw, int32_t value) {
    uint64_t mapped = value <= 0 ? (uint64_t)(-(int64_t)value) * 2U
                                 : (uint64_t)value * 2U - 1U;
    if (mapped > UINT32_MAX) return HLV1_ERR_RANGE;
    return hlv1_bw_put_ue(bw, (uint32_t)mapped);
}

int hlv1_bw_append(HLV1BitWriter *dst, const HLV1BitWriter *src) {
    if (!dst || !src) return HLV1_ERR_ARGUMENT;
    uint64_t bits = src->bit_count;
    for (uint64_t pos = 0; pos < bits;) {
        unsigned n = (unsigned)HLV1_MIN((uint64_t)24, bits - pos);
        uint32_t v = 0;
        for (unsigned i = 0; i < n; ++i) {
            uint64_t bp = pos + i;
            uint8_t byte = src->data[bp >> 3];
            v = (v << 1) | ((byte >> (7 - (bp & 7))) & 1U);
        }
        int r = hlv1_bw_put(dst, v, n);
        if (r < 0) return r;
        pos += n;
    }
    return HLV1_OK;
}

int hlv1_bw_finish(HLV1BitWriter *bw) {
    if (!bw) return HLV1_ERR_ARGUMENT;
    if (bw->bits) {
        int r = bw_reserve(bw, 1);
        if (r < 0) return r;
        bw->data[bw->size++] = (uint8_t)(bw->cache << (8 - bw->bits));
        bw->cache = 0;
        bw->bits = 0;
    }
    return bw->error ? bw->error : HLV1_OK;
}

/* --- Normative MSB-first bit reader ------------------------------------ */
static void br_refill(HLV1BitReader *br) {
    while (br->bits <= 56 && br->ptr < br->end) {
        br->cache |= (uint64_t)(*br->ptr++) << (56 - br->bits);
        br->bits += 8;
    }
}

void hlv1_br_init(HLV1BitReader *br, const uint8_t *data, size_t size, uint32_t valid_bits) {
    memset(br, 0, sizeof *br);
    br->ptr = data;
    br->end = data + size;
    br->bits_left = valid_bits;
    br_refill(br);
}

uint32_t hlv1_br_get(HLV1BitReader *br, unsigned count) {
    if (!br || count > 32 || count > br->bits_left) {
        if (br) br->error = HLV1_ERR_BITSTREAM;
        return 0;
    }
    if (br->bits < count) br_refill(br);
    if (br->bits < count) { br->error = HLV1_ERR_BITSTREAM; return 0; }
    uint32_t v = count ? (uint32_t)(br->cache >> (64 - count)) : 0;
    if (count) br->cache <<= count;
    br->bits -= count;
    br->bits_left -= count;
    br_refill(br);
    return v;
}

uint32_t hlv1_br_get_ue(HLV1BitReader *br) {
    if (!br || !br->bits_left) { if (br) br->error = HLV1_ERR_BITSTREAM; return 0; }
    if (br->bits < 32) br_refill(br);
    unsigned leading;
#if defined(__GNUC__) || defined(__clang__)
    leading = br->cache ? (unsigned)__builtin_clzll(br->cache) : 64U;
#else
    leading = 0; uint64_t mask = 1ULL << 63; while (mask && !(br->cache & mask)) { ++leading; mask >>= 1; }
#endif
    if (leading > 31 || leading + 1 > br->bits_left) {
        br->error = HLV1_ERR_BITSTREAM;
        return 0;
    }
    (void)hlv1_br_get(br, leading + 1);
    uint32_t suffix = leading ? hlv1_br_get(br, leading) : 0;
    if (br->error) return 0;
    return ((1U << leading) | suffix) - 1U;
}

int32_t hlv1_br_get_se(HLV1BitReader *br) {
    uint32_t mapped = hlv1_br_get_ue(br);
    if (br->error) return 0;
    return (mapped & 1U) ? (int32_t)((mapped + 1U) / 2U)
                         : -(int32_t)((mapped + 1U) / 2U);
}

/* --- Integer 4x4 Walsh-Hadamard transform ------------------------------ */
static void wht1d(const int32_t x[4], int32_t y[4]) {
    int32_t a = x[0] + x[1];
    int32_t b = x[0] - x[1];
    int32_t c = x[2] + x[3];
    int32_t d = x[2] - x[3];
    y[0] = a + c;
    y[1] = b + d;
    y[2] = a - c;
    y[3] = b - d;
}

void hlv1_wht4_forward(const int16_t in[16], int32_t out[16]) {
    int32_t tmp[16];
    for (int r = 0; r < 4; ++r) {
        int32_t x[4] = {in[r*4], in[r*4+1], in[r*4+2], in[r*4+3]};
        wht1d(x, &tmp[r*4]);
    }
    for (int c = 0; c < 4; ++c) {
        int32_t x[4] = {tmp[c], tmp[4+c], tmp[8+c], tmp[12+c]}, y[4];
        wht1d(x, y);
        for (int r = 0; r < 4; ++r) out[r*4+c] = y[r];
    }
}

void hlv1_wht4_inverse(const int32_t in[16], int16_t out[16]) {
    int32_t tmp[16], raw[16];
    for (int r = 0; r < 4; ++r) wht1d(&in[r*4], &tmp[r*4]);
    for (int c = 0; c < 4; ++c) {
        int32_t x[4] = {tmp[c], tmp[4+c], tmp[8+c], tmp[12+c]}, y[4];
        wht1d(x, y);
        for (int r = 0; r < 4; ++r) raw[r*4+c] = y[r];
    }
    for (int i = 0; i < 16; ++i) {
        int32_t v = raw[i];
        out[i] = (int16_t)(v >= 0 ? (v + 8) / 16 : -((-v + 8) / 16));
    }
}
