/*
 * Common HLV-1 implementation: container I/O, frame allocation, CRC,
 * bitstream primitives, quality mapping, and the integer 4x4 transform.
 */
#include "hlv1_internal.h"

#include <errno.h>

#if HLV1_IRAM_BITREADER || HLV1_IRAM_INVERSE_WHT
#include "esp_attr.h"
#endif

#if HLV1_IRAM_BITREADER
#define HLV1_BITREADER_ATTR IRAM_ATTR
#else
#define HLV1_BITREADER_ATTR
#endif

#if HLV1_IRAM_INVERSE_WHT
#define HLV1_INVERSE_WHT_ATTR IRAM_ATTR
#else
#define HLV1_INVERSE_WHT_ATTR
#endif

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
    case HLV1_ERR_CRC: return "packet CRC mismatch";
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

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size) {
    while (size >= 4U) {
        crc = crc_table[(crc ^ data[0]) & 0xFFU] ^ (crc >> 8);
        crc = crc_table[(crc ^ data[1]) & 0xFFU] ^ (crc >> 8);
        crc = crc_table[(crc ^ data[2]) & 0xFFU] ^ (crc >> 8);
        crc = crc_table[(crc ^ data[3]) & 0xFFU] ^ (crc >> 8);
        data += 4;
        size -= 4;
    }
    for (size_t i = 0; i < size; ++i)
        crc = crc_table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    return crc;
}

uint32_t hlv1_crc32_begin(void) {
    crc_init();
    return 0xFFFFFFFFU;
}

uint32_t hlv1_crc32_update(uint32_t crc, const uint8_t *data, size_t size) {
    return crc32_update(crc, data, size);
}

uint32_t hlv1_crc32_end(uint32_t crc) {
    return crc ^ 0xFFFFFFFFU;
}

size_t hlv1_packet_payload_span(const HLV1Packet *p, size_t offset,
                                const uint8_t **data) {
    if (data) *data = NULL;
    if (!p || !data || offset >= p->payload_size) return 0;
    size_t remaining = (size_t)p->payload_size - offset;
    if (p->payload) {
        if (p->payload_blocks) return 0;
        *data = p->payload + offset;
        return remaining;
    }
    if (!p->payload_blocks || !p->payload_block_count ||
        !p->payload_block_size)
        return 0;
    size_t block = offset / p->payload_block_size;
    size_t within = offset % p->payload_block_size;
    if (block >= p->payload_block_count || !p->payload_blocks[block]) return 0;
    *data = p->payload_blocks[block] + within;
    return HLV1_MIN(remaining, p->payload_block_size - within);
}

static int packet_storage_valid(const HLV1Packet *p) {
    if (!p) return 0;
    if (!p->payload_size)
        return !(p->payload && p->payload_blocks);
    size_t offset = 0;
    while (offset < p->payload_size) {
        const uint8_t *data;
        size_t span = hlv1_packet_payload_span(p, offset, &data);
        if (!span || !data) return 0;
        offset += span;
    }
    return 1;
}

static uint32_t packet_crc32(const HLV1Packet *p) {
    crc_init();
    uint32_t crc = 0xFFFFFFFFU;
    size_t offset = 0;
    while (offset < p->payload_size) {
        const uint8_t *data;
        size_t span = hlv1_packet_payload_span(p, offset, &data);
        if (!span) return 0;
        crc = crc32_update(crc, data, span);
        offset += span;
    }
    return crc ^ 0xFFFFFFFFU;
}

static int v14_reference_correction(int q4, int x, int y) {
    static const uint8_t threshold[16] = {
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5
    };
    int whole = q4 >= 0 ? q4 / 16 : -((-q4 + 15) / 16);
    int fraction = q4 - whole * 16;
    unsigned phase = ((unsigned)y & 3U) * 4U + ((unsigned)x & 3U);
    return whole + (threshold[phase] < fraction);
}

void hlv1_apply_v14_reference_correction_tile(
    uint8_t *base, int stride, int origin_x, int origin_y, int8_t q4) {
    for (int y = 0; y < 8; ++y) {
        uint8_t *row = base + y * stride;
        for (int x = 0; x < 8; ++x) {
            int value = row[x] + v14_reference_correction(
                                      q4, origin_x + x, origin_y + y);
            row[x] = (uint8_t)HLV1_CLAMP(value, 0, 255);
        }
    }
}

