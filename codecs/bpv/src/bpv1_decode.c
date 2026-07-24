#include "bpv1.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    MODE_SKIP = 0,
    MODE_MOTION = 1,
    MODE_BLOCK_DICTIONARY = 2,
    MODE_PATTERN_DICTIONARY = 3,
    MODE_RAW = 4,
    MODE_COUNT = 5,
    PATTERN_OFFSET = 5
};

typedef struct {
    uint8_t *entries;
    uint32_t capacity;
    uint32_t count;
    uint32_t start;
    size_t stride;
} Dictionary;

struct BPV1Decoder {
    BPV1Header header;
    BPV1Frame frame;
    uint8_t *previous;
    uint8_t *current;
    uint8_t *packet_data;
    size_t block_bytes;
    size_t packet_capacity;
    size_t memory_bytes;
    uint32_t frame_index;
    int has_previous;
    Dictionary blocks;
    Dictionary patterns;
};

static int read_exact(FILE *file, void *data, size_t size) {
    return fread(data, 1, size, file) == size ? BPV1_OK : BPV1_ERR_IO;
}

static uint16_t read_u16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static int multiply_size(size_t left, size_t right, size_t *result) {
    if (left && right > SIZE_MAX / left) return BPV1_ERR_RANGE;
    *result = left * right;
    return BPV1_OK;
}

static int header_layout(const BPV1Header *header, uint32_t *blocks_x,
                         uint32_t *blocks_y, uint32_t *block_count,
                         size_t *block_bytes, size_t *mode_bytes,
                         size_t *packet_capacity) {
    uint64_t count;
    size_t bytes;
    if (!header || !header->width || !header->height ||
        !header->fps_num || !header->fps_den || !header->frame_count ||
        !header->keyframe_interval || !header->max_block_dictionary ||
        !header->max_pattern_dictionary ||
        (header->version != BPV1_VERSION &&
         header->version != BPV1_LEGACY_VERSION) ||
        (header->version == BPV1_VERSION &&
         header->palette_count != BPV1_PALETTE_COUNT) ||
        (header->version == BPV1_LEGACY_VERSION &&
         header->palette_count != 16)) {
        return BPV1_ERR_FORMAT;
    }
    *blocks_x =
        ((uint32_t)header->width + BPV1_BLOCK_SIZE - 1U) / BPV1_BLOCK_SIZE;
    *blocks_y =
        ((uint32_t)header->height + BPV1_BLOCK_SIZE - 1U) / BPV1_BLOCK_SIZE;
    count = (uint64_t)*blocks_x * *blocks_y;
    if (!count || count > UINT32_MAX) return BPV1_ERR_RANGE;
    *block_count = (uint32_t)count;
    if (multiply_size((size_t)*block_count, BPV1_RECORD_BYTES, &bytes))
        return BPV1_ERR_RANGE;
    *block_bytes = bytes;
    if (count > (uint64_t)(SIZE_MAX - 7U) / 3U) return BPV1_ERR_RANGE;
    *mode_bytes = (size_t)(count * 3U + 7U) / 8U;
    if (*mode_bytes > SIZE_MAX - *block_bytes) return BPV1_ERR_RANGE;
    *packet_capacity = *mode_bytes + *block_bytes;
    return BPV1_OK;
}

static uint8_t *dictionary_entry(const Dictionary *dictionary,
                                 uint32_t index) {
    uint32_t physical;
    if (!dictionary || index >= dictionary->count) return NULL;
    physical = (dictionary->start + index) % dictionary->capacity;
    return dictionary->entries + (size_t)physical * dictionary->stride;
}

static void dictionary_reset(Dictionary *dictionary) {
    dictionary->count = 0;
    dictionary->start = 0;
}

