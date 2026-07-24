#include "bpv1.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifndef BPV1_FAST_CANONICAL_DICTIONARIES
#define BPV1_FAST_CANONICAL_DICTIONARIES 0
#endif

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
#if !BPV1_FAST_CANONICAL_DICTIONARIES
    uint16_t *buckets;
    uint16_t *next;
    uint32_t bucket_count;
#endif
    uint32_t capacity;
    uint32_t count;
    uint32_t start;
    size_t stride;
} Dictionary;

#define DICTIONARY_NONE UINT16_MAX

struct BPV1Decoder {
    BPV1Header header;
    BPV1Frame frame;
    uint8_t *previous;
    uint8_t *current;
    uint8_t *packet_data;
    size_t block_bytes;
    size_t video_capacity;
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

static size_t maximum_audio_bytes(const BPV1Header *header) {
    uint64_t numerator;
    if (!header || header->version < BPV1_AUDIO_VERSION ||
        header->audio_codec == BPV1_AUDIO_NONE) {
        return 0;
    }
    numerator = (uint64_t)header->audio_sample_rate * header->fps_den;
    return (size_t)((numerator + header->fps_num - 1U) /
                    header->fps_num) * header->audio_channels;
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
         header->version != BPV1_AUDIO_VERSION &&
         header->version != BPV1_VIDEO_VERSION &&
         header->version != BPV1_LEGACY_VERSION) ||
        (header->version >= BPV1_VIDEO_VERSION &&
         header->palette_count != BPV1_PALETTE_COUNT) ||
        (header->version == BPV1_LEGACY_VERSION &&
         header->palette_count != 16) ||
        (header->version >= BPV1_AUDIO_VERSION &&
         !((header->audio_codec == BPV1_AUDIO_NONE &&
            !header->audio_sample_rate && !header->audio_channels) ||
           (header->audio_codec == BPV1_AUDIO_PCM_U8 &&
            header->audio_sample_rate && header->audio_channels == 1))) ||
        (header->version < BPV1_AUDIO_VERSION &&
         (header->audio_codec != BPV1_AUDIO_NONE ||
          header->audio_sample_rate || header->audio_channels))) {
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
    if (header->version == BPV1_VERSION) {
        if (*packet_capacity > SIZE_MAX - BPV1_MAX_PALETTE_BYTES)
            return BPV1_ERR_RANGE;
        *packet_capacity += BPV1_MAX_PALETTE_BYTES;
    }
    return BPV1_OK;
}

static uint8_t *dictionary_entry(const Dictionary *dictionary,
                                 uint32_t index) {
    uint32_t physical;
    if (!dictionary || index >= dictionary->count) return NULL;
    physical = dictionary->start + index;
    if (physical >= dictionary->capacity)
        physical -= dictionary->capacity;
    return dictionary->entries + (size_t)physical * dictionary->stride;
}

static void dictionary_reset(Dictionary *dictionary) {
    dictionary->count = 0;
    dictionary->start = 0;
#if !BPV1_FAST_CANONICAL_DICTIONARIES
    if (dictionary->buckets) {
        memset(dictionary->buckets, 0xff,
               (size_t)dictionary->bucket_count *
                   sizeof *dictionary->buckets);
    }
#endif
}