int8_t hlv1_correct_v14_reference_tile(
    uint8_t *quantized, int quantized_stride,
    const uint8_t *source, int source_stride,
    int origin_x, int origin_y) {
    int error_sum = 0;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            error_sum += source[y * source_stride + x] -
                         quantized[y * quantized_stride + x];
    int q4 = error_sum >= 0
                 ? (error_sum + 2) / 4
                 : -((-error_sum + 2) / 4);
    q4 = HLV1_CLAMP(q4, -128, 127);
    hlv1_apply_v14_reference_correction_tile(
        quantized, quantized_stride, origin_x, origin_y, (int8_t)q4);
    return (int8_t)q4;
}

static void quantize_v14_reference_tile(uint8_t *base, int stride,
                                        int origin_x, int origin_y,
                                        unsigned shift) {
    unsigned maximum = (1U << (8U - shift)) - 1U;
    int error_sum = 0;
    for (int y = 0; y < 8; ++y) {
        uint8_t *row = base + y * stride;
        for (int x = 0; x < 8; ++x) {
            int original = row[x];
            unsigned code =
                ((unsigned)original + (1U << (shift - 1U))) >> shift;
            if (code > maximum) code = maximum;
            int quantized = (int)(code << shift);
            row[x] = (uint8_t)quantized;
            error_sum += original - quantized;
        }
    }
    int q4 = error_sum >= 0
                 ? (error_sum + 2) / 4
                 : -((-error_sum + 2) / 4);
    hlv1_apply_v14_reference_correction_tile(
        base, stride, origin_x, origin_y, (int8_t)q4);
}

void hlv1_frame_quantize_v14_reference_mb(HLV1Frame *frame,
                                          int macroblock_x,
                                          int macroblock_y) {
    for (int y = 0; y < 16; y += 8)
        for (int x = 0; x < 16; x += 8)
            quantize_v14_reference_tile(
                frame->y + (macroblock_y + y) * frame->stride_y +
                    macroblock_x + x,
                frame->stride_y, macroblock_x + x, macroblock_y + y, 1);
    int chroma_x = macroblock_x >> 1;
    int chroma_y = macroblock_y >> 1;
    quantize_v14_reference_tile(
        frame->u + chroma_y * frame->stride_u + chroma_x,
        frame->stride_u, chroma_x, chroma_y, 2);
    quantize_v14_reference_tile(
        frame->v + chroma_y * frame->stride_v + chroma_x,
        frame->stride_v, chroma_x, chroma_y, 2);
}

/* --- Fixed-size container headers -------------------------------------- */
int hlv1_header_write(FILE *file, const HLV1Header *h) {
    if (!file || !h || !h->width || !h->height || !h->fps_num || !h->fps_den)
        return HLV1_ERR_ARGUMENT;
    uint8_t b[HLV1_HEADER_SIZE] = {0};
    memcpy(b, HLV1_MAGIC, 4);
    unsigned version = hlv1_stream_version(h);
    if (version < HLV1_MIN_VERSION || version > HLV1_MAX_VERSION)
        return HLV1_ERR_ARGUMENT;
    if (h->flags & (uint8_t)~HLV1_FLAG_AUDIO)
        return HLV1_ERR_ARGUMENT;
    if (h->flags & HLV1_FLAG_AUDIO) {
        if (h->audio_codec != HLV1_AUDIO_PCM_U8 ||
            !h->audio_sample_rate || h->audio_channels != 1)
            return HLV1_ERR_ARGUMENT;
    } else if (h->audio_codec != HLV1_AUDIO_NONE ||
               h->audio_sample_rate || h->audio_channels) {
        return HLV1_ERR_ARGUMENT;
    }
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
    b[23] = h->audio_codec;
    hlv1_wr16(b + 24, h->audio_sample_rate);
    b[26] = h->audio_channels;
    b[27] = 0;
    return fwrite(b, 1, sizeof b, file) == sizeof b ? HLV1_OK : HLV1_ERR_IO;
}

