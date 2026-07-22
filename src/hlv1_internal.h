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

/* A zero version appeared in early in-memory callers and is normalized to v1. */
static inline unsigned hlv1_stream_version(const HLV1Header *h) {
    return h && h->version ? h->version : HLV1_STREAM_VERSION_1;
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
typedef struct HLV1BitReader {
    const uint8_t *ptr;
    const uint8_t *end;
    uint64_t cache;
    unsigned bits;
    uint64_t bits_left;
    int error;
} HLV1BitReader;

void hlv1_br_init(HLV1BitReader *br, const uint8_t *data,
                  size_t size, uint32_t valid_bits);
uint32_t hlv1_br_get(HLV1BitReader *br, unsigned count);
uint32_t hlv1_br_get_ue(HLV1BitReader *br);
int32_t hlv1_br_get_se(HLV1BitReader *br);

/* Reversible integer transform used for every coded 4x4 residual block. */
void hlv1_wht4_forward(const int16_t in[16], int32_t out[16]);
void hlv1_wht4_inverse(const int32_t in[16], int16_t out[16]);

static inline uint8_t hlv1_clip8(int value) {
    if ((unsigned)value <= 255U) return (uint8_t)value;
    return (uint8_t)(value < 0 ? 0 : 255);
}

#endif
