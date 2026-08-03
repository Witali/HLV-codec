#include "divx3.h"
#include "compact_yuv420.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef DIVX3_IRAM_BITREADER
#define DIVX3_IRAM_BITREADER 0
#endif

#if DIVX3_IRAM_BITREADER
#include "esp_attr.h"
#define DIVX3_BITREADER_ATTR IRAM_ATTR
#else
#define DIVX3_BITREADER_ATTR
#endif

typedef struct {
    uint8_t length;
    uint32_t code;
    uint8_t magnitude;
} Divx3DcVlc;

typedef struct {
    uint8_t length;
    uint16_t code;
    uint8_t run;
    uint8_t level;
    uint8_t last;
} Divx3TcoefVlc;

typedef struct {
    uint8_t length;
    uint32_t code;
    int8_t x;
    int8_t y;
} Divx3MotionVlc;

typedef struct {
    uint8_t length;
    uint32_t code;
    uint8_t intra;
    uint8_t cbp;
} Divx3MbVlc;

typedef struct {
    uint8_t length;
    uint16_t code;
    uint8_t cbp[6];
} Divx3McbpcVlc;

typedef struct {
    int16_t child[2];
} Divx3VlcNode;

typedef struct {
    const Divx3TcoefVlc *entries;
    size_t count;
    const Divx3VlcNode *nodes;
    uint16_t escape;
    uint8_t escape_length;
    const uint8_t (*max_level)[64];
    const uint8_t (*max_run)[64];
} Divx3TcoefSet;

#include "divx3_tables.inc"

typedef struct {
    const uint8_t *next;
    const uint8_t *end;
    uint8_t *refill;
    size_t refill_capacity;
    size_t unread_bytes;
    Divx3ReadFunction read;
    void *read_context;
    uint32_t cache;
    size_t bits;
    size_t position;
    unsigned cached;
    int failed;
} BitReader;

typedef struct {
    int run;
    int level;
    int last;
} AcCoefficient;

typedef struct {
    uint8_t *y;
    uint8_t *cb;
    uint8_t *cr;
    int8_t *correction_y;
    int8_t *correction_cb;
    int8_t *correction_cr;
} Divx3FrameBuffer;

struct Divx3Decoder {
    uint16_t width;
    uint16_t height;
    uint16_t padded_width;
    uint16_t padded_height;
    uint16_t chroma_width;
    uint16_t chroma_height;
    uint16_t mb_width;
    uint16_t mb_height;
    size_t y_bytes;
    size_t c_bytes;
    size_t y_correction_bytes;
    size_t c_correction_bytes;
    size_t frame_bytes;
    size_t memory_bytes;
    uint16_t y_stride;
    uint16_t c_stride;
    uint16_t correction_stride_y;
    uint16_t correction_stride_c;
    Divx3FrameBuffer frames[2];
    int16_t *dc_luma;
    int16_t *dc_cb;
    int16_t *dc_cr;
    int16_t *ac_luma_row;
    int16_t *ac_luma_col;
    int16_t *ac_cb_row;
    int16_t *ac_cb_col;
    int16_t *ac_cr_row;
    int16_t *ac_cr_col;
    uint8_t *coded_luma;
    int8_t *mv_x;
    int8_t *mv_y;
    uint32_t frame_number;
    uint8_t reference_index;
    uint8_t has_reference;
    uint8_t flipflop_rounding;
    uint8_t no_rounding;
    uint8_t compact_y6_u5_v5;
    uint8_t stream_buffer[DIVX3_STREAM_BUFFER_BYTES];
};

static const uint8_t kScanZigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static const uint8_t kScanAltHorizontal[64] = {
    0,  1,  2,  3,  8,  9,  16, 17, 10, 11, 4,  5,  6,  7,  15, 14,
    13, 12, 19, 18, 24, 25, 32, 33, 26, 27, 20, 21, 22, 23, 28, 29,
    30, 31, 34, 35, 40, 41, 48, 49, 42, 43, 36, 37, 38, 39, 44, 45,
    46, 47, 50, 51, 56, 57, 58, 59, 52, 53, 54, 55, 60, 61, 62, 63,
};

static const uint8_t kScanAltVertical[64] = {
    0,  8,  16, 24, 1,  9,  2,  10, 17, 25, 32, 40, 48, 56, 57, 49,
    41, 33, 26, 18, 3,  11, 4,  12, 19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28, 5,  13, 6,  14, 21, 29, 36, 44, 52, 60, 37, 45,
    53, 61, 22, 30, 7,  15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63,
};

static int DIVX3_BITREADER_ATTR
bitreader_next_byte(BitReader *reader, uint8_t *value) {
    if (reader->next >= reader->end) {
        size_t wanted;
        size_t received;
        if (!reader->read || !reader->unread_bytes ||
            !reader->refill || !reader->refill_capacity) {
            return 0;
        }
        wanted = reader->unread_bytes < reader->refill_capacity
                     ? reader->unread_bytes
                     : reader->refill_capacity;
        received = reader->read(
            reader->read_context, reader->refill, wanted);
        if (!received || received > wanted) {
            reader->failed = 1;
            return 0;
        }
        reader->next = reader->refill;
        reader->end = reader->refill + received;
        reader->unread_bytes -= received;
    }
    *value = *reader->next++;
    return 1;
}

static int DIVX3_BITREADER_ATTR
bitreader_fill_cache(BitReader *reader, unsigned count) {
    while (reader->cached < count) {
        uint8_t byte;
        if (reader->cached > 24U ||
            !bitreader_next_byte(reader, &byte)) {
            return 0;
        }
        reader->cache |=
            (uint32_t)byte << (24U - reader->cached);
        reader->cached += 8U;
    }
    return 1;
}

static int DIVX3_BITREADER_ATTR bit_read(BitReader *reader) {
    int value;
    if (reader->position >= reader->bits ||
        !bitreader_fill_cache(reader, 1U)) {
        reader->failed = 1;
        ++reader->position;
        return 0;
    }
    value = (int)(reader->cache >> 31);
    reader->cache <<= 1;
    --reader->cached;
    ++reader->position;
    return value;
}

static uint32_t DIVX3_BITREADER_ATTR bits_read(BitReader *reader,
                                               unsigned count) {
    uint32_t value;
    unsigned requested = count;
    if (!count) return 0;
    if (!bitreader_fill_cache(reader, count) ||
        reader->position > reader->bits ||
        count > reader->bits - reader->position) {
        reader->failed = 1;
        reader->position += count;
        reader->cache = 0;
        reader->cached = 0;
        return 0;
    }
    value = reader->cache >> (32U - count);
    reader->cache <<= count;
    reader->cached -= requested;
    reader->position += requested;
    return value;
}

static uint32_t bits_peek(BitReader *reader, unsigned count) {
    if (!count || count > 32U ||
        reader->position > reader->bits) {
        reader->failed = 1;
        return 0;
    }
    /*
     * TCOEF escape codes are longer than some direct VLC entries. Near the
     * end of a packet there may be too few bits left to form an escape while
     * still having enough for a valid direct entry. Treat that as a definite
     * non-match; the subsequent VLC walk validates the exact short code.
     */
    if (count > reader->bits - reader->position)
        return UINT32_MAX;
    if (!bitreader_fill_cache(reader, count)) {
        reader->failed = 1;
        return 0;
    }
    return reader->cache >> (32U - count);
}

static void bits_drop_cached(BitReader *reader, unsigned count) {
    reader->cache <<= count;
    reader->cached -= count;
    reader->position += count;
}

static int abs_int(int value) { return value < 0 ? -value : value; }