static void dictionary_add_unique(Dictionary *dictionary,
                                  const uint8_t *value) {
    uint32_t index;
    uint32_t physical;
    for (index = 0; index < dictionary->count; ++index) {
        if (!memcmp(dictionary_entry(dictionary, index), value,
                    dictionary->stride)) {
            return;
        }
    }
    if (dictionary->count < dictionary->capacity) {
        physical =
            (dictionary->start + dictionary->count) % dictionary->capacity;
        dictionary->count++;
    } else {
        dictionary->start =
            (dictionary->start + 1U) % dictionary->capacity;
        physical = (dictionary->start + dictionary->count - 1U) %
                   dictionary->capacity;
    }
    memcpy(dictionary->entries + (size_t)physical * dictionary->stride,
           value, dictionary->stride);
}

static unsigned read_mode(const uint8_t *modes, uint32_t block_index) {
    const uint32_t bit_offset = block_index * 3U;
    unsigned value = 0;
    unsigned bit;
    for (bit = 0; bit < 3; ++bit) {
        const uint32_t target = bit_offset + bit;
        value = (value << 1) |
                ((modes[target >> 3] >> (7U - (target & 7U))) & 1U);
    }
    return value;
}

static int validate_record(const BPV1Header *header,
                           const uint8_t record[BPV1_RECORD_BYTES]) {
    unsigned index;
    if (record[0] >= header->palette_count) return BPV1_ERR_DECODE;
    for (index = 1; index <= 4; ++index) {
        if (record[index] >= BPV1_COLORS_PER_PALETTE)
            return BPV1_ERR_DECODE;
    }
    return BPV1_OK;
}

static void reset_references(BPV1Decoder *decoder) {
    decoder->has_previous = 0;
    dictionary_reset(&decoder->blocks);
    dictionary_reset(&decoder->patterns);
}

const char *bpv1_strerror(int result) {
    switch (result) {
    case BPV1_OK: return "success";
    case BPV1_EOF: return "end of file";
    case BPV1_ERR_ARGUMENT: return "invalid argument";
    case BPV1_ERR_MEMORY: return "not enough memory";
    case BPV1_ERR_IO: return "I/O error";
    case BPV1_ERR_FORMAT: return "invalid or unsupported BPV1 format";
    case BPV1_ERR_RANGE: return "BPV1 value is out of range";
    case BPV1_ERR_DECODE: return "invalid BPV1 frame data";
    default: return "unknown BPV1 error";
    }
}

int bpv1_header_read(FILE *file, BPV1Header *header) {
    uint8_t fixed[25];
    size_t palette_bytes;
    uint32_t blocks_x, blocks_y, block_count;
    size_t block_bytes, mode_bytes, packet_capacity;
    if (!file || !header) return BPV1_ERR_ARGUMENT;
    memset(header, 0, sizeof *header);
    if (read_exact(file, fixed, sizeof fixed)) return BPV1_ERR_IO;
    if (memcmp(fixed, "BPV1", 4)) return BPV1_ERR_FORMAT;
    header->version = fixed[4];
    header->width = read_u16(fixed + 5);
    header->height = read_u16(fixed + 7);
    header->frame_count = read_u32(fixed + 9);
    header->fps_num = read_u16(fixed + 13);
    header->fps_den = read_u16(fixed + 15);
    header->keyframe_interval = read_u16(fixed + 17);
    header->max_block_dictionary = read_u16(fixed + 19);
    header->max_pattern_dictionary = read_u16(fixed + 21);
    header->search_radius = fixed[23];
    header->palette_count =
        header->version == BPV1_VERSION ? BPV1_PALETTE_COUNT :
        header->version == BPV1_LEGACY_VERSION ? 16 : 0;
    if (fixed[24] != 0 ||
        header_layout(header, &blocks_x, &blocks_y, &block_count,
                      &block_bytes, &mode_bytes, &packet_capacity)) {
        return BPV1_ERR_FORMAT;
    }
    palette_bytes =
        (size_t)header->palette_count * BPV1_COLORS_PER_PALETTE * 3U;
    return read_exact(file, header->palette, palette_bytes);
}