int hlv1_header_read(FILE *file, HLV1Header *h) {
    if (!file || !h) return HLV1_ERR_ARGUMENT;
    uint8_t b[HLV1_HEADER_SIZE];
    size_t got = fread(b, 1, sizeof b, file);
    if (got == 0 && feof(file)) return HLV1_EOF;
    if (got != sizeof b) return HLV1_ERR_IO;
    if (memcmp(b, HLV1_MAGIC, 4) ||
        b[4] < HLV1_MIN_VERSION || b[4] > HLV1_MAX_VERSION ||
        b[22] != 16 || b[27] != 0)
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
    h->audio_codec = b[23];
    h->audio_sample_rate = hlv1_rd16(b + 24);
    h->audio_channels = b[26];
    if (!h->width || !h->height || !h->fps_num || !h->fps_den ||
        (h->flags & (uint8_t)~HLV1_FLAG_AUDIO))
        return HLV1_ERR_FORMAT;
    if (h->flags & HLV1_FLAG_AUDIO) {
        if (h->audio_codec != HLV1_AUDIO_PCM_U8 ||
            !h->audio_sample_rate || h->audio_channels != 1)
            return HLV1_ERR_FORMAT;
    } else if (h->audio_codec != HLV1_AUDIO_NONE ||
               h->audio_sample_rate || h->audio_channels) {
        return HLV1_ERR_FORMAT;
    }
    return HLV1_OK;
}

int hlv1_packet_write(FILE *file, const HLV1Packet *p) {
    if (!file || !packet_storage_valid(p)) return HLV1_ERR_ARGUMENT;
    uint8_t b[HLV1_FRAME_HEADER_SIZE] = {0};
    memcpy(b, HLV1_FRAME_MAGIC, 4);
    b[4] = p->frame_type;
    b[5] = p->q_y;
    b[6] = p->q_uv;
    b[7] = p->q_shift;
    hlv1_wr32(b + 8, p->bit_length);
    hlv1_wr32(b + 12, p->payload_size);
    hlv1_wr32(b + 16, packet_crc32(p));
    if (fwrite(b, 1, sizeof b, file) != sizeof b) return HLV1_ERR_IO;
    size_t offset = 0;
    while (offset < p->payload_size) {
        const uint8_t *data;
        size_t span = hlv1_packet_payload_span(p, offset, &data);
        if (!span || fwrite(data, 1, span, file) != span) return HLV1_ERR_IO;
        offset += span;
    }
    return HLV1_OK;
}

int hlv1_packet_header_parse(const uint8_t b[HLV1_FRAME_HEADER_SIZE],
                             HLV1Packet *p, uint32_t *expected_crc) {
    if (!b || !p || !expected_crc) return HLV1_ERR_ARGUMENT;
    if (memcmp(b, HLV1_FRAME_MAGIC, 4)) return HLV1_ERR_FORMAT;
    p->frame_type = b[4];
    p->q_y = b[5];
    p->q_uv = b[6];
    p->q_shift = b[7];
    p->bit_length = hlv1_rd32(b + 8);
    p->payload_size = hlv1_rd32(b + 12);
    *expected_crc = hlv1_rd32(b + 16);
    if (p->frame_type > HLV1_FRAME_P || !p->q_y || !p->q_uv ||
        p->q_shift > 3 || p->bit_length > p->payload_size * 8ULL ||
        p->payload_size > (1U << 30))
        return HLV1_ERR_FORMAT;
    return HLV1_OK;
}

static int packet_header_read(FILE *file, HLV1Packet *p,
                              uint32_t *expected_crc) {
    uint8_t b[HLV1_FRAME_HEADER_SIZE];
    size_t got = fread(b, 1, sizeof b, file);
    if (got == 0 && feof(file)) return HLV1_EOF;
    if (got != sizeof b) return HLV1_ERR_IO;
    return hlv1_packet_header_parse(b, p, expected_crc);
}