static uint8_t clamp_byte(int value) {
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static int DIVX3_BITREADER_ATTR
decode_vlc_index(BitReader *reader, const Divx3VlcNode *nodes,
                 size_t entry_count, size_t *entry_index) {
    int node_index = 0;
    unsigned depth = 0;
    if (reader->position <= reader->bits &&
        reader->bits - reader->position >= 8U) {
        uint32_t prefix;
        if (!bitreader_fill_cache(reader, 8U))
            return DIVX3_ERR_BITSTREAM;
        prefix = reader->cache;
        for (; depth < 8U; ++depth) {
            int child =
                nodes[node_index].child[(prefix >> (31U - depth)) & 1U];
            if (child == INT16_MAX) return DIVX3_ERR_BITSTREAM;
            if (child < 0) {
                size_t index = (size_t)(-child - 1);
                if (index >= entry_count) return DIVX3_ERR_BITSTREAM;
                bits_drop_cached(reader, depth + 1U);
                *entry_index = index;
                return DIVX3_OK;
            }
            node_index = child;
        }
        bits_drop_cached(reader, 8U);
    }
    for (; depth < 30U; ++depth) {
        int child = nodes[node_index].child[bit_read(reader)];
        if (reader->failed || child == INT16_MAX)
            return DIVX3_ERR_BITSTREAM;
        if (child < 0) {
            size_t index = (size_t)(-child - 1);
            if (index >= entry_count) return DIVX3_ERR_BITSTREAM;
            *entry_index = index;
            return DIVX3_OK;
        }
        node_index = child;
    }
    return DIVX3_ERR_BITSTREAM;
}

static int decode_dc(BitReader *reader, const Divx3DcVlc *table,
                     size_t count, const Divx3VlcNode *nodes,
                     int *difference) {
    size_t index;
    const Divx3DcVlc *entry;
    if (decode_vlc_index(reader, nodes, count, &index) != DIVX3_OK)
        return DIVX3_ERR_BITSTREAM;
    entry = table + index;
        if (entry->magnitude == 119) {
            int value = (int)bits_read(reader, 8);
            if (bit_read(reader)) value = -value;
            *difference = value;
        } else if (!entry->magnitude) {
            *difference = 0;
        } else {
            *difference = bit_read(reader) ? -(int)entry->magnitude
                                           : (int)entry->magnitude;
        }
    return reader->failed ? DIVX3_ERR_BITSTREAM : DIVX3_OK;
}

static int decode_tcoef_direct(BitReader *reader,
                               const Divx3TcoefSet *set,
                               const Divx3TcoefVlc **entry) {
    size_t index;
    if (decode_vlc_index(
            reader, set->nodes, set->count, &index) != DIVX3_OK)
        return DIVX3_ERR_BITSTREAM;
    *entry = set->entries + index;
    return DIVX3_OK;
}

static int decode_tcoef(BitReader *reader, const Divx3TcoefSet *set,
                        int run_difference, AcCoefficient *coefficient) {
    const Divx3TcoefVlc *entry = NULL;
    if (bits_peek(reader, set->escape_length) == set->escape) {
        bits_read(reader, set->escape_length);
        if (bit_read(reader)) {
            if (decode_tcoef_direct(reader, set, &entry) != DIVX3_OK)
                return DIVX3_ERR_BITSTREAM;
            coefficient->run = entry->run;
            coefficient->level =
                entry->level + set->max_level[entry->last][entry->run];
            coefficient->last = entry->last;
            if (bit_read(reader)) coefficient->level = -coefficient->level;
        } else if (bit_read(reader)) {
            int level_index;
            if (decode_tcoef_direct(reader, set, &entry) != DIVX3_OK)
                return DIVX3_ERR_BITSTREAM;
            level_index = entry->level < 64 ? entry->level : 63;
            coefficient->run =
                entry->run +
                set->max_run[entry->last][level_index] +
                run_difference;
            coefficient->level = entry->level;
            coefficient->last = entry->last;
            if (bit_read(reader)) coefficient->level = -coefficient->level;
        } else {
            int level;
            coefficient->last = bit_read(reader);
            coefficient->run = (int)bits_read(reader, 6);
            level = (int)bits_read(reader, 8);
            coefficient->level = level >= 128 ? level - 256 : level;
        }
    } else {
        if (decode_tcoef_direct(reader, set, &entry) != DIVX3_OK)
            return DIVX3_ERR_BITSTREAM;
        coefficient->run = entry->run;
        coefficient->level = entry->level;
        coefficient->last = entry->last;
        if (bit_read(reader)) coefficient->level = -coefficient->level;
    }
    return reader->failed || !coefficient->level
               ? DIVX3_ERR_BITSTREAM
               : DIVX3_OK;
}

static int decode_mcbpc(BitReader *reader, uint8_t cbp[6]) {
    size_t index;
    if (decode_vlc_index(
            reader, kMcbpcVlcNodes,
            sizeof(kMcbpcVlc) / sizeof(kMcbpcVlc[0]),
            &index) != DIVX3_OK)
        return DIVX3_ERR_BITSTREAM;
    memcpy(cbp, kMcbpcVlc[index].cbp, 6);
    return DIVX3_OK;
}

static int decode_mb(BitReader *reader, int *intra, int *cbp) {
    size_t index;
    if (decode_vlc_index(
            reader, kMbNonIntraVlcNodes,
            sizeof(kMbNonIntraVlc) / sizeof(kMbNonIntraVlc[0]),
            &index) != DIVX3_OK)
        return DIVX3_ERR_BITSTREAM;
    *intra = kMbNonIntraVlc[index].intra;
    *cbp = kMbNonIntraVlc[index].cbp;
    return DIVX3_OK;
}

static int decode_motion(BitReader *reader, int table_index,
                         int *x, int *y) {
    const Divx3MotionVlc *table =
        table_index ? kmvVLC1 : kmvVLC0;
    size_t count = table_index
                       ? sizeof(kmvVLC1) / sizeof(kmvVLC1[0])
                       : sizeof(kmvVLC0) / sizeof(kmvVLC0[0]);
    const Divx3VlcNode *nodes =
        table_index ? kmvVLC1Nodes : kmvVLC0Nodes;
    size_t index;
    const Divx3MotionVlc *entry;
    if (decode_vlc_index(reader, nodes, count, &index) != DIVX3_OK)
        return DIVX3_ERR_BITSTREAM;
    entry = table + index;
    if (entry->x == -32 && entry->y == -32) {
        *x = (int)bits_read(reader, 6) - 32;
        *y = (int)bits_read(reader, 6) - 32;
    } else {
        *x = entry->x;
        *y = entry->y;
    }
    return reader->failed ? DIVX3_ERR_BITSTREAM : DIVX3_OK;
}

static int read_c3(BitReader *reader) {
    if (!bit_read(reader)) return 0;
    return bit_read(reader) ? 2 : 1;
}

enum {
    IDCT_W1 = 22725,
    IDCT_W2 = 21407,
    IDCT_W3 = 19266,
    IDCT_W4 = 16383,
    IDCT_W5 = 12873,
    IDCT_W6 = 8867,
    IDCT_W7 = 4520,
    IDCT_ROW_SHIFT = 11,
    IDCT_COLUMN_SHIFT = 20,
    IDCT_COLUMN_ROUND = (1 << (IDCT_COLUMN_SHIFT - 1)) / IDCT_W4,
};

static void idct_row(int32_t *row) {
    int32_t a0 =
        IDCT_W4 * row[0] + (1 << (IDCT_ROW_SHIFT - 1));
    int32_t a1 = a0, a2 = a0, a3 = a0;
    int32_t b0, b1, b2, b3;
    if (!(row[1] | row[2] | row[3] | row[4] |
          row[5] | row[6] | row[7])) {
        int32_t value = a0 >> IDCT_ROW_SHIFT;
        unsigned index;
        for (index = 0; index < 8U; ++index) row[index] = value;
        return;
    }
    a0 += IDCT_W2 * row[2];
    a1 += IDCT_W6 * row[2];
    a2 -= IDCT_W6 * row[2];
    a3 -= IDCT_W2 * row[2];
    b0 = IDCT_W1 * row[1] + IDCT_W3 * row[3];
    b1 = IDCT_W3 * row[1] - IDCT_W7 * row[3];
    b2 = IDCT_W5 * row[1] - IDCT_W1 * row[3];
    b3 = IDCT_W7 * row[1] - IDCT_W5 * row[3];
    a0 += IDCT_W4 * row[4] + IDCT_W6 * row[6];
    a1 += -IDCT_W4 * row[4] - IDCT_W2 * row[6];
    a2 += -IDCT_W4 * row[4] + IDCT_W2 * row[6];
    a3 += IDCT_W4 * row[4] - IDCT_W6 * row[6];
    b0 += IDCT_W5 * row[5] + IDCT_W7 * row[7];
    b1 += -IDCT_W1 * row[5] - IDCT_W5 * row[7];
    b2 += IDCT_W7 * row[5] + IDCT_W3 * row[7];
    b3 += IDCT_W3 * row[5] - IDCT_W1 * row[7];
    row[0] = (a0 + b0) >> IDCT_ROW_SHIFT;
    row[7] = (a0 - b0) >> IDCT_ROW_SHIFT;
    row[1] = (a1 + b1) >> IDCT_ROW_SHIFT;
    row[6] = (a1 - b1) >> IDCT_ROW_SHIFT;
    row[2] = (a2 + b2) >> IDCT_ROW_SHIFT;
    row[5] = (a2 - b2) >> IDCT_ROW_SHIFT;
    row[3] = (a3 + b3) >> IDCT_ROW_SHIFT;
    row[4] = (a3 - b3) >> IDCT_ROW_SHIFT;
}

static void idct_column(int32_t *column) {
    int32_t a0 = IDCT_W4 * (column[0] + IDCT_COLUMN_ROUND);
    int32_t a1 = a0, a2 = a0, a3 = a0;
    int32_t b0, b1, b2, b3;
    a0 += IDCT_W2 * column[16];
    a1 += IDCT_W6 * column[16];
    a2 -= IDCT_W6 * column[16];
    a3 -= IDCT_W2 * column[16];
    b0 = IDCT_W1 * column[8] + IDCT_W3 * column[24];
    b1 = IDCT_W3 * column[8] - IDCT_W7 * column[24];
    b2 = IDCT_W5 * column[8] - IDCT_W1 * column[24];
    b3 = IDCT_W7 * column[8] - IDCT_W5 * column[24];
    a0 += IDCT_W4 * column[32] + IDCT_W6 * column[48];
    a1 += -IDCT_W4 * column[32] - IDCT_W2 * column[48];
    a2 += -IDCT_W4 * column[32] + IDCT_W2 * column[48];
    a3 += IDCT_W4 * column[32] - IDCT_W6 * column[48];
    b0 += IDCT_W5 * column[40] + IDCT_W7 * column[56];
    b1 += -IDCT_W1 * column[40] - IDCT_W5 * column[56];
    b2 += IDCT_W7 * column[40] + IDCT_W3 * column[56];
    b3 += IDCT_W3 * column[40] - IDCT_W1 * column[56];
    column[0] = (a0 + b0) >> IDCT_COLUMN_SHIFT;
    column[8] = (a1 + b1) >> IDCT_COLUMN_SHIFT;
    column[16] = (a2 + b2) >> IDCT_COLUMN_SHIFT;
    column[24] = (a3 + b3) >> IDCT_COLUMN_SHIFT;
    column[32] = (a3 - b3) >> IDCT_COLUMN_SHIFT;
    column[40] = (a2 - b2) >> IDCT_COLUMN_SHIFT;
    column[48] = (a1 - b1) >> IDCT_COLUMN_SHIFT;
    column[56] = (a0 - b0) >> IDCT_COLUMN_SHIFT;
}

static void inverse_dct(int32_t block[64]) {
    unsigned index;
    for (index = 1; index < 64U; ++index)
        if (block[index]) break;
    if (index == 64U) {
        int32_t row_value =
            (IDCT_W4 * block[0] +
             (1 << (IDCT_ROW_SHIFT - 1))) >>
            IDCT_ROW_SHIFT;
        int32_t value =
            IDCT_W4 * (row_value + IDCT_COLUMN_ROUND) >>
            IDCT_COLUMN_SHIFT;
        for (index = 0; index < 64U; ++index) block[index] = value;
        return;
    }
    for (index = 0; index < 64; index += 8) idct_row(block + index);
    for (index = 0; index < 8; ++index) idct_column(block + index);
}

static void inverse_dct_sparse(int32_t block[64], unsigned row_mask,
                               int has_ac) {
    unsigned index;
    if (!has_ac) {
        int32_t row_value =
            (IDCT_W4 * block[0] +
             (1 << (IDCT_ROW_SHIFT - 1))) >>
            IDCT_ROW_SHIFT;
        int32_t value =
            IDCT_W4 * (row_value + IDCT_COLUMN_ROUND) >>
            IDCT_COLUMN_SHIFT;
        for (index = 0; index < 64U; ++index) block[index] = value;
        return;
    }
    for (index = 0; index < 8U; ++index)
        if (row_mask & (1U << index))
            idct_row(block + index * 8U);
    for (index = 0; index < 8U; ++index) idct_column(block + index);
}

static int luma_dc_scaler(int quantizer) {
    if (quantizer <= 4) return 8;
    if (quantizer <= 8) return quantizer * 2;
    if (quantizer <= 24) return quantizer + 8;
    return quantizer * 2 - 16;
}

static int chroma_dc_scaler(int quantizer) {
    if (quantizer <= 4) return 8;
    if (quantizer <= 24) return (quantizer + 13) / 2;
    return quantizer - 6;
}

static int dequantize(int level, int quantizer) {
    int value;
    if (!level) return 0;
    value = quantizer * (2 * abs_int(level) + 1);
    if (!(quantizer & 1)) --value;
    return level < 0 ? -value : value;
}

static int predict_dc(const int16_t *grid, unsigned width,
                      unsigned rows, unsigned x, unsigned y, int default_value,
                      int *from_left) {
    unsigned row = y % rows;
    unsigned top_row = (y - 1U) % rows;
    int left = default_value;
    int top_left = default_value;
    int top = default_value;
    if (x) left = grid[row * width + x - 1U];
    if (x && y) top_left = grid[top_row * width + x - 1U];
    if (y) top = grid[top_row * width + x];
    *from_left = abs_int(left - top_left) > abs_int(top_left - top);
    return *from_left ? left : top;
}

static void block_target(Divx3Decoder *decoder, Divx3FrameBuffer *frame,
                         unsigned block, unsigned mb_x, unsigned mb_y,
                         uint8_t **destination, unsigned *stride) {
    if (block < 4) {
        *destination =
            frame->y + (mb_y * 16U + (block / 2U) * 8U) *
                           decoder->y_stride +
                       mb_x * 16U + (block & 1U) * 8U;
        *stride = decoder->y_stride;
    } else {
        uint8_t *plane = block == 5 ? frame->cr : frame->cb;
        *destination =
            plane + (mb_y * 8U) * decoder->c_stride + mb_x * 8U;
        *stride = decoder->c_stride;
    }
}

static void compact_block_target(
    Divx3Decoder *decoder, Divx3FrameBuffer *frame, unsigned block,
    unsigned mb_x, unsigned mb_y, uint8_t **destination,
    unsigned *stride, int8_t **correction, unsigned *correction_stride,
    unsigned *bits, unsigned *block_x, unsigned *block_y) {
    if (block < 4) {
        *block_x = mb_x * 16U + (block & 1U) * 8U;
        *block_y = mb_y * 16U + (block / 2U) * 8U;
        *stride = decoder->y_stride;
        *bits = COMPACT_YUV420_LUMA_BITS;
        *destination =
            frame->y + (size_t)*block_y * *stride +
            (size_t)*block_x * *bits / 8U;
        *correction = frame->correction_y;
        *correction_stride = decoder->correction_stride_y;
    } else {
        uint8_t *plane = block == 5 ? frame->cr : frame->cb;
        *block_x = mb_x * 8U;
        *block_y = mb_y * 8U;
        *stride = decoder->c_stride;
        *bits = COMPACT_YUV420_CHROMA_BITS;
        *destination =
            plane +
            (size_t)*block_y * *stride +
            (size_t)*block_x * *bits / 8U;
        *correction = block == 5 ? frame->correction_cr
                                 : frame->correction_cb;
        *correction_stride = decoder->correction_stride_c;
    }
}

static void write_residual_block(Divx3Decoder *decoder,
                                 Divx3FrameBuffer *frame,
                                 unsigned block, unsigned mb_x,
                                 unsigned mb_y, int32_t values[64],
                                 const uint8_t *prediction) {
    uint8_t *destination;
    unsigned stride, row, column;
    if (decoder->compact_y6_u5_v5) {
        int8_t *correction;
        unsigned correction_stride;
        unsigned bits;
        unsigned block_x;
        unsigned block_y;
        uint8_t samples[64];
        int residual_sum = 0;
        compact_block_target(
            decoder, frame, block, mb_x, mb_y, &destination, &stride,
            &correction, &correction_stride, &bits, &block_x, &block_y);
        for (row = 0; row < 8; ++row) {
            for (column = 0; column < 8; ++column) {
                int value = values[row * 8U + column];
                if (prediction)
                    value += prediction[row * 8U + column];
                samples[row * 8U + column] = clamp_byte(value);
            }
            compact_yuv420_pack_aligned_samples(
                destination + row * stride, samples + row * 8U, 8,
                bits, &residual_sum, NULL);
        }
        correction[(block_y / 8U) * correction_stride + block_x / 8U] =
            compact_yuv420_error_q4(residual_sum);
        return;
    }
    block_target(decoder, frame, block, mb_x, mb_y,
                 &destination, &stride);
    for (row = 0; row < 8; ++row) {
        for (column = 0; column < 8; ++column) {
            int value = values[row * 8 + column];
            if (prediction) value += prediction[row * 8 + column];
            destination[row * stride + column] = clamp_byte(value);
        }
    }
}

static void write_prediction_block(Divx3Decoder *decoder,
                                   Divx3FrameBuffer *frame,
                                   unsigned block, unsigned mb_x,
                                   unsigned mb_y,
                                   const uint8_t prediction[64]) {
    uint8_t *destination;
    unsigned stride;
    unsigned row;
    if (decoder->compact_y6_u5_v5) {
        int8_t *correction;
        unsigned correction_stride;
        unsigned bits;
        unsigned block_x;
        unsigned block_y;
        int residual_sum = 0;
        compact_block_target(
            decoder, frame, block, mb_x, mb_y, &destination, &stride,
            &correction, &correction_stride, &bits, &block_x, &block_y);
        for (row = 0; row < 8U; ++row)
            compact_yuv420_pack_aligned_samples(
                destination + row * stride, prediction + row * 8U, 8,
                bits, &residual_sum, NULL);
        correction[(block_y / 8U) * correction_stride + block_x / 8U] =
            compact_yuv420_error_q4(residual_sum);
        return;
    }
    block_target(decoder, frame, block, mb_x, mb_y,
                 &destination, &stride);
    for (row = 0; row < 8U; ++row)
        memcpy(destination + row * stride, prediction + row * 8U, 8U);
}

static int decode_coefficients(BitReader *reader,
                               const Divx3TcoefSet *set,
                               int run_difference,
                               const uint8_t *scan, int start,
                               int32_t coefficients[64]) {
    int position = start;
    unsigned count;
    for (count = 0; count < 64; ++count) {
        AcCoefficient coefficient;
        int result = decode_tcoef(reader, set, run_difference,
                                  &coefficient);
        if (result != DIVX3_OK) return result;
        position += coefficient.run;
        if (position < 0 || position >= 64)
            return DIVX3_ERR_BITSTREAM;
        coefficients[scan[position]] = coefficient.level;
        ++position;
        if (coefficient.last) return DIVX3_OK;
    }
    return DIVX3_ERR_BITSTREAM;
}

static int decode_intra_block(
    Divx3Decoder *decoder, BitReader *reader, Divx3FrameBuffer *frame,
    unsigned block, unsigned mb_x, unsigned mb_y, int quantizer,
    int coded, int ac_prediction, const Divx3TcoefSet *luma_set,
    const Divx3TcoefSet *chroma_set, const Divx3DcVlc *dc_luma,
    size_t dc_luma_count, const Divx3VlcNode *dc_luma_nodes,
    const Divx3DcVlc *dc_chroma, size_t dc_chroma_count,
    const Divx3VlcNode *dc_chroma_nodes) {
    int16_t *dc_grid;
    int16_t *ac_row;
    int16_t *ac_column;
    unsigned grid_width;
    unsigned grid_rows;
    unsigned gx, gy;
    int default_value;
    int dc_scale;
    int difference;
    int predictor;
    int from_left;
    int32_t quantized[64] = {0};
    int32_t coefficients[64];
    const uint8_t *scan = kScanZigzag;
    const Divx3TcoefSet *set = block < 4 ? luma_set : chroma_set;
    unsigned index;

    if (block < 4) {
        grid_width = decoder->mb_width * 2U;
        grid_rows = 3U;
        gx = mb_x * 2U + (block & 1U);
        gy = mb_y * 2U + block / 2U;
        dc_grid = decoder->dc_luma;
        ac_row = decoder->ac_luma_row;
        ac_column = decoder->ac_luma_col;
        dc_scale = luma_dc_scaler(quantizer);
        if (decode_dc(reader, dc_luma, dc_luma_count, dc_luma_nodes,
                      &difference) != DIVX3_OK)
            return DIVX3_ERR_BITSTREAM;
    } else {
        grid_width = decoder->mb_width;
        grid_rows = 2U;
        gx = mb_x;
        gy = mb_y;
        dc_grid = block == 4 ? decoder->dc_cb : decoder->dc_cr;
        ac_row = block == 4 ? decoder->ac_cb_row : decoder->ac_cr_row;
        ac_column =
            block == 4 ? decoder->ac_cb_col : decoder->ac_cr_col;
        dc_scale = chroma_dc_scaler(quantizer);
        if (decode_dc(reader, dc_chroma, dc_chroma_count,
                      dc_chroma_nodes,
                      &difference) != DIVX3_OK)
            return DIVX3_ERR_BITSTREAM;
    }
    default_value = (2048 + dc_scale) / (2 * dc_scale);
    predictor = predict_dc(dc_grid, grid_width, grid_rows, gx, gy,
                           default_value, &from_left);
    if (difference < INT16_MIN - predictor ||
        difference > INT16_MAX - predictor)
        return DIVX3_ERR_BITSTREAM;
    quantized[0] = predictor + difference;
    dc_grid[(gy % grid_rows) * grid_width + gx] =
        (int16_t)quantized[0];

    if (coded) {
        if (ac_prediction)
            scan = from_left ? kScanAltVertical : kScanAltHorizontal;
        if (decode_coefficients(reader, set, 0, scan, 1,
                                quantized) != DIVX3_OK)
            return DIVX3_ERR_BITSTREAM;
    }
    if (ac_prediction) {
        if (from_left && gx) {
            const int16_t *source =
                ac_column +
                ((((gy % grid_rows) * grid_width) + gx - 1U) * 8U);
            for (index = 1; index < 8; ++index)
                quantized[index * 8U] += source[index];
        } else if (!from_left && gy) {
            const int16_t *source =
                ac_row +
                (((((gy - 1U) % grid_rows) * grid_width) + gx) * 8U);
            for (index = 1; index < 8; ++index)
                quantized[index] += source[index];
        }
    }
    for (index = 1; index < 8; ++index) {
        int row_value = quantized[index];
        int column_value = quantized[index * 8U];
        if (row_value < INT16_MIN || row_value > INT16_MAX ||
            column_value < INT16_MIN || column_value > INT16_MAX)
            return DIVX3_ERR_BITSTREAM;
        ac_row[(((gy % grid_rows) * grid_width + gx) * 8U) + index] =
            (int16_t)row_value;
        ac_column[(((gy % grid_rows) * grid_width + gx) * 8U) + index] =
            (int16_t)column_value;
    }
    coefficients[0] = quantized[0] * dc_scale;
    for (index = 1; index < 64; ++index)
        coefficients[index] = dequantize(quantized[index], quantizer);
    inverse_dct(coefficients);
    write_residual_block(decoder, frame, block, mb_x, mb_y,
                         coefficients, NULL);
    return DIVX3_OK;
}

static void reset_prediction_grids(Divx3Decoder *decoder, int quantizer) {
    size_t luma_blocks = (size_t)decoder->mb_width * 6U;
    size_t chroma_blocks = (size_t)decoder->mb_width * 2U;
    int16_t luma_default =
        (int16_t)((2048 + luma_dc_scaler(quantizer)) /
                  (2 * luma_dc_scaler(quantizer)));
    int16_t chroma_default =
        (int16_t)((2048 + chroma_dc_scaler(quantizer)) /
                  (2 * chroma_dc_scaler(quantizer)));
    size_t index;
    for (index = 0; index < luma_blocks; ++index)
        decoder->dc_luma[index] = luma_default;
    for (index = 0; index < chroma_blocks; ++index) {
        decoder->dc_cb[index] = chroma_default;
        decoder->dc_cr[index] = chroma_default;
    }
    memset(decoder->ac_luma_row, 0,
           luma_blocks * 8U * sizeof(*decoder->ac_luma_row));
    memset(decoder->ac_luma_col, 0,
           luma_blocks * 8U * sizeof(*decoder->ac_luma_col));
    memset(decoder->ac_cb_row, 0,
           chroma_blocks * 8U * sizeof(*decoder->ac_cb_row));
    memset(decoder->ac_cb_col, 0,
           chroma_blocks * 8U * sizeof(*decoder->ac_cb_col));
    memset(decoder->ac_cr_row, 0,
           chroma_blocks * 8U * sizeof(*decoder->ac_cr_row));
    memset(decoder->ac_cr_col, 0,
           chroma_blocks * 8U * sizeof(*decoder->ac_cr_col));
    memset(decoder->coded_luma, 0, luma_blocks);
}

static void prepare_prediction_macroblock_row(Divx3Decoder *decoder,
                                              unsigned mb_y,
                                              int quantizer) {
    unsigned luma_width = decoder->mb_width * 2U;
    unsigned chroma_width = decoder->mb_width;
    int16_t luma_default =
        (int16_t)((2048 + luma_dc_scaler(quantizer)) /
                  (2 * luma_dc_scaler(quantizer)));
    int16_t chroma_default =
        (int16_t)((2048 + chroma_dc_scaler(quantizer)) /
                  (2 * chroma_dc_scaler(quantizer)));
    unsigned block_row;
    unsigned column;
    for (block_row = 0; block_row < 2U; ++block_row) {
        unsigned gy = mb_y * 2U + block_row;
        size_t index = (size_t)(gy % 3U) * luma_width;
        for (column = 0; column < luma_width; ++column)
            decoder->dc_luma[index + column] = luma_default;
        memset(decoder->ac_luma_row + index * 8U, 0,
               (size_t)luma_width * 8U *
                   sizeof(*decoder->ac_luma_row));
        memset(decoder->ac_luma_col + index * 8U, 0,
               (size_t)luma_width * 8U *
                   sizeof(*decoder->ac_luma_col));
        memset(decoder->coded_luma + index, 0, luma_width);
    }
    {
        size_t index = (size_t)(mb_y & 1U) * chroma_width;
        for (column = 0; column < chroma_width; ++column) {
            decoder->dc_cb[index + column] = chroma_default;
            decoder->dc_cr[index + column] = chroma_default;
        }
        memset(decoder->ac_cb_row + index * 8U, 0,
               (size_t)chroma_width * 8U *
                   sizeof(*decoder->ac_cb_row));
        memset(decoder->ac_cb_col + index * 8U, 0,
               (size_t)chroma_width * 8U *
                   sizeof(*decoder->ac_cb_col));
        memset(decoder->ac_cr_row + index * 8U, 0,
               (size_t)chroma_width * 8U *
                   sizeof(*decoder->ac_cr_row));
        memset(decoder->ac_cr_col + index * 8U, 0,
               (size_t)chroma_width * 8U *
                   sizeof(*decoder->ac_cr_col));
    }
}

static int decode_intra_picture(Divx3Decoder *decoder,
                                BitReader *reader,
                                Divx3FrameBuffer *frame,
                                int quantizer) {
    int chroma_index;
    int luma_index;
    int dc_index;
    const Divx3TcoefSet *luma_set;
    const Divx3TcoefSet *chroma_set;
    const Divx3DcVlc *dc_luma;
    const Divx3DcVlc *dc_chroma;
    const Divx3VlcNode *dc_luma_nodes;
    const Divx3VlcNode *dc_chroma_nodes;
    size_t dc_luma_count;
    size_t dc_chroma_count;
    unsigned mb_x, mb_y;

    bits_read(reader, 5);
    chroma_index = read_c3(reader);
    luma_index = read_c3(reader);
    dc_index = bit_read(reader);
    if (reader->failed) return DIVX3_ERR_BITSTREAM;
    luma_set = kLumaSets + luma_index;
    chroma_set = kChromaSets + chroma_index;
    if (dc_index) {
        dc_luma = kdcRaw1_0;
        dc_luma_count = sizeof(kdcRaw1_0) / sizeof(kdcRaw1_0[0]);
        dc_luma_nodes = kdcRaw1_0Nodes;
        dc_chroma = kdcRaw1_1;
        dc_chroma_count = sizeof(kdcRaw1_1) / sizeof(kdcRaw1_1[0]);
        dc_chroma_nodes = kdcRaw1_1Nodes;
    } else {
        dc_luma = kdcRaw0_0;
        dc_luma_count = sizeof(kdcRaw0_0) / sizeof(kdcRaw0_0[0]);
        dc_luma_nodes = kdcRaw0_0Nodes;
        dc_chroma = kdcRaw0_1;
        dc_chroma_count = sizeof(kdcRaw0_1) / sizeof(kdcRaw0_1[0]);
        dc_chroma_nodes = kdcRaw0_1Nodes;
    }
    reset_prediction_grids(decoder, quantizer);
    for (mb_y = 0; mb_y < decoder->mb_height; ++mb_y) {
        for (mb_x = 0; mb_x < decoder->mb_width; ++mb_x) {
            uint8_t cbp[6];
            int ac_prediction;
            unsigned block;
            if (decode_mcbpc(reader, cbp) != DIVX3_OK)
                return DIVX3_ERR_BITSTREAM;
            for (block = 0; block < 4; ++block) {
                unsigned bx = mb_x * 2U + (block & 1U);
                unsigned by = mb_y * 2U + block / 2U;
                unsigned grid_width = decoder->mb_width * 2U;
                unsigned row = by % 3U;
                unsigned top_row = (by - 1U) % 3U;
                int left = bx ? decoder->coded_luma[row * grid_width +
                                                     bx - 1U]
                              : 0;
                int top_left =
                    bx && by ? decoder->coded_luma[top_row * grid_width +
                                                   bx - 1U]
                             : 0;
                int top = by ? decoder->coded_luma[top_row * grid_width +
                                                   bx]
                             : 0;
                int prediction = top_left == top ? left : top;
                cbp[block] ^= (uint8_t)prediction;
                decoder->coded_luma[row * grid_width + bx] = cbp[block];
            }
            ac_prediction = bit_read(reader);
            for (block = 0; block < 6; ++block) {
                int result = decode_intra_block(
                    decoder, reader, frame, block, mb_x, mb_y,
                    quantizer, cbp[block], ac_prediction,
                    luma_set, chroma_set, dc_luma, dc_luma_count,
                    dc_luma_nodes, dc_chroma, dc_chroma_count,
                    dc_chroma_nodes);
                if (result != DIVX3_OK) return result;
            }
        }
    }
    return reader->failed ? DIVX3_ERR_BITSTREAM : DIVX3_OK;
}

static int median3(int a, int b, int c) {
    if (a > b) {
        int swap = a;
        a = b;
        b = swap;
    }
    if (b > c) b = c;
    if (a > b) b = a;
    return b;
}

static int half_pixel_integer(int value, int *fraction) {
    int remainder = value % 2;
    if (remainder < 0) remainder += 2;
    *fraction = remainder;
    return (value - remainder) / 2;
}

static int clamp_coordinate(int value, int extent) {
    if (value < 0) return 0;
    if (value >= extent) return extent - 1;
    return value;
}

static int motion_sample(const uint8_t *source, unsigned stride,
                         int x, int y, int compact, unsigned bits,
                         const int8_t *correction,
                         unsigned correction_stride) {
    const uint8_t *row = source + (size_t)y * stride;
    return compact
               ? compact_yuv420_corrected_sample(
                     row, x, y, bits, correction,
                     (int)correction_stride)
               : row[x];
}

static void motion_patch(uint8_t patch[81], unsigned patch_stride,
                         unsigned patch_width, unsigned patch_height,
                         const uint8_t *source, unsigned stride,
                         unsigned plane_width, unsigned plane_height,
                         int source_x, int source_y,
                         int compact, unsigned bits,
                         const int8_t *correction,
                         unsigned correction_stride) {
    unsigned row;
    int interior_x =
        source_x >= 0 &&
        source_x + (int)patch_width <= (int)plane_width;
    for (row = 0; row < patch_height; ++row) {
        int y = clamp_coordinate(
            source_y + (int)row, (int)plane_height);
        uint8_t *destination = patch + row * patch_stride;
        const uint8_t *source_row = source + (size_t)y * stride;
        if (interior_x) {
            if (compact) {
                compact_yuv420_unpack_corrected_samples(
                    source_row, source_x, y, bits, correction,
                    (int)correction_stride, destination,
                    (int)patch_width);
            } else {
                memcpy(destination, source_row + source_x,
                       patch_width);
            }
        } else {
            unsigned column;
            for (column = 0; column < patch_width; ++column) {
                int x = clamp_coordinate(
                    source_x + (int)column, (int)plane_width);
                destination[column] = (uint8_t)motion_sample(
                    source, stride, x, y, compact, bits,
                    correction, correction_stride);
            }
        }
    }
}

static void motion_compensate(uint8_t output[64],
                              const uint8_t *source, unsigned stride,
                              unsigned plane_width, unsigned plane_height,
                              int block_y, int block_x,
                              int motion_x, int motion_y,
                              int no_rounding, int compact, unsigned bits,
                              const int8_t *correction,
                              unsigned correction_stride) {
    int fractional_x, fractional_y;
    int integer_x = half_pixel_integer(motion_x, &fractional_x);
    int integer_y = half_pixel_integer(motion_y, &fractional_y);
    int round_two = no_rounding ? 0 : 1;
    int round_four = no_rounding ? 1 : 2;
    unsigned patch_width = 8U + (unsigned)fractional_x;
    unsigned patch_height = 8U + (unsigned)fractional_y;
    uint8_t patch[81];
    unsigned row, column;
    motion_patch(
        patch, patch_width, patch_width, patch_height,
        source, stride, plane_width, plane_height,
        block_x + integer_x, block_y + integer_y,
        compact, bits, correction, correction_stride);
    if (!fractional_x && !fractional_y) {
        for (row = 0; row < 8; ++row)
            for (column = 0; column < 8; ++column)
                output[row * 8U + column] =
                    patch[row * patch_width + column];
    } else if (fractional_x && !fractional_y) {
        for (row = 0; row < 8; ++row) {
            const uint8_t *patch_row = patch + row * patch_width;
            for (column = 0; column < 8; ++column)
                output[row * 8U + column] = (uint8_t)(
                    (patch_row[column] + patch_row[column + 1U] +
                     round_two) >>
                    1);
        }
    } else if (!fractional_x && fractional_y) {
        for (row = 0; row < 8; ++row) {
            const uint8_t *patch_row = patch + row * patch_width;
            const uint8_t *next_row = patch_row + patch_width;
            for (column = 0; column < 8; ++column)
                output[row * 8U + column] = (uint8_t)(
                    (patch_row[column] + next_row[column] +
                     round_two) >>
                    1);
        }
    } else {
        for (row = 0; row < 8; ++row) {
            const uint8_t *patch_row = patch + row * patch_width;
            const uint8_t *next_row = patch_row + patch_width;
            for (column = 0; column < 8; ++column)
                output[row * 8U + column] = (uint8_t)(
                    (patch_row[column] + patch_row[column + 1U] +
                     next_row[column] + next_row[column + 1U] +
                     round_four) >>
                    2);
        }
    }
}

static int chroma_motion(int value) {
    int shifted = value >= 0 ? value / 2 : -(((-value) + 1) / 2);
    return shifted | (value & 1);
}

static int decode_inter_block(BitReader *reader,
                              const Divx3TcoefSet *set,
                              int quantizer, int32_t coefficients[64]) {
    int position = 0;
    unsigned count;
    unsigned row_mask = 0;
    int has_ac = 0;
    memset(coefficients, 0, 64U * sizeof(*coefficients));
    for (count = 0; count < 64U; ++count) {
        AcCoefficient coefficient;
        unsigned index;
        int result = decode_tcoef(reader, set, 1, &coefficient);
        if (result != DIVX3_OK) return result;
        position += coefficient.run;
        if (position < 0 || position >= 64)
            return DIVX3_ERR_BITSTREAM;
        index = kScanZigzag[position];
        coefficients[index] =
            dequantize(coefficient.level, quantizer);
        row_mask |= 1U << (index / 8U);
        has_ac |= index != 0U;
        ++position;
        if (coefficient.last) {
            inverse_dct_sparse(coefficients, row_mask, has_ac);
            return DIVX3_OK;
        }
    }
    return DIVX3_ERR_BITSTREAM;
}

static int decode_inter_picture(Divx3Decoder *decoder,
                                BitReader *reader,
                                Divx3FrameBuffer *frame,
                                const Divx3FrameBuffer *reference,
                                int quantizer, int no_rounding) {
    int use_skip = bit_read(reader);
    int table_index = read_c3(reader);
    int dc_index = bit_read(reader);
    int motion_table = bit_read(reader);
    const Divx3TcoefSet *chroma_set = kChromaSets + table_index;
    const Divx3TcoefSet *luma_set = kLumaSets + table_index;
    const Divx3DcVlc *dc_luma;
    const Divx3DcVlc *dc_chroma;
    const Divx3VlcNode *dc_luma_nodes;
    const Divx3VlcNode *dc_chroma_nodes;
    size_t dc_luma_count;
    size_t dc_chroma_count;
    unsigned mb_x, mb_y;

    if (reader->failed) return DIVX3_ERR_BITSTREAM;
    if (dc_index) {
        dc_luma = kdcRaw1_0;
        dc_luma_count = sizeof(kdcRaw1_0) / sizeof(kdcRaw1_0[0]);
        dc_luma_nodes = kdcRaw1_0Nodes;
        dc_chroma = kdcRaw1_1;
        dc_chroma_count = sizeof(kdcRaw1_1) / sizeof(kdcRaw1_1[0]);
        dc_chroma_nodes = kdcRaw1_1Nodes;
    } else {
        dc_luma = kdcRaw0_0;
        dc_luma_count = sizeof(kdcRaw0_0) / sizeof(kdcRaw0_0[0]);
        dc_luma_nodes = kdcRaw0_0Nodes;
        dc_chroma = kdcRaw0_1;
        dc_chroma_count = sizeof(kdcRaw0_1) / sizeof(kdcRaw0_1[0]);
        dc_chroma_nodes = kdcRaw0_1Nodes;
    }
    memcpy(frame->y, reference->y, decoder->y_bytes);
    memcpy(frame->cb, reference->cb, decoder->c_bytes);
    memcpy(frame->cr, reference->cr, decoder->c_bytes);
    if (decoder->compact_y6_u5_v5) {
        memcpy(frame->correction_y, reference->correction_y,
               decoder->y_correction_bytes);
        memcpy(frame->correction_cb, reference->correction_cb,
               decoder->c_correction_bytes);
        memcpy(frame->correction_cr, reference->correction_cr,
               decoder->c_correction_bytes);
    }
    memset(decoder->mv_x, 0, (size_t)decoder->mb_width * 2U);
    memset(decoder->mv_y, 0, (size_t)decoder->mb_width * 2U);

    for (mb_y = 0; mb_y < decoder->mb_height; ++mb_y) {
        prepare_prediction_macroblock_row(decoder, mb_y, quantizer);
        for (mb_x = 0; mb_x < decoder->mb_width; ++mb_x) {
            size_t mb_index =
                (size_t)(mb_y & 1U) * decoder->mb_width + mb_x;
            size_t top_index =
                (size_t)((mb_y - 1U) & 1U) * decoder->mb_width + mb_x;
            int intra;
            int cbp;
            unsigned block;
            if (use_skip && bit_read(reader)) {
                decoder->mv_x[mb_index] = 0;
                decoder->mv_y[mb_index] = 0;
                continue;
            }
            if (decode_mb(reader, &intra, &cbp) != DIVX3_OK)
                return DIVX3_ERR_BITSTREAM;
            if (intra) {
                int ac_prediction = bit_read(reader);
                decoder->mv_x[mb_index] = 0;
                decoder->mv_y[mb_index] = 0;
                for (block = 0; block < 6; ++block) {
                    int result = decode_intra_block(
                        decoder, reader, frame, block, mb_x, mb_y,
                        quantizer, (cbp >> (5U - block)) & 1U,
                        ac_prediction, luma_set, chroma_set,
                        dc_luma, dc_luma_count, dc_luma_nodes,
                        dc_chroma, dc_chroma_count,
                        dc_chroma_nodes);
                    if (result != DIVX3_OK) return result;
                }
            } else {
                int delta_x, delta_y;
                int left_x = mb_x ? decoder->mv_x[mb_index - 1U] : 0;
                int left_y = mb_x ? decoder->mv_y[mb_index - 1U] : 0;
                int top_x =
                    mb_y ? decoder->mv_x[top_index] : 0;
                int top_y =
                    mb_y ? decoder->mv_y[top_index] : 0;
                int right_x =
                    mb_y && mb_x + 1U < decoder->mb_width
                        ? decoder->mv_x[top_index + 1U]
                        : 0;
                int right_y =
                    mb_y && mb_x + 1U < decoder->mb_width
                        ? decoder->mv_y[top_index + 1U]
                        : 0;
                int motion_x;
                int motion_y;
                if (decode_motion(reader, motion_table,
                                  &delta_x, &delta_y) != DIVX3_OK)
                    return DIVX3_ERR_BITSTREAM;
                if (!mb_y) {
                    motion_x = left_x + delta_x;
                    motion_y = left_y + delta_y;
                } else {
                    motion_x =
                        median3(left_x, top_x, right_x) + delta_x;
                    motion_y =
                        median3(left_y, top_y, right_y) + delta_y;
                }
                if (motion_x <= -64)
                    motion_x += 64;
                else if (motion_x >= 64)
                    motion_x -= 64;
                if (motion_y <= -64)
                    motion_y += 64;
                else if (motion_y >= 64)
                    motion_y -= 64;
                decoder->mv_x[mb_index] = (int8_t)motion_x;
                decoder->mv_y[mb_index] = (int8_t)motion_y;

                for (block = 0; block < 6; ++block) {
                    const uint8_t *source;
                    unsigned stride;
                    unsigned plane_width;
                    unsigned plane_height;
                    int block_x;
                    int block_y;
                    int block_motion_x;
                    int block_motion_y;
                    const int8_t *correction = NULL;
                    unsigned correction_stride = 0;
                    unsigned bits = 8;
                    uint8_t prediction[64];
                    if (block < 4) {
                        source = reference->y;
                        stride = decoder->y_stride;
                        plane_width = decoder->width;
                        plane_height = decoder->height;
                        block_x = (int)(mb_x * 16U +
                                        (block & 1U) * 8U);
                        block_y = (int)(mb_y * 16U +
                                        (block / 2U) * 8U);
                        block_motion_x = motion_x;
                        block_motion_y = motion_y;
                        if (decoder->compact_y6_u5_v5) {
                            correction = reference->correction_y;
                            correction_stride =
                                decoder->correction_stride_y;
                            bits = COMPACT_YUV420_LUMA_BITS;
                        }
                    } else {
                        source = block == 5 ? reference->cr
                                            : reference->cb;
                        stride = decoder->c_stride;
                        plane_width = (decoder->width + 1U) / 2U;
                        plane_height = (decoder->height + 1U) / 2U;
                        block_x = (int)(mb_x * 8U);
                        block_y = (int)(mb_y * 8U);
                        block_motion_x = chroma_motion(motion_x);
                        block_motion_y = chroma_motion(motion_y);
                        if (decoder->compact_y6_u5_v5) {
                            correction =
                                block == 5
                                    ? reference->correction_cr
                                    : reference->correction_cb;
                            correction_stride =
                                decoder->correction_stride_c;
                            bits = COMPACT_YUV420_CHROMA_BITS;
                        }
                    }
                    motion_compensate(prediction, source, stride,
                                      plane_width, plane_height,
                                      block_y, block_x,
                                      block_motion_x, block_motion_y,
                                      no_rounding,
                                      decoder->compact_y6_u5_v5,
                                      bits, correction,
                                      correction_stride);
                    if ((cbp >> (5U - block)) & 1U) {
                        int32_t residual[64];
                        int result = decode_inter_block(
                            reader, chroma_set, quantizer, residual);
                        if (result != DIVX3_OK) return result;
                        write_residual_block(
                            decoder, frame, block, mb_x, mb_y,
                            residual, prediction);
                    } else {
                        write_prediction_block(
                            decoder, frame, block, mb_x, mb_y,
                            prediction);
                    }
                }
            }
        }
    }
    return reader->failed ? DIVX3_ERR_BITSTREAM : DIVX3_OK;
}

static int allocate_decoder_buffers(Divx3Decoder *decoder) {
    size_t macroblock_rows = (size_t)decoder->mb_width * 2U;
    size_t luma_blocks = (size_t)decoder->mb_width * 6U;
    size_t chroma_blocks = macroblock_rows;
    size_t ac_luma_values = luma_blocks * 8U;
    size_t ac_chroma_values = chroma_blocks * 8U;

    /*
     * Allocate predictive frames by plane. The decoder retains exactly the
     * same samples, but the largest contiguous request is one luma plane
     * instead of a complete packed frame. This matters on ESP32 after an
     * audio FILE has expanded libc's persistent stdio pool and split DRAM
     * into otherwise sufficiently large free regions.
     */
    /*
     * Reserve both largest planes first. Smaller chroma and correction
     * allocations must not consume the only remaining luma-sized region.
     */
    for (unsigned index = 0; index < 2U; ++index) {
        decoder->frames[index].y = (uint8_t *)malloc(decoder->y_bytes);
    }
    for (unsigned index = 0; index < 2U; ++index) {
        decoder->frames[index].cb = (uint8_t *)malloc(decoder->c_bytes);
        decoder->frames[index].cr = (uint8_t *)malloc(decoder->c_bytes);
        if (decoder->compact_y6_u5_v5) {
            decoder->frames[index].correction_y =
                (int8_t *)malloc(decoder->y_correction_bytes);
            decoder->frames[index].correction_cb =
                (int8_t *)malloc(decoder->c_correction_bytes);
            decoder->frames[index].correction_cr =
                (int8_t *)malloc(decoder->c_correction_bytes);
        }
    }
    decoder->dc_luma =
        (int16_t *)malloc(luma_blocks * sizeof(int16_t));
    decoder->dc_cb =
        (int16_t *)malloc(chroma_blocks * sizeof(int16_t));
    decoder->dc_cr =
        (int16_t *)malloc(chroma_blocks * sizeof(int16_t));
    decoder->ac_luma_row =
        (int16_t *)malloc(ac_luma_values * sizeof(int16_t));
    decoder->ac_luma_col =
        (int16_t *)malloc(ac_luma_values * sizeof(int16_t));
    decoder->ac_cb_row =
        (int16_t *)malloc(ac_chroma_values * sizeof(int16_t));
    decoder->ac_cb_col =
        (int16_t *)malloc(ac_chroma_values * sizeof(int16_t));
    decoder->ac_cr_row =
        (int16_t *)malloc(ac_chroma_values * sizeof(int16_t));
    decoder->ac_cr_col =
        (int16_t *)malloc(ac_chroma_values * sizeof(int16_t));
    decoder->coded_luma = (uint8_t *)malloc(luma_blocks);
    decoder->mv_x = (int8_t *)malloc(macroblock_rows);
    decoder->mv_y = (int8_t *)malloc(macroblock_rows);
    if (!decoder->frames[0].y || !decoder->frames[0].cb ||
        !decoder->frames[0].cr || !decoder->frames[1].y ||
        !decoder->frames[1].cb || !decoder->frames[1].cr ||
        (decoder->compact_y6_u5_v5 &&
         (!decoder->frames[0].correction_y ||
          !decoder->frames[0].correction_cb ||
          !decoder->frames[0].correction_cr ||
          !decoder->frames[1].correction_y ||
          !decoder->frames[1].correction_cb ||
          !decoder->frames[1].correction_cr)) ||
        !decoder->dc_luma ||
        !decoder->dc_cb || !decoder->dc_cr ||
        !decoder->ac_luma_row || !decoder->ac_luma_col ||
        !decoder->ac_cb_row || !decoder->ac_cb_col ||
        !decoder->ac_cr_row || !decoder->ac_cr_col ||
        !decoder->coded_luma || !decoder->mv_x || !decoder->mv_y)
        return DIVX3_ERR_MEMORY;
    decoder->memory_bytes =
        sizeof(*decoder) + decoder->frame_bytes * 2U +
        (luma_blocks + chroma_blocks * 2U) * sizeof(int16_t) +
        (ac_luma_values * 2U + ac_chroma_values * 4U) *
            sizeof(int16_t) +
        luma_blocks + macroblock_rows * 2U;
    return DIVX3_OK;
}

static Divx3Decoder *divx3_decoder_create_internal(
    uint16_t width, uint16_t height, int compact_y6_u5_v5) {
    Divx3Decoder *decoder;
    uint32_t padded_width;
    uint32_t padded_height;
    size_t y_stride;
    size_t c_stride;
    size_t y_bytes;
    size_t c_bytes;
    size_t y_correction_bytes = 0;
    size_t c_correction_bytes = 0;
    size_t frame_bytes;
    if (!width || !height) return NULL;
    padded_width = ((uint32_t)width + 15U) & ~15U;
    padded_height = ((uint32_t)height + 15U) & ~15U;
    if (padded_width > UINT16_MAX || padded_height > UINT16_MAX)
        return NULL;
    if (compact_y6_u5_v5) {
        y_stride = compact_yuv420_packed_stride(
            (int)padded_width, COMPACT_YUV420_LUMA_BITS);
        c_stride = compact_yuv420_packed_stride(
            (int)(padded_width / 2U),
            COMPACT_YUV420_CHROMA_BITS);
        y_bytes = y_stride * padded_height;
        c_bytes = c_stride * (padded_height / 2U);
        y_correction_bytes =
            compact_yuv420_plane_correction_bytes(
                (int)padded_width, (int)padded_height);
        c_correction_bytes =
            compact_yuv420_plane_correction_bytes(
                (int)(padded_width / 2U),
                (int)(padded_height / 2U));
    } else {
        y_stride = padded_width;
        c_stride = padded_width / 2U;
        y_bytes = y_stride * padded_height;
        c_bytes = c_stride * (padded_height / 2U);
    }
    if (c_bytes > (SIZE_MAX - y_bytes) / 2U)
        return NULL;
    frame_bytes = y_bytes + c_bytes * 2U;
    if (y_correction_bytes > SIZE_MAX - frame_bytes)
        return NULL;
    frame_bytes += y_correction_bytes;
    if (c_correction_bytes > (SIZE_MAX - frame_bytes) / 2U)
        return NULL;
    frame_bytes += c_correction_bytes * 2U;
    if (frame_bytes > SIZE_MAX / 2U ||
        y_stride > UINT16_MAX || c_stride > UINT16_MAX)
        return NULL;
    decoder = (Divx3Decoder *)calloc(1, sizeof(*decoder));
    if (!decoder) return NULL;
    decoder->width = width;
    decoder->height = height;
    decoder->padded_width = (uint16_t)padded_width;
    decoder->padded_height = (uint16_t)padded_height;
    decoder->chroma_width = (uint16_t)(padded_width / 2U);
    decoder->chroma_height = (uint16_t)(padded_height / 2U);
    decoder->mb_width = (uint16_t)(padded_width / 16U);
    decoder->mb_height = (uint16_t)(padded_height / 16U);
    decoder->y_stride = (uint16_t)y_stride;
    decoder->c_stride = (uint16_t)c_stride;
    decoder->correction_stride_y =
        compact_y6_u5_v5 ? (uint16_t)(padded_width / 8U) : 0;
    decoder->correction_stride_c =
        compact_y6_u5_v5 ? (uint16_t)(padded_width / 16U) : 0;
    decoder->y_bytes = y_bytes;
    decoder->c_bytes = c_bytes;
    decoder->y_correction_bytes = y_correction_bytes;
    decoder->c_correction_bytes = c_correction_bytes;
    decoder->frame_bytes = frame_bytes;
    decoder->compact_y6_u5_v5 = compact_y6_u5_v5 != 0;
    if (allocate_decoder_buffers(decoder) != DIVX3_OK) {
        divx3_decoder_destroy(decoder);
        return NULL;
    }
    return decoder;
}

Divx3Decoder *divx3_decoder_create(uint16_t width, uint16_t height) {
    return divx3_decoder_create_internal(width, height, 0);
}

Divx3Decoder *divx3_decoder_create_y6_u5_v5(
    uint16_t width, uint16_t height) {
    return divx3_decoder_create_internal(width, height, 1);
}

void divx3_decoder_destroy(Divx3Decoder *decoder) {
    if (!decoder) return;
    for (unsigned index = 0; index < 2U; ++index) {
        free(decoder->frames[index].y);
        free(decoder->frames[index].cb);
        free(decoder->frames[index].cr);
        free(decoder->frames[index].correction_y);
        free(decoder->frames[index].correction_cb);
        free(decoder->frames[index].correction_cr);
    }
    free(decoder->dc_luma);
    free(decoder->dc_cb);
    free(decoder->dc_cr);
    free(decoder->ac_luma_row);
    free(decoder->ac_luma_col);
    free(decoder->ac_cb_row);
    free(decoder->ac_cb_col);
    free(decoder->ac_cr_row);
    free(decoder->ac_cr_col);
    free(decoder->coded_luma);
    free(decoder->mv_x);
    free(decoder->mv_y);
    free(decoder);
}

static int decode_with_reader(
    Divx3Decoder *decoder, BitReader reader, Divx3Frame *frame) {
    unsigned picture_type;
    int quantizer;
    uint8_t output_index;
    Divx3FrameBuffer *output;
    int next_no_rounding;
    int result;
    if (!decoder || !frame)
        return DIVX3_ERR_ARGUMENT;
    picture_type = bits_read(&reader, 2);
    quantizer = (int)bits_read(&reader, 5);
    if (reader.failed || !quantizer) return DIVX3_ERR_BITSTREAM;
    if (picture_type > 1) return DIVX3_ERR_UNSUPPORTED;
    if (picture_type == 1 && !decoder->has_reference)
        return DIVX3_ERR_BITSTREAM;
    output_index =
        decoder->has_reference ? (uint8_t)(decoder->reference_index ^ 1U)
                               : 0;
    output = &decoder->frames[output_index];
    next_no_rounding =
        decoder->flipflop_rounding
            ? (decoder->no_rounding ^ 1)
            : 0;
    result = picture_type == 0
                 ? decode_intra_picture(
                       decoder, &reader, output, quantizer)
                 : decode_inter_picture(
                       decoder, &reader, output,
                       &decoder->frames[decoder->reference_index],
                       quantizer, next_no_rounding);
    if (result != DIVX3_OK) return result;
    if (picture_type == 0) {
        size_t remaining =
            reader.position <= reader.bits
                ? reader.bits - reader.position
                : 0;
        decoder->no_rounding = 1;
        decoder->flipflop_rounding = 0;
        if (remaining >= 17 && remaining < 25) {
            bits_read(&reader, 5);
            bits_read(&reader, 11);
            decoder->flipflop_rounding =
                (uint8_t)bit_read(&reader);
        }
    } else {
        decoder->no_rounding = (uint8_t)next_no_rounding;
    }
    decoder->reference_index = output_index;
    decoder->has_reference = 1;
    frame->y = output->y;
    frame->cb = output->cb;
    frame->cr = output->cr;
    frame->correction_y =
        decoder->compact_y6_u5_v5
            ? output->correction_y
            : NULL;
    frame->correction_cb =
        frame->correction_y
            ? output->correction_cb
            : NULL;
    frame->correction_cr =
        frame->correction_cb
            ? output->correction_cr
            : NULL;
    frame->width = decoder->width;
    frame->height = decoder->height;
    frame->y_stride = decoder->y_stride;
    frame->c_stride = decoder->c_stride;
    frame->correction_stride_y = decoder->correction_stride_y;
    frame->correction_stride_c = decoder->correction_stride_c;
    frame->frame_number = decoder->frame_number++;
    frame->storage_mode = decoder->compact_y6_u5_v5
                              ? DIVX3_FRAME_STORAGE_Y6_U5_V5
                              : DIVX3_FRAME_STORAGE_YUV420;
    frame->intra = picture_type == 0;
    return DIVX3_OK;
}

int divx3_decoder_decode(Divx3Decoder *decoder, const uint8_t *packet,
                         size_t packet_size, Divx3Frame *frame) {
    BitReader reader = {0};
    if (!decoder || !packet || !packet_size || !frame)
        return DIVX3_ERR_ARGUMENT;
    reader.next = packet;
    reader.end = packet + packet_size;
    reader.bits = packet_size > SIZE_MAX / 8U ? SIZE_MAX
                                              : packet_size * 8U;
    return decode_with_reader(decoder, reader, frame);
}

int divx3_decoder_decode_stream(
    Divx3Decoder *decoder, size_t packet_size,
    Divx3ReadFunction read, void *read_context, Divx3Frame *frame) {
    BitReader reader = {0};
    if (!decoder || !packet_size || !read || !frame)
        return DIVX3_ERR_ARGUMENT;
    reader.next = decoder->stream_buffer;
    reader.end = decoder->stream_buffer;
    reader.refill = decoder->stream_buffer;
    reader.refill_capacity = sizeof(decoder->stream_buffer);
    reader.unread_bytes = packet_size;
    reader.read = read;
    reader.read_context = read_context;
    reader.bits = packet_size > SIZE_MAX / 8U ? SIZE_MAX
                                              : packet_size * 8U;
    return decode_with_reader(decoder, reader, frame);
}

int divx3_packet_probe_intra(const uint8_t *prefix, size_t prefix_size,
                             int *intra) {
    unsigned picture_type;
    unsigned quantizer;
    if (!prefix || !prefix_size || !intra)
        return DIVX3_ERR_ARGUMENT;
    picture_type = prefix[0] >> 6;
    quantizer = (prefix[0] >> 1) & 31U;
    if (!quantizer) return DIVX3_ERR_BITSTREAM;
    if (picture_type > 1U) return DIVX3_ERR_UNSUPPORTED;
    *intra = picture_type == 0U;
    return DIVX3_OK;
}

size_t divx3_decoder_memory_bytes(const Divx3Decoder *decoder) {
    return decoder ? decoder->memory_bytes : 0;
}

const char *divx3_strerror(int result) {
    switch (result) {
        case DIVX3_OK: return "success";
        case DIVX3_ERR_ARGUMENT: return "invalid argument";
        case DIVX3_ERR_MEMORY: return "not enough memory";
        case DIVX3_ERR_BITSTREAM: return "invalid DivX 3 bitstream";
        case DIVX3_ERR_UNSUPPORTED: return "unsupported DivX 3 picture mode";
        default: return "unknown DivX 3 error";
    }
}