int bpv1_frame_info_read(FILE *file, const BPV1Header *header,
                         BPV1FrameInfo *info) {
    uint8_t bytes[9];
    uint32_t blocks_x, blocks_y, block_count;
    size_t block_bytes, mode_bytes, packet_capacity;
    size_t count;
    if (!file || !header || !info) return BPV1_ERR_ARGUMENT;
    count = fread(bytes, 1, sizeof bytes, file);
    if (!count && feof(file)) return BPV1_EOF;
    if (count != sizeof bytes) return BPV1_ERR_IO;
    if (header_layout(header, &blocks_x, &blocks_y, &block_count,
                      &block_bytes, &mode_bytes, &packet_capacity)) {
        return BPV1_ERR_FORMAT;
    }
    info->keyframe = bytes[0];
    info->frame_bytes = read_u32(bytes + 1);
    info->mode_bytes = read_u32(bytes + 5);
    if (info->keyframe > 1U || info->mode_bytes != mode_bytes ||
        info->frame_bytes < info->mode_bytes ||
        info->frame_bytes > packet_capacity) {
        return BPV1_ERR_FORMAT;
    }
    return BPV1_OK;
}

BPV1Decoder *bpv1_decoder_create(const BPV1Header *header) {
    BPV1Decoder *decoder = NULL;
    uint32_t blocks_x, blocks_y, block_count;
    size_t block_bytes, mode_bytes, packet_capacity;
    size_t block_dictionary_bytes;
    size_t pattern_dictionary_bytes;
    if (!header) return NULL;
    if (header_layout(header, &blocks_x, &blocks_y, &block_count,
                      &block_bytes, &mode_bytes, &packet_capacity)) {
        return NULL;
    }
    if (multiply_size(header->max_block_dictionary, BPV1_RECORD_BYTES,
                      &block_dictionary_bytes) ||
        multiply_size(header->max_pattern_dictionary, BPV1_PATTERN_BYTES,
                      &pattern_dictionary_bytes)) {
        return NULL;
    }
    decoder = (BPV1Decoder *)calloc(1, sizeof *decoder);
    if (!decoder) return NULL;
    decoder->previous = (uint8_t *)malloc(block_bytes);
    decoder->current = (uint8_t *)malloc(block_bytes);
    decoder->packet_data = (uint8_t *)malloc(packet_capacity);
    decoder->blocks.entries = (uint8_t *)malloc(block_dictionary_bytes);
    decoder->patterns.entries = (uint8_t *)malloc(pattern_dictionary_bytes);
    if (!decoder->previous || !decoder->current || !decoder->packet_data ||
        !decoder->blocks.entries || !decoder->patterns.entries) {
        bpv1_decoder_destroy(decoder);
        return NULL;
    }
    decoder->header = *header;
    decoder->block_bytes = block_bytes;
    decoder->packet_capacity = packet_capacity;
    decoder->blocks.capacity = header->max_block_dictionary;
    decoder->blocks.stride = BPV1_RECORD_BYTES;
    decoder->patterns.capacity = header->max_pattern_dictionary;
    decoder->patterns.stride = BPV1_PATTERN_BYTES;
    decoder->frame.width = header->width;
    decoder->frame.height = header->height;
    decoder->frame.blocks_x = (uint16_t)blocks_x;
    decoder->frame.blocks_y = (uint16_t)blocks_y;
    decoder->frame.block_count = block_count;
    decoder->memory_bytes = sizeof *decoder + block_bytes * 2U +
                            packet_capacity + block_dictionary_bytes +
                            pattern_dictionary_bytes;
    bpv1_decoder_reset(decoder);
    return decoder;
}

void bpv1_decoder_destroy(BPV1Decoder *decoder) {
    if (!decoder) return;
    free(decoder->previous);
    free(decoder->current);
    free(decoder->packet_data);
    free(decoder->blocks.entries);
    free(decoder->patterns.entries);
    free(decoder);
}

void bpv1_decoder_reset(BPV1Decoder *decoder) {
    if (!decoder) return;
    decoder->frame_index = 0;
    decoder->frame.frame_index = 0;
    decoder->frame.keyframe = 0;
    decoder->frame.blocks = NULL;
    reset_references(decoder);
}

size_t bpv1_decoder_packet_capacity(const BPV1Decoder *decoder) {
    return decoder ? decoder->packet_capacity : 0;
}