int hlv1_packet_read(FILE *file, HLV1Packet *p) {
    if (!file || !p) return HLV1_ERR_ARGUMENT;
    memset(p, 0, sizeof *p);
    uint32_t expected_crc;
    int result = packet_header_read(file, p, &expected_crc);
    if (result != HLV1_OK) return result;
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

int hlv1_packet_read_blocks(FILE *file, HLV1Packet *p,
                            uint8_t **blocks, size_t block_count,
                            size_t block_size) {
    if (!file || !p || !blocks || !block_count || !block_size ||
        block_count > SIZE_MAX / block_size)
        return HLV1_ERR_ARGUMENT;
    memset(p, 0, sizeof *p);
    uint32_t expected_crc;
    int result = packet_header_read(file, p, &expected_crc);
    if (result != HLV1_OK) return result;
    if (p->payload_size > block_count * block_size) {
        memset(p, 0, sizeof *p);
        return HLV1_ERR_MEMORY;
    }
    p->payload_blocks = blocks;
    p->payload_block_count = block_count;
    p->payload_block_size = block_size;
    crc_init();
    uint32_t crc = 0xFFFFFFFFU;
    size_t offset = 0;
    while (offset < p->payload_size) {
        size_t block = offset / block_size;
        size_t span = HLV1_MIN((size_t)p->payload_size - offset, block_size);
        if (!blocks[block] || fread(blocks[block], 1, span, file) != span) {
            hlv1_packet_free(p);
            return HLV1_ERR_IO;
        }
        crc = crc32_update(crc, blocks[block], span);
        offset += span;
    }
    if ((crc ^ 0xFFFFFFFFU) != expected_crc) {
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

size_t hlv1_packet_video_payload_size(const HLV1Packet *p) {
    if (!p) return 0;
    uint64_t size = ((uint64_t)p->bit_length + 7U) / 8U;
    return size <= p->payload_size ? (size_t)size : 0;
}

size_t hlv1_packet_audio_size(const HLV1Packet *p) {
    if (!p) return 0;
    size_t video_size = hlv1_packet_video_payload_size(p);
    if (!video_size && p->bit_length) return 0;
    return p->payload_size - video_size;
}

const uint8_t *hlv1_packet_audio_data(const HLV1Packet *p) {
    size_t audio_size = hlv1_packet_audio_size(p);
    if (!p || !audio_size || !p->payload || p->payload_blocks) return NULL;
    return p->payload + hlv1_packet_video_payload_size(p);
}

int hlv1_packet_append_audio(HLV1Packet *p,
                             const uint8_t *samples, size_t size) {
    if (!p || (!samples && size) ||
        p->payload_blocks || (p->payload_size && !p->payload) ||
        p->bit_length > (uint64_t)p->payload_size * 8U)
        return HLV1_ERR_ARGUMENT;
    if (!size) return HLV1_OK;
    if (size > UINT32_MAX - p->payload_size) return HLV1_ERR_RANGE;
    size_t new_size = (size_t)p->payload_size + size;
    uint8_t *payload = (uint8_t *)realloc(p->payload, new_size);
    if (!payload) return HLV1_ERR_MEMORY;
    memcpy(payload + p->payload_size, samples, size);
    p->payload = payload;
    p->payload_size = (uint32_t)new_size;
    return HLV1_OK;
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
    /* Prefer one cache-friendly allocation.  Small MCUs can have enough total
     * heap while no individual region can hold a complete QVGA YUV420 frame;
     * fall back to independently allocated planes in that case. */
    f->storage = (uint8_t *)malloc(y_size + 2 * c_size);
    if (f->storage) {
        f->y = f->storage;
        f->u = f->y + y_size;
        f->v = f->u + c_size;
    } else {
        f->y = (uint8_t *)malloc(y_size);
        f->u = (uint8_t *)malloc(c_size);
        f->v = (uint8_t *)malloc(c_size);
        f->storage = f->y;
        f->storage_mode = HLV1_FRAME_STORAGE_PLANAR;
        if (!f->y || !f->u || !f->v) {
            free(f->y);
            free(f->u);
            free(f->v);
            memset(f, 0, sizeof *f);
            return HLV1_ERR_MEMORY;
        }
    }
    memset(f->y, 0, y_size);
    memset(f->u, 128, c_size);
    memset(f->v, 128, c_size);
    return HLV1_OK;
}

void hlv1_frame_free(HLV1Frame *f) {
    if (!f) return;
    free(f->correction_storage);
    if (f->storage_mode != HLV1_FRAME_STORAGE_CONTIGUOUS) {
        free(f->u);
        free(f->v);
    }
    if (f->storage) {
        free(f->storage);
    }
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
#if HLV1_FAST_32BIT_BITREADER
#define HLV1_BR_CACHE_BITS 32U
#else
#define HLV1_BR_CACHE_BITS 64U
#endif

static int HLV1_BITREADER_ATTR br_load_span(HLV1BitReader *br) {
    if (br->ptr != br->end) return 1;
    if (br->refill) {
        const uint8_t *data = NULL;
        int error = HLV1_OK;
        size_t span = br->refill(br->refill_context, &data, &error);
        if (!span) {
            if (error < HLV1_OK) br->error = error;
            return 0;
        }
        if (!data) {
            br->error = HLV1_ERR_BITSTREAM;
            return 0;
        }
        br->ptr = data;
        br->end = data + span;
        return 1;
    }
    if (!br->packet || br->next_offset >= br->byte_limit) return 0;
    const uint8_t *data;
    size_t span = hlv1_packet_payload_span(
        br->packet, br->next_offset, &data);
    span = HLV1_MIN(span, br->byte_limit - br->next_offset);
    if (!span) {
        br->error = HLV1_ERR_BITSTREAM;
        return 0;
    }
    br->ptr = data;
    br->end = data + span;
    br->next_offset += span;
    return 1;
}

static void HLV1_BITREADER_ATTR br_refill(HLV1BitReader *br) {
    while (br->bits <= HLV1_BR_CACHE_BITS - 8U) {
        if (!br_load_span(br)) break;
        br->cache |=
            (uint64_t)(*br->ptr++) << (HLV1_BR_CACHE_BITS - 8U - br->bits);
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

void hlv1_br_init_packet(HLV1BitReader *br, const HLV1Packet *p) {
    memset(br, 0, sizeof *br);
    br->packet = p;
    br->byte_limit = hlv1_packet_video_payload_size(p);
    br->bits_left = p ? p->bit_length : 0;
    br_refill(br);
}

void hlv1_br_init_stream(HLV1BitReader *br, uint32_t valid_bits,
                         HLV1BitReaderRefill refill, void *context) {
    memset(br, 0, sizeof *br);
    br->refill = refill;
    br->refill_context = context;
    br->bits_left = valid_bits;
    br_refill(br);
}

#if HLV1_FAST_32BIT_BITREADER
uint32_t HLV1_BITREADER_ATTR hlv1_br_get_slow(HLV1BitReader *br,
                                              unsigned count) {
#else
uint32_t HLV1_BITREADER_ATTR hlv1_br_get(HLV1BitReader *br,
                                        unsigned count) {
#endif
    if (!br || count > 32 || count > br->bits_left) {
        if (br) br->error = HLV1_ERR_BITSTREAM;
        return 0;
    }
    if (br->bits < count) br_refill(br);
#if HLV1_FAST_32BIT_BITREADER
    uint32_t v = 0;
    if (br->bits < count) {
        unsigned prefix_bits = br->bits;
        unsigned remaining = count - prefix_bits;
        if (prefix_bits)
            v = br->cache >> (HLV1_BR_CACHE_BITS - prefix_bits);
        br->cache = 0;
        br->bits = 0;
        br_refill(br);
        if (br->bits < remaining) {
            br->error = HLV1_ERR_BITSTREAM;
            return 0;
        }
        v = (v << remaining) |
            (br->cache >> (HLV1_BR_CACHE_BITS - remaining));
        br->cache = remaining == HLV1_BR_CACHE_BITS
                        ? 0 : br->cache << remaining;
        br->bits -= remaining;
    } else if (count) {
        v = br->cache >> (HLV1_BR_CACHE_BITS - count);
        br->cache = count == HLV1_BR_CACHE_BITS ? 0 : br->cache << count;
        br->bits -= count;
    }
#else
    if (br->bits < count) { br->error = HLV1_ERR_BITSTREAM; return 0; }
    uint32_t v = count ? (uint32_t)(br->cache >> (64 - count)) : 0;
    if (count) br->cache <<= count;
    br->bits -= count;
#endif
    br->bits_left -= count;
    return v;
}

uint32_t HLV1_BITREADER_ATTR hlv1_br_get_ue(HLV1BitReader *br) {
    if (!br || !br->bits_left) { if (br) br->error = HLV1_ERR_BITSTREAM; return 0; }
#if HLV1_FAST_32BIT_BITREADER
#if defined(__GNUC__) || defined(__clang__)
    /* Most Exp-Golomb values fit completely in the current 32-bit cache.
     * Consume prefix, marker and suffix together so the common case performs
     * one cache update instead of two nested bitreader calls. */
    unsigned cached_zeros =
        br->cache ? (unsigned)__builtin_clz(br->cache) : 32U;
    unsigned cached_code_bits = cached_zeros * 2U + 1U;
    if (cached_zeros <= 15U &&
        cached_code_bits <= br->bits &&
        cached_code_bits <= br->bits_left) {
        return hlv1_br_get(br, cached_code_bits) - 1U;
    }
#endif
    unsigned leading = 0;
    for (;;) {
        if (!br->bits) br_refill(br);
        if (!br->bits) {
            br->error = HLV1_ERR_BITSTREAM;
            return 0;
        }
#if defined(__GNUC__) || defined(__clang__)
        unsigned zeros = br->cache ? (unsigned)__builtin_clz(br->cache) : 32U;
#else
        unsigned zeros = 0;
        uint32_t mask = UINT32_C(1) << 31;
        while (mask && !(br->cache & mask)) { ++zeros; mask >>= 1; }
#endif
        if (zeros < br->bits) {
            if (leading + zeros > 31U || zeros + 1U > br->bits_left) {
                br->error = HLV1_ERR_BITSTREAM;
                return 0;
            }
            leading += zeros;
            (void)hlv1_br_get(br, zeros + 1U);
            break;
        }
        unsigned available = br->bits;
        if (leading + available > 31U || available >= br->bits_left) {
            br->error = HLV1_ERR_BITSTREAM;
            return 0;
        }
        (void)hlv1_br_get(br, available);
        leading += available;
    }
#else
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
#endif
    uint32_t suffix = leading ? hlv1_br_get(br, leading) : 0;
    if (br->error) return 0;
    return ((1U << leading) | suffix) - 1U;
}

int32_t HLV1_BITREADER_ATTR hlv1_br_get_se(HLV1BitReader *br) {
    uint32_t mapped = hlv1_br_get_ue(br);
    if (br->error) return 0;
    return (mapped & 1U) ? (int32_t)((mapped + 1U) / 2U)
                         : -(int32_t)((mapped + 1U) / 2U);
}

int hlv1_br_read_bytes(HLV1BitReader *br, uint8_t *destination,
                       size_t bytes) {
    if (!br || (!destination && bytes) ||
        bytes > (size_t)(br->bits_left / 8U)) {
        if (br) br->error = HLV1_ERR_BITSTREAM;
        return HLV1_ERR_BITSTREAM;
    }

    /* A byte-aligned syntax position leaves a whole number of bytes in the
     * MSB-first cache. Drain those bytes before copying untouched packet spans
     * directly. Keep a general fallback for defensive callers. */
    if (br->bits & 7U) {
        for (size_t i = 0; i < bytes; ++i)
            destination[i] = (uint8_t)hlv1_br_get(br, 8);
        return br->error ? br->error : HLV1_OK;
    }
    while (bytes && br->bits >= 8U) {
        *destination++ = (uint8_t)hlv1_br_get(br, 8);
        --bytes;
    }
    while (bytes) {
        if (!br_load_span(br)) {
            if (!br->error) br->error = HLV1_ERR_BITSTREAM;
            return br->error;
        }
        size_t available = (size_t)(br->end - br->ptr);
        size_t count = HLV1_MIN(bytes, available);
        memcpy(destination, br->ptr, count);
        destination += count;
        br->ptr += count;
        br->bits_left -= (uint32_t)(count * 8U);
        bytes -= count;
    }
    return HLV1_OK;
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

void HLV1_INVERSE_WHT_ATTR
hlv1_wht4_inverse_add(const int32_t in[16], uint8_t *destination,
                      int stride) {
    int32_t tmp[16];
    for (int r = 0; r < 4; ++r)
        wht1d(&in[r * 4], &tmp[r * 4]);
    for (int c = 0; c < 4; ++c) {
        int32_t x[4] = {tmp[c], tmp[4 + c], tmp[8 + c], tmp[12 + c]};
        int32_t y[4];
        wht1d(x, y);
        for (int r = 0; r < 4; ++r) {
            int32_t value = y[r];
            int residual = value >= 0
                               ? (int)((value + 8) / 16)
                               : -(int)((-value + 8) / 16);
            destination[r * stride + c] = hlv1_clip8(
                (int)destination[r * stride + c] + residual);
        }
    }
}