#if !BPV1_FAST_CANONICAL_DICTIONARIES
static uint32_t dictionary_hash(const uint8_t *value, size_t size) {
    uint32_t hash = UINT32_C(2166136261);
    size_t index;
    for (index = 0; index < size; ++index) {
        hash ^= value[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static int dictionary_value_equal(const uint8_t *left,
                                  const uint8_t *right, size_t size) {
    size_t index;
    for (index = 0; index < size; ++index) {
        if (left[index] != right[index]) return 0;
    }
    return 1;
}

static uint16_t dictionary_find_physical(
    const Dictionary *dictionary, const uint8_t *value, uint32_t hash) {
    uint16_t physical =
        dictionary->buckets[hash & (dictionary->bucket_count - 1U)];
    while (physical != DICTIONARY_NONE) {
        const uint8_t *entry =
            dictionary->entries +
            (size_t)physical * dictionary->stride;
        if (dictionary_value_equal(entry, value, dictionary->stride))
            return physical;
        physical = dictionary->next[physical];
    }
    return DICTIONARY_NONE;
}

static void dictionary_remove_physical(Dictionary *dictionary,
                                       uint16_t physical) {
    const uint8_t *entry =
        dictionary->entries + (size_t)physical * dictionary->stride;
    const uint32_t hash = dictionary_hash(entry, dictionary->stride);
    uint16_t *link =
        &dictionary->buckets[hash & (dictionary->bucket_count - 1U)];
    while (*link != DICTIONARY_NONE) {
        if (*link == physical) {
            *link = dictionary->next[physical];
            return;
        }
        link = &dictionary->next[*link];
    }
}
#endif

static void dictionary_add_unique(Dictionary *dictionary,
                                  const uint8_t *value) {
#if !BPV1_FAST_CANONICAL_DICTIONARIES
    const uint32_t hash = dictionary_hash(value, dictionary->stride);
#endif
    uint32_t physical;
#if !BPV1_FAST_CANONICAL_DICTIONARIES
    uint32_t bucket;
    if (dictionary_find_physical(dictionary, value, hash) !=
        DICTIONARY_NONE) {
        return;
    }

    if (dictionary->count < dictionary->capacity) {
        physical = dictionary->start + dictionary->count;
        if (physical >= dictionary->capacity)
            physical -= dictionary->capacity;
        dictionary->count++;
    } else {
        physical = dictionary->start;
        dictionary_remove_physical(dictionary, (uint16_t)physical);
#else
    if (dictionary->count < dictionary->capacity) {
        physical = dictionary->start + dictionary->count;
        if (physical >= dictionary->capacity)
            physical -= dictionary->capacity;
        dictionary->count++;
    } else {
        physical = dictionary->start;
#endif
        if (++dictionary->start == dictionary->capacity)
            dictionary->start = 0;
    }
    memcpy(dictionary->entries + (size_t)physical * dictionary->stride,
           value, dictionary->stride);
#if !BPV1_FAST_CANONICAL_DICTIONARIES
    bucket = hash & (dictionary->bucket_count - 1U);
    dictionary->next[physical] = dictionary->buckets[bucket];
    dictionary->buckets[bucket] = (uint16_t)physical;
#endif
}

static int dictionary_allocate(Dictionary *dictionary, uint32_t capacity,
                               size_t stride) {
#if !BPV1_FAST_CANONICAL_DICTIONARIES
    uint32_t bucket_count = 1;
    size_t bucket_bytes;
    size_t next_bytes;
#endif
    size_t entry_bytes;
#if !BPV1_FAST_CANONICAL_DICTIONARIES
    while (bucket_count < capacity * 2U)
        bucket_count <<= 1;
#endif
    if (multiply_size(capacity, stride, &entry_bytes)
#if !BPV1_FAST_CANONICAL_DICTIONARIES
        ||
        multiply_size(bucket_count, sizeof *dictionary->buckets,
                      &bucket_bytes) ||
        multiply_size(capacity, sizeof *dictionary->next, &next_bytes)
#endif
    ) {
        return BPV1_ERR_RANGE;
    }
    dictionary->entries = (uint8_t *)malloc(entry_bytes);
#if !BPV1_FAST_CANONICAL_DICTIONARIES
    dictionary->buckets = (uint16_t *)malloc(bucket_bytes);
    dictionary->next = (uint16_t *)malloc(next_bytes);
    if (!dictionary->entries || !dictionary->buckets ||
        !dictionary->next) {
        return BPV1_ERR_MEMORY;
    }
    dictionary->bucket_count = bucket_count;
#else
    if (!dictionary->entries) return BPV1_ERR_MEMORY;
#endif
    dictionary->capacity = capacity;
    dictionary->stride = stride;
    dictionary_reset(dictionary);
    return BPV1_OK;
}

static void dictionary_destroy(Dictionary *dictionary) {
    if (!dictionary) return;
    free(dictionary->entries);
#if !BPV1_FAST_CANONICAL_DICTIONARIES
    free(dictionary->buckets);
    free(dictionary->next);
#endif
    memset(dictionary, 0, sizeof *dictionary);
}

static size_t dictionary_memory_bytes(const Dictionary *dictionary) {
    size_t bytes =
        (size_t)dictionary->capacity * dictionary->stride;
#if !BPV1_FAST_CANONICAL_DICTIONARIES
    bytes +=
           (size_t)dictionary->bucket_count *
               sizeof *dictionary->buckets +
           (size_t)dictionary->capacity * sizeof *dictionary->next;
#endif
    return bytes;
}

static inline void copy_record(uint8_t *destination,
                               const uint8_t *source) {
    destination[0] = source[0];
    destination[1] = source[1];
    destination[2] = source[2];
    destination[3] = source[3];
    destination[4] = source[4];
    destination[5] = source[5];
    destination[6] = source[6];
    destination[7] = source[7];
    destination[8] = source[8];
}

typedef struct {
    const uint8_t *cursor;
    uint32_t buffer;
    unsigned available;
} ModeReader;

static inline unsigned read_mode(ModeReader *reader) {
    if (reader->available < 3U) {
        reader->buffer =
            (reader->buffer << 8) | *reader->cursor++;
        reader->available += 8U;
    }
    reader->available -= 3U;
    return (reader->buffer >> reader->available) & 7U;
}

static int validate_record(const BPV1Header *header,
                           const uint8_t record[BPV1_RECORD_BYTES]) {
    return record[0] >= header->palette_count ||
                   record[1] >= BPV1_COLORS_PER_PALETTE ||
                   record[2] >= BPV1_COLORS_PER_PALETTE ||
                   record[3] >= BPV1_COLORS_PER_PALETTE ||
                   record[4] >= BPV1_COLORS_PER_PALETTE
               ? BPV1_ERR_DECODE
               : BPV1_OK;
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
    uint8_t audio[4];
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
    if (header->version >= BPV1_AUDIO_VERSION) {
        header->audio_codec = fixed[24];
        if (read_exact(file, audio, sizeof audio)) return BPV1_ERR_IO;
        header->audio_sample_rate = read_u16(audio);
        header->audio_channels = audio[2];
        if (audio[3] != 0) return BPV1_ERR_FORMAT;
    } else if (fixed[24] != 0) {
        return BPV1_ERR_FORMAT;
    }
    header->palette_count =
        header->version >= BPV1_VIDEO_VERSION ? BPV1_PALETTE_COUNT :
        header->version == BPV1_LEGACY_VERSION ? 16 : 0;
    if (header_layout(header, &blocks_x, &blocks_y, &block_count,
                      &block_bytes, &mode_bytes, &packet_capacity)) {
        return BPV1_ERR_FORMAT;
    }
    palette_bytes =
        (size_t)header->palette_count * BPV1_COLORS_PER_PALETTE * 3U;
    if (header->version == BPV1_VERSION) return BPV1_OK;
    return read_exact(file, header->palette, palette_bytes);
}

int bpv1_frame_info_read(FILE *file, const BPV1Header *header,
                         BPV1FrameInfo *info) {
    uint8_t bytes[13];
    uint32_t blocks_x, blocks_y, block_count;
    size_t block_bytes, mode_bytes, packet_capacity;
    size_t count;
    if (!file || !header || !info) return BPV1_ERR_ARGUMENT;
    const size_t header_bytes =
        header->version >= BPV1_AUDIO_VERSION ? sizeof bytes : 9U;
    count = fread(bytes, 1, header_bytes, file);
    if (!count && feof(file)) return BPV1_EOF;
    if (count != header_bytes) return BPV1_ERR_IO;
    if (header_layout(header, &blocks_x, &blocks_y, &block_count,
                      &block_bytes, &mode_bytes, &packet_capacity)) {
        return BPV1_ERR_FORMAT;
    }
    info->keyframe = bytes[0];
    info->frame_bytes = read_u32(bytes + 1);
    info->mode_bytes = read_u32(bytes + 5);
    info->audio_bytes =
        header->version >= BPV1_AUDIO_VERSION ? read_u32(bytes + 9) : 0;
    {
        const size_t palette_bytes =
            header->version == BPV1_VERSION && info->keyframe
                ? BPV1_MAX_PALETTE_BYTES
                : 0U;
        if (palette_bytes > SIZE_MAX - info->mode_bytes ||
            info->frame_bytes < palette_bytes + info->mode_bytes) {
            return BPV1_ERR_FORMAT;
        }
    }
    if (info->keyframe > 1U || info->mode_bytes != mode_bytes ||
        info->frame_bytes > packet_capacity ||
        info->audio_bytes > maximum_audio_bytes(header) ||
        (header->audio_codec == BPV1_AUDIO_NONE && info->audio_bytes)) {
        return BPV1_ERR_FORMAT;
    }
    return BPV1_OK;
}

BPV1Decoder *bpv1_decoder_create(const BPV1Header *header) {
    BPV1Decoder *decoder = NULL;
    uint32_t blocks_x, blocks_y, block_count;
    size_t block_bytes, mode_bytes, packet_capacity;
    if (!header) return NULL;
    if (header_layout(header, &blocks_x, &blocks_y, &block_count,
                      &block_bytes, &mode_bytes, &packet_capacity)) {
        return NULL;
    }
    decoder = (BPV1Decoder *)calloc(1, sizeof *decoder);
    if (!decoder) return NULL;
    decoder->previous = (uint8_t *)malloc(block_bytes);
    decoder->current = (uint8_t *)malloc(block_bytes);
    {
        const size_t audio_capacity = maximum_audio_bytes(header);
        if (audio_capacity > SIZE_MAX - packet_capacity) {
            bpv1_decoder_destroy(decoder);
            return NULL;
        }
        decoder->video_capacity = packet_capacity;
        decoder->packet_capacity = packet_capacity + audio_capacity;
    }
    decoder->packet_data = (uint8_t *)malloc(decoder->packet_capacity);
    if (dictionary_allocate(&decoder->blocks,
                            header->max_block_dictionary,
                            BPV1_RECORD_BYTES) != BPV1_OK ||
        dictionary_allocate(&decoder->patterns,
                            header->max_pattern_dictionary,
                            BPV1_PATTERN_BYTES) != BPV1_OK) {
        bpv1_decoder_destroy(decoder);
        return NULL;
    }
    if (!decoder->previous || !decoder->current || !decoder->packet_data ||
        !decoder->blocks.entries || !decoder->patterns.entries) {
        bpv1_decoder_destroy(decoder);
        return NULL;
    }
    decoder->header = *header;
    decoder->block_bytes = block_bytes;
    decoder->frame.width = header->width;
    decoder->frame.height = header->height;
    decoder->frame.blocks_x = (uint16_t)blocks_x;
    decoder->frame.blocks_y = (uint16_t)blocks_y;
    decoder->frame.block_count = block_count;
    decoder->frame.palette = decoder->header.palette;
    decoder->memory_bytes = sizeof *decoder + block_bytes * 2U +
                            decoder->packet_capacity +
                            dictionary_memory_bytes(&decoder->blocks) +
                            dictionary_memory_bytes(&decoder->patterns);
    bpv1_decoder_reset(decoder);
    return decoder;
}

void bpv1_decoder_destroy(BPV1Decoder *decoder) {
    if (!decoder) return;
    free(decoder->previous);
    free(decoder->current);
    free(decoder->packet_data);
    dictionary_destroy(&decoder->blocks);
    dictionary_destroy(&decoder->patterns);
    free(decoder);
}

void bpv1_decoder_reset(BPV1Decoder *decoder) {
    if (!decoder) return;
    decoder->frame_index = 0;
    decoder->frame.frame_index = 0;
    decoder->frame.keyframe = 0;
    decoder->frame.blocks = NULL;
    decoder->frame.palette = decoder->header.palette;
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
    size_t total_size;
    if (!decoder || !file || !packet) return BPV1_ERR_ARGUMENT;
    memset(packet, 0, sizeof *packet);
    result = bpv1_frame_info_read(file, &decoder->header, &packet->info);
    if (result != BPV1_OK) return result;
    if (packet->info.frame_bytes > decoder->video_capacity ||
        packet->info.audio_bytes >
            decoder->packet_capacity - packet->info.frame_bytes)
        return BPV1_ERR_RANGE;
    total_size = (size_t)packet->info.frame_bytes +
                 packet->info.audio_bytes;
    if (read_exact(file, decoder->packet_data, total_size))
        return BPV1_ERR_IO;
    packet->data = decoder->packet_data;
    packet->size = packet->info.frame_bytes;
    packet->audio_data = packet->info.audio_bytes
                             ? decoder->packet_data + packet->info.frame_bytes
                             : NULL;
    packet->audio_size = packet->info.audio_bytes;
    return BPV1_OK;
}

int bpv1_decoder_decode(BPV1Decoder *decoder, const BPV1Packet *packet,
                        const BPV1Frame **frame) {
    const uint8_t *modes;
    const uint8_t *cursor;
    const uint8_t *payload_end;
    const uint8_t *previous_at_position;
    uint8_t *destination;
    size_t palette_bytes;
    ModeReader mode_reader;
    uint32_t block_index;
    int block_x = 0;
    int block_y = 0;
    if (!decoder || !packet || !frame || !packet->data ||
        packet->size != packet->info.frame_bytes ||
        decoder->frame_index >= decoder->header.frame_count) {
        return BPV1_ERR_ARGUMENT;
    }
    if (!decoder->frame_index && !packet->info.keyframe)
        return BPV1_ERR_DECODE;
    if (packet->info.keyframe) reset_references(decoder);
    palette_bytes =
        decoder->header.version == BPV1_VERSION && packet->info.keyframe
            ? BPV1_MAX_PALETTE_BYTES
            : 0U;
    if (palette_bytes > packet->size ||
        packet->info.mode_bytes > packet->size - palette_bytes) {
        return BPV1_ERR_DECODE;
    }
    if (palette_bytes) {
        memcpy(decoder->header.palette, packet->data, palette_bytes);
    }
    modes = packet->data + palette_bytes;
    cursor = modes + packet->info.mode_bytes;
    payload_end = packet->data + packet->size;
    previous_at_position = decoder->previous;
    destination = decoder->current;
    mode_reader.cursor = modes;
    mode_reader.buffer = 0;
    mode_reader.available = 0;

    for (block_index = 0; block_index < decoder->frame.block_count;
         ++block_index) {
        const unsigned mode = read_mode(&mode_reader);
        if (mode >= MODE_COUNT) return BPV1_ERR_DECODE;
        if (mode == MODE_SKIP) {
            if (!decoder->has_previous) return BPV1_ERR_DECODE;
            copy_record(destination, previous_at_position);
        } else if (mode == MODE_MOTION) {
            int source_x, source_y;
            if (!decoder->has_previous ||
                (size_t)(payload_end - cursor) < 2U)
                return BPV1_ERR_DECODE;
            source_x = block_x + (int)(int8_t)cursor[0];
            source_y = block_y + (int)(int8_t)cursor[1];
            cursor += 2;
            if (source_x < 0 || source_y < 0 ||
                source_x >= decoder->frame.blocks_x ||
                source_y >= decoder->frame.blocks_y) {
                return BPV1_ERR_DECODE;
            }
            copy_record(
                destination,
                decoder->previous +
                    ((size_t)source_y * decoder->frame.blocks_x +
                     (size_t)source_x) * BPV1_RECORD_BYTES);
        } else if (mode == MODE_BLOCK_DICTIONARY) {
            uint8_t *source;
            uint16_t dictionary_index;
            if ((size_t)(payload_end - cursor) < 2U)
                return BPV1_ERR_DECODE;
            dictionary_index = read_u16(cursor);
            cursor += 2;
            source = dictionary_entry(&decoder->blocks, dictionary_index);
            if (!source) return BPV1_ERR_DECODE;
            copy_record(destination, source);
        } else if (mode == MODE_PATTERN_DICTIONARY) {
            uint8_t *pattern;
            uint16_t dictionary_index;
            if ((size_t)(payload_end - cursor) < 7U)
                return BPV1_ERR_DECODE;
            dictionary_index = read_u16(cursor);
            cursor += 2;
            pattern = dictionary_entry(&decoder->patterns,
                                       dictionary_index);
            if (!pattern) return BPV1_ERR_DECODE;
            memcpy(destination, cursor, PATTERN_OFFSET);
            cursor += PATTERN_OFFSET;
            memcpy(destination + PATTERN_OFFSET, pattern,
                   BPV1_PATTERN_BYTES);
            if (validate_record(&decoder->header, destination))
                return BPV1_ERR_DECODE;
            dictionary_add_unique(&decoder->blocks, destination);
        } else {
            if ((size_t)(payload_end - cursor) < BPV1_RECORD_BYTES)
                return BPV1_ERR_DECODE;
            copy_record(destination, cursor);
            cursor += BPV1_RECORD_BYTES;
            if (validate_record(&decoder->header, destination))
                return BPV1_ERR_DECODE;
            dictionary_add_unique(&decoder->patterns,
                                  destination + PATTERN_OFFSET);
            dictionary_add_unique(&decoder->blocks, destination);
        }
        destination += BPV1_RECORD_BYTES;
        previous_at_position += BPV1_RECORD_BYTES;
        if (++block_x == decoder->frame.blocks_x) {
            block_x = 0;
            ++block_y;
        }
    }
    if (cursor != payload_end) return BPV1_ERR_DECODE;
    {
        uint8_t *scratch = decoder->previous;
        decoder->previous = decoder->current;
        decoder->current = scratch;
    }
    decoder->has_previous = 1;
    decoder->frame.frame_index = decoder->frame_index++;
    decoder->frame.keyframe = packet->info.keyframe;
    decoder->frame.blocks = decoder->previous;
    decoder->frame.palette = decoder->header.palette;
    *frame = &decoder->frame;
    return BPV1_OK;
}

size_t bpv1_packet_audio_size(const BPV1Packet *packet) {
    return packet ? packet->audio_size : 0;
}

const uint8_t *bpv1_packet_audio_data(const BPV1Packet *packet) {
    return packet && packet->audio_size ? packet->audio_data : NULL;
}

static int frame_row_arguments(const BPV1Header *header,
                               const BPV1Frame *frame, uint16_t y) {
    if (!header || !frame || !frame->blocks || !frame->palette ||
        frame->width != header->width || frame->height != header->height ||
        y >= frame->height) {
        return BPV1_ERR_ARGUMENT;
    }
    return BPV1_OK;
}

static const uint8_t *pixel_rgb(const BPV1Frame *frame, uint16_t x,
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
    return frame->palette + color;
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
        const uint8_t *color = pixel_rgb(frame, x, y);
        rgb[(size_t)x * 3U] = color[0];
        rgb[(size_t)x * 3U + 1U] = color[1];
        rgb[(size_t)x * 3U + 2U] = color[2];
    }
    return BPV1_OK;
}

int bpv1_frame_render_rgb565_row(const BPV1Header *header,
                                 const BPV1Frame *frame, uint16_t y,
                                 uint16_t *rgb565, size_t pixels) {
    return bpv1_frame_render_rgb565_rows(
        header, frame, y, 1, rgb565,
        frame ? frame->width : 0, pixels);
}

static inline uint16_t rgb888_to_rgb565(const uint8_t *color) {
    return (uint16_t)(((uint16_t)(color[0] & 0xf8U) << 8) |
                      ((uint16_t)(color[1] & 0xfcU) << 3) |
                      (color[2] >> 3));
}

int bpv1_frame_render_rgb565_rows(const BPV1Header *header,
                                  const BPV1Frame *frame, uint16_t y,
                                  uint16_t rows, uint16_t *rgb565,
                                  size_t stride_pixels, size_t pixels) {
    uint16_t output_row = 0;
    if (!rgb565 || !rows ||
        frame_row_arguments(header, frame, y) ||
        rows > (uint16_t)(frame->height - y) ||
        stride_pixels < frame->width ||
        (size_t)(rows - 1U) >
            (SIZE_MAX - frame->width) / stride_pixels ||
        pixels <
            (size_t)(rows - 1U) * stride_pixels + frame->width) {
        return BPV1_ERR_ARGUMENT;
    }

    while (output_row < rows) {
        const uint16_t source_y = (uint16_t)(y + output_row);
        const unsigned first_local_y = source_y & 3U;
        const unsigned remaining_rows = (unsigned)rows - output_row;
        const unsigned block_rows =
            BPV1_BLOCK_SIZE - first_local_y;
        const uint16_t group_rows = (uint16_t)(
            remaining_rows < block_rows ? remaining_rows : block_rows);
        const uint8_t *record =
            frame->blocks +
            (size_t)(source_y >> 2) * frame->blocks_x *
                BPV1_RECORD_BYTES;
        uint16_t x = 0;

        while (x < frame->width) {
            const uint8_t *palette =
                frame->palette +
                (size_t)record[0] * BPV1_COLORS_PER_PALETTE * 3U;
            uint16_t colors[4];
            const uint16_t block_pixels =
                (uint16_t)(frame->width - x < BPV1_BLOCK_SIZE
                               ? frame->width - x
                               : BPV1_BLOCK_SIZE);
            uint16_t row;
            unsigned color_index;

            for (color_index = 0; color_index < 4U; ++color_index) {
                colors[color_index] = rgb888_to_rgb565(
                    palette + (size_t)record[1U + color_index] * 3U);
            }
            for (row = 0; row < group_rows; ++row) {
                const uint8_t pattern =
                    record[PATTERN_OFFSET + first_local_y + row];
                uint16_t *destination =
                    rgb565 + (size_t)(output_row + row) *
                                 stride_pixels +
                    x;
                uint16_t pixel;
                for (pixel = 0; pixel < block_pixels; ++pixel) {
                    destination[pixel] =
                        colors[(pattern >> (6U - pixel * 2U)) & 3U];
                }
            }
            x = (uint16_t)(x + block_pixels);
            record += BPV1_RECORD_BYTES;
        }
        output_row = (uint16_t)(output_row + group_rows);
    }
    return BPV1_OK;
}
