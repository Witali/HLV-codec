/*
 * HLV-1 internal primitives shared by the encoder and decoder.
 *
 * This header is not part of the public ABI.  It contains endian helpers,
 * the normative MSB-first bitstream reader/writer, CRC declarations, and the
 * integer 4x4 Walsh-Hadamard transform.
 */
#ifndef HLV1_INTERNAL_H
#define HLV1_INTERNAL_H

#include "hlv1.h"

#ifndef HLV1_FAST_32BIT_BITREADER
#define HLV1_FAST_32BIT_BITREADER 0
#endif

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HLV1_MIN(a,b) ((a) < (b) ? (a) : (b))
#define HLV1_MAX(a,b) ((a) > (b) ? (a) : (b))
#define HLV1_CLAMP(v,lo,hi) (HLV1_MIN(HLV1_MAX((v),(lo)),(hi)))

/* Container integers are little-endian regardless of host architecture. */
static inline uint16_t hlv1_rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t hlv1_rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void hlv1_wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static inline void hlv1_wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

uint32_t hlv1_crc32(const uint8_t *data, size_t size);
void hlv1_frame_quantize_v14_reference_mb(HLV1Frame *frame,
                                          int macroblock_x,
                                          int macroblock_y);
uint32_t hlv1_crc32_begin(void);
uint32_t hlv1_crc32_update(uint32_t crc, const uint8_t *data, size_t size);
uint32_t hlv1_crc32_end(uint32_t crc);
int hlv1_packet_header_parse(const uint8_t header[HLV1_FRAME_HEADER_SIZE],
                             HLV1Packet *packet, uint32_t *expected_crc);

/* A zero-initialized in-memory header selects the stable v14 syntax. */
static inline unsigned hlv1_stream_version(const HLV1Header *h) {
    return h && h->version ? h->version : HLV1_VERSION;
}

/* MSB-first writer compatible with HLV-1 v0.1. */
typedef struct HLV1BitWriter {
    uint8_t *data;
    size_t size;
    size_t capacity;
    uint64_t cache;
    unsigned bits;
    uint64_t bit_count;
    int error;
} HLV1BitWriter;

void hlv1_bw_init(HLV1BitWriter *bw);
void hlv1_bw_free(HLV1BitWriter *bw);
int hlv1_bw_put(HLV1BitWriter *bw, uint32_t value, unsigned count);
int hlv1_bw_put_ue(HLV1BitWriter *bw, uint32_t value);
int hlv1_bw_put_se(HLV1BitWriter *bw, int32_t value);
int hlv1_bw_append(HLV1BitWriter *dst, const HLV1BitWriter *src);
int hlv1_bw_finish(HLV1BitWriter *bw);

/* Reader mirrors HLV1BitWriter.  bits_left is the normative valid-bit count,
 * so padding bits in the final byte are never interpreted as syntax. */
typedef size_t (*HLV1BitReaderRefill)(void *context,
                                      const uint8_t **data,
                                      int *error);

typedef struct HLV1BitReader {
    const uint8_t *ptr;
    const uint8_t *end;
    const HLV1Packet *packet;
    HLV1BitReaderRefill refill;
    void *refill_context;
    size_t next_offset;
    size_t byte_limit;
#if HLV1_FAST_32BIT_BITREADER
    uint32_t cache;
#else
    uint64_t cache;
#endif
    unsigned bits;
#if HLV1_FAST_32BIT_BITREADER
    uint32_t bits_left;
#else
    uint64_t bits_left;
#endif
    int error;
} HLV1BitReader;

void hlv1_br_init(HLV1BitReader *br, const uint8_t *data,
                  size_t size, uint32_t valid_bits);
void hlv1_br_init_packet(HLV1BitReader *br, const HLV1Packet *packet);
void hlv1_br_init_stream(HLV1BitReader *br, uint32_t valid_bits,
                         HLV1BitReaderRefill refill, void *context);
#if HLV1_FAST_32BIT_BITREADER
uint32_t hlv1_br_get_slow(HLV1BitReader *br, unsigned count);

/* Keep the overwhelmingly common in-cache extraction in the caller.  The
 * slow helper validates malformed requests, refills an exhausted cache and
 * handles a value split across packet spans.  Refilling is deliberately lazy:
 * eagerly topping the cache up after every syntax field used to call the
 * sizeable refill routine thousands of times per frame. */
static inline uint32_t hlv1_br_get(HLV1BitReader *br, unsigned count) {
    if (br && count <= 32U && count <= br->bits &&
        count <= br->bits_left) {
        uint32_t value =
            count ? br->cache >> (32U - count) : 0;
        if (count) {
            br->cache = count == 32U ? 0 : br->cache << count;
            br->bits -= count;
            br->bits_left -= count;
        }
        return value;
    }
    return hlv1_br_get_slow(br, count);
}
#else
uint32_t hlv1_br_get(HLV1BitReader *br, unsigned count);
#endif
uint32_t hlv1_br_get_ue(HLV1BitReader *br);
int32_t hlv1_br_get_se(HLV1BitReader *br);
int hlv1_br_read_bytes(HLV1BitReader *br, uint8_t *destination,
                       size_t bytes);

/* Reversible integer transform used for every coded 4x4 residual block. */
void hlv1_wht4_forward(const int16_t in[16], int32_t out[16]);
void hlv1_wht4_inverse(const int32_t in[16], int16_t out[16]);
void hlv1_wht4_inverse_add(const int32_t in[16], uint8_t *destination,
                           int stride);

static inline uint8_t hlv1_clip8(int value) {
    if ((unsigned)value <= 255U) return (uint8_t)value;
    return (uint8_t)(value < 0 ? 0 : 255);
}

#endif