size_t bpv1_decoder_memory_bytes(const BPV1Decoder *decoder) {
    return decoder ? decoder->memory_bytes : 0;
}

int bpv1_decoder_read_packet(BPV1Decoder *decoder, FILE *file,
                             BPV1Packet *packet) {
    int result;
    if (!decoder || !file || !packet) return BPV1_ERR_ARGUMENT;
    memset(packet, 0, sizeof *packet);
    result = bpv1_frame_info_read(file, &decoder->header, &packet->info);
    if (result != BPV1_OK) return result;
    if (packet->info.frame_bytes > decoder->packet_capacity)
        return BPV1_ERR_RANGE;
    if (read_exact(file, decoder->packet_data, packet->info.frame_bytes))
        return BPV1_ERR_IO;
    packet->data = decoder->packet_data;
    packet->size = packet->info.frame_bytes;
    return BPV1_OK;
}

int bpv1_decoder_decode(BPV1Decoder *decoder, const BPV1Packet *packet,
                        const BPV1Frame **frame) {
    const uint8_t *modes;
    const uint8_t *payload;
    size_t offset;
    uint32_t block_index;
    if (!decoder || !packet || !frame || !packet->data ||
        packet->size != packet->info.frame_bytes ||
        packet->info.mode_bytes > packet->size ||
        decoder->frame_index >= decoder->header.frame_count) {
        return BPV1_ERR_ARGUMENT;
    }
    if (!decoder->frame_index && !packet->info.keyframe)
        return BPV1_ERR_DECODE;
    if (packet->info.keyframe) reset_references(decoder);
    modes = packet->data;
    payload = packet->data;
    offset = packet->info.mode_bytes;

    for (block_index = 0; block_index < decoder->frame.block_count;
         ++block_index) {
        const unsigned mode = read_mode(modes, block_index);
        uint8_t *destination =
            decoder->current + (size_t)block_index * BPV1_RECORD_BYTES;
        if (mode >= MODE_COUNT) return BPV1_ERR_DECODE;
        if (mode == MODE_SKIP) {
            if (!decoder->has_previous) return BPV1_ERR_DECODE;
            memcpy(destination,
                   decoder->previous +
                       (size_t)block_index * BPV1_RECORD_BYTES,
                   BPV1_RECORD_BYTES);
        } else if (mode == MODE_MOTION) {
            int source_x, source_y;
            const int block_x =
                (int)(block_index % decoder->frame.blocks_x);
            const int block_y =
                (int)(block_index / decoder->frame.blocks_x);
            if (!decoder->has_previous || offset + 2U > packet->size)
                return BPV1_ERR_DECODE;
            source_x = block_x + (int)(int8_t)payload[offset];
            source_y = block_y + (int)(int8_t)payload[offset + 1U];
            offset += 2U;
            if (source_x < 0 || source_y < 0 ||
                source_x >= decoder->frame.blocks_x ||
                source_y >= decoder->frame.blocks_y) {
                return BPV1_ERR_DECODE;
            }
            memcpy(destination,
                   decoder->previous +
                       ((size_t)source_y * decoder->frame.blocks_x +
                        (size_t)source_x) * BPV1_RECORD_BYTES,
                   BPV1_RECORD_BYTES);
        } else if (mode == MODE_BLOCK_DICTIONARY) {
            uint8_t *source;
            uint16_t dictionary_index;
            if (offset + 2U > packet->size) return BPV1_ERR_DECODE;
            dictionary_index = read_u16(payload + offset);
            offset += 2U;
            source = dictionary_entry(&decoder->blocks, dictionary_index);
            if (!source) return BPV1_ERR_DECODE;
            memcpy(destination, source, BPV1_RECORD_BYTES);
        } else if (mode == MODE_PATTERN_DICTIONARY) {
            uint8_t *pattern;
            uint16_t dictionary_index;
            if (offset + 7U > packet->size) return BPV1_ERR_DECODE;
            dictionary_index = read_u16(payload + offset);
            offset += 2U;
            pattern = dictionary_entry(&decoder->patterns,
                                       dictionary_index);
            if (!pattern) return BPV1_ERR_DECODE;
            memcpy(destination, payload + offset, PATTERN_OFFSET);
            offset += PATTERN_OFFSET;
            memcpy(destination + PATTERN_OFFSET, pattern,
                   BPV1_PATTERN_BYTES);
            if (validate_record(&decoder->header, destination))
                return BPV1_ERR_DECODE;
            dictionary_add_unique(&decoder->blocks, destination);
        } else {
            if (offset + BPV1_RECORD_BYTES > packet->size)
                return BPV1_ERR_DECODE;
            memcpy(destination, payload + offset, BPV1_RECORD_BYTES);
            offset += BPV1_RECORD_BYTES;
            if (validate_record(&decoder->header, destination))
                return BPV1_ERR_DECODE;
            dictionary_add_unique(&decoder->patterns,
                                  destination + PATTERN_OFFSET);
            dictionary_add_unique(&decoder->blocks, destination);
        }
    }
    if (offset != packet->size) return BPV1_ERR_DECODE;
    {
        uint8_t *scratch = decoder->previous;
        decoder->previous = decoder->current;
        decoder->current = scratch;
    }
    decoder->has_previous = 1;
    decoder->frame.frame_index = decoder->frame_index++;
    decoder->frame.keyframe = packet->info.keyframe;
    decoder->frame.blocks = decoder->previous;
    *frame = &decoder->frame;
    return BPV1_OK;
}

static int frame_row_arguments(const BPV1Header *header,
                               const BPV1Frame *frame, uint16_t y) {
    if (!header || !frame || !frame->blocks ||
        frame->width != header->width || frame->height != header->height ||
        y >= frame->height) {
        return BPV1_ERR_ARGUMENT;
    }
    return BPV1_OK;
}

static const uint8_t *pixel_rgb(const BPV1Header *header,
                                const BPV1Frame *frame, uint16_t x,
                                uint16_t y) {
    const uint32_t block_index =
        (uint32_t)(y >> 2) * frame->blocks_x + (x >> 2);
    const uint8_t *record =
        frame->blocks + (size_t)block_index * BPV1_RECORD_BYTES;
    const unsigned pixel = ((y & 3U) << 2) | (x & 3U);
    const unsigned local =
        (record[PATTERN_OFFSET + (pixel >> 2)] >>
         (6U - ((pixel & 3U) << 1))) & 3U;
    const unsigned color =
        ((unsigned)record[0] * BPV1_COLORS_PER_PALETTE +
         record[1U + local]) * 3U;
    return header->palette + color;
}

int bpv1_frame_render_rgb24_row(const BPV1Header *header,
                                const BPV1Frame *frame, uint16_t y,
                                uint8_t *rgb, size_t rgb_bytes) {
    uint16_t x;
    if (!rgb || frame_row_arguments(header, frame, y) ||
        rgb_bytes < (size_t)frame->width * 3U) {
        return BPV1_ERR_ARGUMENT;
    }
    for (x = 0; x < frame->width; ++x) {
        const uint8_t *color = pixel_rgb(header, frame, x, y);
        rgb[(size_t)x * 3U] = color[0];
        rgb[(size_t)x * 3U + 1U] = color[1];
        rgb[(size_t)x * 3U + 2U] = color[2];
    }
    return BPV1_OK;
}

int bpv1_frame_render_rgb565_row(const BPV1Header *header,
                                 const BPV1Frame *frame, uint16_t y,
                                 uint16_t *rgb565, size_t pixels) {
    uint16_t x;
    if (!rgb565 || frame_row_arguments(header, frame, y) ||
        pixels < frame->width) {
        return BPV1_ERR_ARGUMENT;
    }
    for (x = 0; x < frame->width; ++x) {
        const uint8_t *color = pixel_rgb(header, frame, x, y);
        rgb565[x] =
            (uint16_t)(((uint16_t)(color[0] & 0xf8U) << 8) |
                       ((uint16_t)(color[1] & 0xfcU) << 3) |
                       (color[2] >> 3));
    }
    return BPV1_OK;
}
