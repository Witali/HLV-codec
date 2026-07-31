#include "h263_3gp.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "mp4dec_api.h"

#ifndef PV_H263_STAGE_PROFILE
#define PV_H263_STAGE_PROFILE 0
#endif

#if PV_H263_STAGE_PROFILE && defined(ESP_PLATFORM)
#include "esp_cpu.h"
#define H263_PROFILE_NOW() esp_cpu_get_cycle_count()
#define H263_PROFILE_START(name) uint32_t name = H263_PROFILE_NOW()
#define H263_PROFILE_ADD(decoder, field, start) do {                     \
        (decoder)->profile.field +=                                     \
            (uint32_t)(H263_PROFILE_NOW() - (start));                   \
    } while (0)
#define H263_PROFILE_COUNT(decoder, field, amount) \
    ((decoder)->profile.field += (uint32_t)(amount))
#elif PV_H263_STAGE_PROFILE
#define H263_PROFILE_NOW() 0U
#define H263_PROFILE_START(name) uint32_t name = 0U
#define H263_PROFILE_ADD(decoder, field, start) do {                     \
        (void)(decoder);                                                \
        (void)(start);                                                  \
    } while (0)
#define H263_PROFILE_COUNT(decoder, field, amount) \
    ((decoder)->profile.field += (uint32_t)(amount))
#else
#define H263_PROFILE_NOW() 0U
#define H263_PROFILE_START(name) uint32_t name = 0U
#define H263_PROFILE_ADD(decoder, field, start) do {                     \
        (void)(decoder);                                                \
        (void)(start);                                                  \
    } while (0)
#define H263_PROFILE_COUNT(decoder, field, amount) do {                  \
        (void)(decoder);                                                \
        (void)(amount);                                                 \
    } while (0)
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static uint32_t fourcc(char a, char b, char c, char d) {
    return ((uint32_t)((uint8_t)(a)) << 24) |
           ((uint32_t)((uint8_t)(b)) << 16) |
           ((uint32_t)((uint8_t)(c)) << 8) |
           (uint32_t)((uint8_t)(d));
}

static uint32_t fourccLe(char a, char b, char c, char d) {
    return (uint32_t)((uint8_t)(a)) |
           ((uint32_t)((uint8_t)(b)) << 8) |
           ((uint32_t)((uint8_t)(c)) << 16) |
           ((uint32_t)((uint8_t)(d)) << 24);
}

enum {
    kInputPadding = 8,
    kMpeg4VolHeaderBytes = 256
};

#ifndef H263_PACKET_BUFFER_BYTES
#define H263_PACKET_BUFFER_BYTES 4096
#endif

static bool isSupportedH263Geometry(uint16_t width, uint16_t height) {
    return (width == 176 && height == 144) ||
           (width == 352 && height == 288);
}

static bool isSupportedVideoGeometry(uint8_t video_codec,
                                     uint16_t width,
                                     uint16_t height) {
    return video_codec == H263_VIDEO_CODEC_MPEG4_SIMPLE
               ? width == 320 && height == 240
               : isSupportedH263Geometry(width, height);
}

typedef struct Box {
    uint32_t type;
    uint64_t start;
    uint64_t data;
    uint64_t end;
} Box;

typedef struct StscEntry {
    uint32_t first_chunk;
    uint32_t samples_per_chunk;
} StscEntry;

typedef struct SttsEntry {
    uint32_t sample_count;
    uint32_t sample_delta;
} SttsEntry;

static uint16_t readLe16Value(const uint8_t *value) {
    return (uint16_t)(value[0] | ((uint16_t)value[1] << 8));
}

static uint32_t readLe32Value(const uint8_t *value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int hexDigit(uint8_t value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static bool seekFile(FILE *file, uint64_t offset) {
#ifdef _WIN32
    return offset <= (uint64_t)(INT64_MAX) &&
           _fseeki64(file, (int64_t)(offset), SEEK_SET) == 0;
#else
    return offset <= (uint64_t)(
                         (sizeof(off_t) >= 8 ? INT64_MAX : INT32_MAX)) &&
           fseeko(file, (off_t)(offset), SEEK_SET) == 0;
#endif
}

static bool tellFile(FILE *file, uint64_t *offset) {
#ifdef _WIN32
    const int64_t position = _ftelli64(file);
#else
    const off_t position = ftello(file);
#endif
    if (position < 0) return false;
    *offset = (uint64_t)(position);
    return true;
}

static bool readExact(FILE *file, void *destination, size_t size) {
    return size == 0 || fread(destination, 1, size, file) == size;
}

static bool readU16(FILE *file, uint16_t *value) {
    uint8_t bytes[2];
    if (!readExact(file, bytes, sizeof bytes)) return false;
    *value = (uint16_t)((bytes[0] << 8) | bytes[1]);
    return true;
}

static bool readU32(FILE *file, uint32_t *value) {
    uint8_t bytes[4];
    if (!readExact(file, bytes, sizeof bytes)) return false;
    *value = ((uint32_t)(bytes[0]) << 24) |
             ((uint32_t)(bytes[1]) << 16) |
             ((uint32_t)(bytes[2]) << 8) |
             (uint32_t)(bytes[3]);
    return true;
}

static bool readU64(FILE *file, uint64_t *value) {
    uint32_t high = 0;
    uint32_t low = 0;
    if (!readU32(file, &high) || !readU32(file, &low)) return false;
    *value = ((uint64_t)(high) << 32) | low;
    return true;
}

static bool readLe32(FILE *file, uint32_t *value) {
    uint8_t bytes[4];
    if (!readExact(file, bytes, sizeof bytes)) return false;
    *value = (uint32_t)(bytes[0]) |
             ((uint32_t)(bytes[1]) << 8) |
             ((uint32_t)(bytes[2]) << 16) |
             ((uint32_t)(bytes[3]) << 24);
    return true;
}

static bool fileSize(FILE *file, uint64_t *size) {
    uint64_t saved = 0;
    if (!tellFile(file, &saved)) return false;
#ifdef _WIN32
    if (_fseeki64(file, 0, SEEK_END) != 0) return false;
#else
    if (fseeko(file, 0, SEEK_END) != 0) return false;
#endif
    const bool ok = tellFile(file, size);
    return seekFile(file, saved) && ok;
}

static bool readBox(FILE *file, uint64_t limit, Box *box) {
    uint64_t start = 0;
    uint32_t size32 = 0;
    uint32_t type = 0;
    if (!tellFile(file, &start) || start > limit || limit - start < 8 ||
        !readU32(file, &size32) || !readU32(file, &type)) {
        return false;
    }
    uint64_t size = size32;
    uint64_t header = 8;
    if (size32 == 1) {
        if (limit - start < 16 || !readU64(file, &size)) return false;
        header = 16;
    } else if (size32 == 0) {
        size = limit - start;
    }
    if (size < header || size > limit - start) return false;
    box->type = type;
    box->start = start;
    box->data = start + header;
    box->end = start + size;
    return true;
}

static bool findChildFrom(FILE *file, Box parent, uint32_t type, Box *child,
                   uint64_t first_data) {
    uint64_t cursor = first_data ? first_data : parent.data;
    while (cursor < parent.end) {
        if (!seekFile(file, cursor) || !readBox(file, parent.end, child))
            return false;
        if (child->type == type) return true;
        if (child->end <= cursor) return false;
        cursor = child->end;
    }
    return false;
}

static bool findChild(FILE *file, Box parent, uint32_t type, Box *child) {
    return findChildFrom(file, parent, type, child, 0);
}

static uint32_t gcd32(uint32_t a, uint32_t b) {
    while (b != 0) {
        const uint32_t next = a % b;
        a = b;
        b = next;
    }
    return a ? a : 1;
}

static int aviStreamNumber(uint32_t id) {
    const int high = hexDigit((uint8_t)(id));
    const int low = hexDigit((uint8_t)(id >> 8));
    return high < 0 || low < 0 ? -1 : (high << 4) | low;
}

static bool isAviVideoChunk(uint32_t id, uint8_t stream) {
    const uint8_t c2 = (uint8_t)(id >> 16);
    const uint8_t c3 = (uint8_t)(id >> 24);
    return aviStreamNumber(id) == stream &&
           ((c2 == 'd' && c3 == 'c') ||
            (c2 == 'd' && c3 == 'b'));
}

static bool isAviAudioChunk(uint32_t id, uint8_t stream) {
    return aviStreamNumber(id) == stream &&
           (uint8_t)(id >> 16) == 'w' &&
           (uint8_t)(id >> 24) == 'b';
}

typedef struct AviState {
    uint64_t movi_start;
    uint64_t movi_end;
    uint64_t next_video_offset;
    uint64_t next_audio_offset;
    uint32_t video_scale;
    uint32_t video_rate;
    uint32_t video_length;
    uint32_t main_frame_count;
    uint32_t microseconds_per_frame;
    uint32_t audio_format;
    uint16_t mpeg4_vol_size;
    uint8_t video_stream;
    uint8_t audio_stream;
    uint8_t mpeg4_vol[kMpeg4VolHeaderBytes];
} AviState;

static void resetAviState(AviState *avi) {
    memset(avi, 0, sizeof(*avi));
    avi->video_stream = 0xff;
    avi->audio_stream = 0xff;
}


struct H2633gpDecoder {
    VideoDecControls controls;
    H2633gpInfo info;
    H263DecodeProfile profile;
    uint8_t *packet;
    size_t packet_capacity;
    FILE *stream_file;
    uint32_t stream_remaining;
    bool stream_io_error;
    uint8_t *output_y[2];
    uint8_t *output_u[2];
    uint8_t *output_v[2];
    size_t output_bytes;
    uint8_t allocated_output_count;
    uint8_t output_count;
    uint8_t requested_output_count;
    uint16_t buffer_width;
    uint16_t buffer_height;
    bool intra_only;
    bool compact_reference_enabled;
    CompactYuv420Frame compact_reference;
    CompactYuv420Frame compact_output;
    uint16_t next_compact_row;
    H263OutputRowGuard output_row_guard;
    void *output_row_guard_opaque;
    bool pv_ready;

    uint32_t fixed_sample_size;
    uint32_t *sample_sizes;
    uint64_t *chunk_offsets;
    uint32_t chunk_count;
    StscEntry *stsc;
    uint32_t stsc_count;
    SttsEntry *stts;
    uint32_t stts_count;

    uint32_t sample_index;
    uint32_t chunk_index;
    uint32_t sample_in_chunk;
    uint64_t sample_offset;
    uint32_t samples_in_chunk;
    uint32_t stsc_index;
    uint32_t stts_index;
    uint32_t stts_remaining;
    uint64_t timestamp;
    AviState avi;
};

struct H263AviPcmReader {
    H2633gpInfo info;
    AviState avi;
    uint32_t chunk_remaining;
    bool chunk_has_padding;
};

static void clearOutputFrames(H2633gpDecoder *decoder) {
    for (uint8_t index = 0; index < 2; ++index) {
        free(decoder->output_y[index]);
        free(decoder->output_u[index]);
        free(decoder->output_v[index]);
        decoder->output_y[index] = NULL;
        decoder->output_u[index] = NULL;
        decoder->output_v[index] = NULL;
    }
    decoder->output_bytes = 0;
    decoder->allocated_output_count = 0;
    decoder->output_count = 0;
}

static void clearCompactFrame(CompactYuv420Frame *frame) {
    if (!frame) return;
    free(frame->y.data);
    free(frame->y.correction);
    free(frame->u.data);
    free(frame->u.correction);
    free(frame->v.data);
    free(frame->v.correction);
    memset(frame, 0, sizeof(*frame));
}

static void clearCompactReference(H2633gpDecoder *decoder) {
    clearCompactFrame(&decoder->compact_reference);
    clearCompactFrame(&decoder->compact_output);
    decoder->compact_reference_enabled = false;
    decoder->next_compact_row = 0;
}

static void clearDecoder(H2633gpDecoder *decoder) {
    uint8_t requested_output_count = decoder->requested_output_count;
    if (requested_output_count != 1 && requested_output_count != 2)
        requested_output_count = 1;
    if (decoder->pv_ready) PVCleanUpVideoDecoder(&decoder->controls);
    free(decoder->packet);
    clearOutputFrames(decoder);
    clearCompactReference(decoder);
    free(decoder->stsc);
    free(decoder->stts);
    free(decoder->sample_sizes);
    free(decoder->chunk_offsets);
    memset(decoder, 0, sizeof(*decoder));
    decoder->requested_output_count = requested_output_count;
    resetAviState(&decoder->avi);
}

static bool initializeCompactPlane(CompactYuv420Plane *plane,
                                   int width, int height,
                                   uint8_t bits) {
    const size_t stride =
        compact_yuv420_packed_stride(width, bits);
    const size_t correction_stride =
        compact_yuv420_correction_stride(width);
    const size_t data_bytes =
        compact_yuv420_plane_storage_bytes(width, height, bits);
    const size_t correction_bytes =
        compact_yuv420_plane_correction_bytes(width, height);
    if (!plane || !stride || !correction_stride ||
        stride > INT_MAX || correction_stride > INT_MAX) {
        return false;
    }
    plane->data = (uint8_t *)(malloc(data_bytes));
    plane->correction = (int8_t *)(malloc(correction_bytes));
    if (!plane->data || !plane->correction) return false;
    plane->width = width;
    plane->height = height;
    plane->stride = (int)(stride);
    plane->correction_stride = (int)(correction_stride);
    plane->bits = bits;
    return true;
}

static bool initializeCompactFrame(CompactYuv420Frame *reference,
                                   int width, int height) {
    const int chroma_width = (width + 1) / 2;
    const int chroma_height = (height + 1) / 2;
    reference->width = width;
    reference->height = height;
    if (!initializeCompactPlane(
            &reference->y, width, height,
            COMPACT_YUV420_LUMA_BITS) ||
        !initializeCompactPlane(
            &reference->u, chroma_width, chroma_height,
            COMPACT_YUV420_CHROMA_BITS) ||
        !initializeCompactPlane(
            &reference->v, chroma_width, chroma_height,
            COMPACT_YUV420_CHROMA_BITS)) {
        clearCompactFrame(reference);
        return false;
    }
    return true;
}

static bool initializeCompactFrames(H2633gpDecoder *decoder,
                                    int width, int height) {
    if (!initializeCompactFrame(
            &decoder->compact_reference, width, height) ||
        !initializeCompactFrame(
            &decoder->compact_output, width, height)) {
        clearCompactReference(decoder);
        return false;
    }
    decoder->compact_reference_enabled = true;
    return true;
}

static void copyCompactBlock8(CompactYuv420Plane *destination,
                              const CompactYuv420Plane *source,
                              int x, int y) {
    const size_t row_bytes = (size_t)(8 * destination->bits) >> 3;
    const size_t row_offset = ((size_t)x * destination->bits) >> 3;
    for (int row = 0; row < 8; ++row) {
        memcpy(destination->data +
                   (size_t)(y + row) * destination->stride + row_offset,
               source->data +
                   (size_t)(y + row) * source->stride + row_offset,
               row_bytes);
    }
    destination->correction[
        (y / 8) * destination->correction_stride + x / 8] =
        source->correction[
            (y / 8) * source->correction_stride + x / 8];
}

static void packCompactBlock8(CompactYuv420Plane *destination,
                              int destination_x, int destination_y,
                              const uint8_t *source, int source_stride,
                              int source_x, int source_y) {
    int residual_sum = 0;
    const size_t packed_x =
        ((size_t)destination_x * destination->bits) >> 3;
    for (int row = 0; row < 8; ++row) {
        compact_yuv420_pack_aligned_samples(
            destination->data +
                (size_t)(destination_y + row) * destination->stride +
                packed_x,
            source + (size_t)(source_y + row) * source_stride + source_x,
            8, destination->bits, &residual_sum, NULL);
    }
    destination->correction[
        (destination_y / 8) * destination->correction_stride +
        destination_x / 8] =
        compact_yuv420_error_q4(residual_sum);
}

static void packCompactMacroblockRow(H2633gpDecoder *decoder,
                                     uint16_t first_y) {
    H263_PROFILE_START(pack_start);
    CompactYuv420Frame *output = &decoder->compact_output;
    const CompactYuv420Frame *reference = &decoder->compact_reference;
    const int macroblock_row = first_y / 16;
    const int macroblocks = decoder->buffer_width / 16;
    const int chroma_stride = decoder->buffer_width / 2;
    for (int macroblock_x = 0;
         macroblock_x < macroblocks; ++macroblock_x) {
        const int luma_x = macroblock_x * 16;
        const int chroma_x = macroblock_x * 8;
        if (PVIsMacroblockSkipped(
                &decoder->controls,
                macroblock_row * macroblocks + macroblock_x)) {
            copyCompactBlock8(
                &output->y, &reference->y, luma_x, first_y);
            copyCompactBlock8(
                &output->y, &reference->y, luma_x + 8, first_y);
            copyCompactBlock8(
                &output->y, &reference->y, luma_x, first_y + 8);
            copyCompactBlock8(
                &output->y, &reference->y, luma_x + 8, first_y + 8);
            copyCompactBlock8(
                &output->u, &reference->u, chroma_x, first_y / 2);
            copyCompactBlock8(
                &output->v, &reference->v, chroma_x, first_y / 2);
        } else {
            packCompactBlock8(
                &output->y, luma_x, first_y,
                decoder->output_y[0], decoder->buffer_width,
                luma_x, 0);
            packCompactBlock8(
                &output->y, luma_x + 8, first_y,
                decoder->output_y[0], decoder->buffer_width,
                luma_x + 8, 0);
            packCompactBlock8(
                &output->y, luma_x, first_y + 8,
                decoder->output_y[0], decoder->buffer_width,
                luma_x, 8);
            packCompactBlock8(
                &output->y, luma_x + 8, first_y + 8,
                decoder->output_y[0], decoder->buffer_width,
                luma_x + 8, 8);
            packCompactBlock8(
                &output->u, chroma_x, first_y / 2,
                decoder->output_u[0], chroma_stride,
                chroma_x, 0);
            packCompactBlock8(
                &output->v, chroma_x, first_y / 2,
                decoder->output_v[0], chroma_stride,
                chroma_x, 0);
        }
    }
    H263_PROFILE_ADD(decoder, packing_cycles, pack_start);
}

static void compactOutputRowGuard(void *opaque, uint16 first_y) {
    H2633gpDecoder *decoder = (H2633gpDecoder *)(opaque);
    if (!decoder) return;
    while (decoder->next_compact_row < first_y &&
           decoder->next_compact_row < decoder->buffer_height) {
        packCompactMacroblockRow(
            decoder, decoder->next_compact_row);
        decoder->next_compact_row += 16;
    }
    if (decoder->output_row_guard) {
        decoder->output_row_guard(
            decoder->output_row_guard_opaque, first_y);
    }
}

static void copyCompactFrame(CompactYuv420Frame *destination,
                             const CompactYuv420Frame *source) {
    CompactYuv420Plane *destination_planes[3] = {
        &destination->y, &destination->u, &destination->v};
    const CompactYuv420Plane *source_planes[3] = {
        &source->y, &source->u, &source->v};
    for (int plane = 0; plane < 3; ++plane) {
        const size_t data_bytes =
            (size_t)(source_planes[plane]->stride) *
            source_planes[plane]->height;
        const size_t correction_bytes =
            (size_t)(source_planes[plane]->correction_stride) *
            ((source_planes[plane]->height + 7) / 8);
        memcpy(destination_planes[plane]->data,
               source_planes[plane]->data, data_bytes);
        memcpy(destination_planes[plane]->correction,
               source_planes[plane]->correction,
               correction_bytes);
    }
}

static void resetPcmReader(H263AviPcmReader *reader) {
    memset(reader, 0, sizeof(*reader));
    resetAviState(&reader->avi);
}

static int refillPacketBuffer(uint8 *buffer, int bytes_required,
                              void *opaque) {
    H2633gpDecoder *decoder = (H2633gpDecoder *)(opaque);
    H263_PROFILE_START(input_start);
    if (!decoder || !decoder->stream_file || !buffer ||
        bytes_required <= 0 || !decoder->stream_remaining) {
        return 0;
    }
    const size_t wanted = MIN(
        (size_t)(bytes_required),
        (size_t)(decoder->stream_remaining));
    const size_t bytes_read =
        fread(buffer, 1, wanted, decoder->stream_file);
    decoder->stream_remaining -= (uint32_t)(bytes_read);
    if (bytes_read != wanted) decoder->stream_io_error = true;
    if (bytes_read <= decoder->packet_capacity) {
        const size_t padding = MIN(
            (size_t)(kInputPadding),
            decoder->packet_capacity + kInputPadding - bytes_read);
        memset(buffer + bytes_read, 0, padding);
    }
    H263_PROFILE_ADD(decoder, input_cycles, input_start);
    H263_PROFILE_COUNT(decoder, input_refills, 1);
    H263_PROFILE_COUNT(decoder, input_bytes, bytes_read);
    return (int)(bytes_read);
}

static bool skipAviChunk(FILE *file, uint64_t data_start, uint32_t size) {
    const uint64_t end =
        data_start + (uint64_t)(size) + (size & 1U);
    return end >= data_start && seekFile(file, end);
}

static bool readAviChunkHeader(FILE *file, uint32_t *id, uint32_t *size,
                        uint64_t *data_start) {
    return readLe32(file, id) && readLe32(file, size) &&
           tellFile(file, data_start);
}

typedef struct AviStreamHeader {
    uint32_t type;
    uint32_t handler;
    uint32_t scale;
    uint32_t rate;
    uint32_t length;
    uint32_t suggested_buffer;
} AviStreamHeader;

static int parseAviStreamList(FILE *file, uint64_t end, uint8_t stream_index,
                       H2633gpInfo *info, AviState *avi) {
    AviStreamHeader stream = {0};
    uint8_t format[40 + kMpeg4VolHeaderBytes] = {0};
    size_t format_size = 0;
    uint32_t format_chunk_size = 0;

    uint64_t cursor = 0;
    while (tellFile(file, &cursor) && cursor + 8 <= end) {
        uint32_t id = 0;
        uint32_t size = 0;
        uint64_t data_start = 0;
        if (!readAviChunkHeader(file, &id, &size, &data_start))
            return H263_3GP_ERR_IO;
        if (data_start > end ||
            (uint64_t)(size) > end - data_start) {
            return H263_3GP_ERR_FORMAT;
        }

        if (id == fourccLe('s', 't', 'r', 'h')) {
            uint8_t bytes[56] = {0};
            const size_t wanted = MIN(size, sizeof bytes);
            if (wanted < 48 || !readExact(file, bytes, wanted))
                return H263_3GP_ERR_FORMAT;
            stream.type = readLe32Value(bytes);
            stream.handler = readLe32Value(bytes + 4);
            stream.scale = readLe32Value(bytes + 20);
            stream.rate = readLe32Value(bytes + 24);
            stream.length = readLe32Value(bytes + 32);
            stream.suggested_buffer = readLe32Value(bytes + 36);
        } else if (id == fourccLe('s', 't', 'r', 'f')) {
            format_chunk_size = size;
            format_size = MIN(size, sizeof format);
            if (!readExact(file, format, format_size))
                return H263_3GP_ERR_IO;
        }
        if (!skipAviChunk(file, data_start, size))
            return H263_3GP_ERR_IO;
    }

    const bool is_h263 =
        stream.handler == fourccLe('H', '2', '6', '3') ||
        stream.handler == fourccLe('U', '2', '6', '3') ||
        stream.handler == fourccLe('I', '2', '6', '3');
    const bool is_mpeg4_simple =
        stream.handler == fourccLe('M', '4', 'S', '2');
    if (stream.type == fourccLe('v', 'i', 'd', 's') &&
        (is_h263 || is_mpeg4_simple)) {
        if (!stream.scale || !stream.rate || format_size < 20)
            return H263_3GP_ERR_FORMAT;
        const uint32_t compression = readLe32Value(format + 16);
        const bool compression_is_h263 =
            compression == fourccLe('H', '2', '6', '3') ||
            compression == fourccLe('U', '2', '6', '3') ||
            compression == fourccLe('I', '2', '6', '3');
        const bool compression_is_mpeg4_simple =
            compression == fourccLe('M', '4', 'S', '2');
        if ((is_h263 && !compression_is_h263) ||
            (is_mpeg4_simple && !compression_is_mpeg4_simple)) {
            return H263_3GP_ERR_UNSUPPORTED;
        }
        const uint32_t width = readLe32Value(format + 4);
        const int32_t signed_height =
            (int32_t)(readLe32Value(format + 8));
        const uint32_t height =
            signed_height < 0
                ? (uint32_t)(-(int64_t)(
                      signed_height))
                : (uint32_t)(signed_height);
        if (!width || !height || width > UINT16_MAX ||
            height > UINT16_MAX) {
            return H263_3GP_ERR_UNSUPPORTED;
        }
        avi->video_stream = stream_index;
        avi->video_scale = stream.scale;
        avi->video_rate = stream.rate;
        avi->video_length = stream.length;
        info->width = (uint16_t)(width);
        info->height = (uint16_t)(height);
        info->max_sample_size = stream.suggested_buffer;
        info->video_codec =
            is_mpeg4_simple
                ? H263_VIDEO_CODEC_MPEG4_SIMPLE
                : H263_VIDEO_CODEC_H263;
        if (is_mpeg4_simple) {
            /*
             * PacketVideo requires the complete VOL configuration as one
             * contiguous input during initialization. AVI stores that small
             * decoder configuration after BITMAPINFOHEADER, not in a video
             * packet. Retain only this bounded header; frame packets remain
             * sequential through the fixed-size refill buffer below.
             */
            if (format_chunk_size > sizeof format) {
                return H263_3GP_ERR_UNSUPPORTED;
            }
            if (format_size > 40) {
                avi->mpeg4_vol_size =
                    (uint16_t)(format_size - 40);
                memcpy(avi->mpeg4_vol, format + 40,
                       avi->mpeg4_vol_size);
            }
        }
    } else if (stream.type == fourccLe('a', 'u', 'd', 's')) {
        if (format_size < 16) return H263_3GP_ERR_FORMAT;
        avi->audio_stream = stream_index;
        avi->audio_format = readLe16Value(format);
        info->audio_channels =
            (uint8_t)(readLe16Value(format + 2));
        info->audio_sample_rate = readLe32Value(format + 4);
        info->audio_bits_per_sample =
            (uint8_t)(readLe16Value(format + 14));
    }
    return H263_3GP_OK;
}

static int parseAviHeaderList(FILE *file, uint64_t end, H2633gpInfo *info,
                       AviState *avi) {
    uint8_t stream_index = 0;
    uint64_t cursor = 0;
    while (tellFile(file, &cursor) && cursor + 8 <= end) {
        uint32_t id = 0;
        uint32_t size = 0;
        uint64_t data_start = 0;
        if (!readAviChunkHeader(file, &id, &size, &data_start))
            return H263_3GP_ERR_IO;
        if (data_start > end ||
            (uint64_t)(size) > end - data_start) {
            return H263_3GP_ERR_FORMAT;
        }
        if (id == fourccLe('a', 'v', 'i', 'h')) {
            uint8_t header[40] = {0};
            if (size < sizeof header ||
                !readExact(file, header, sizeof header)) {
                return H263_3GP_ERR_FORMAT;
            }
            avi->microseconds_per_frame = readLe32Value(header);
            avi->main_frame_count = readLe32Value(header + 16);
        } else if (id == fourccLe('L', 'I', 'S', 'T')) {
            uint32_t list_type = 0;
            if (size < 4 || !readLe32(file, &list_type))
                return H263_3GP_ERR_FORMAT;
            if (list_type == fourccLe('s', 't', 'r', 'l')) {
                const int result = parseAviStreamList(
                    file, data_start + size, stream_index++, info, avi);
                if (result != H263_3GP_OK) return result;
            }
        }
        if (!skipAviChunk(file, data_start, size))
            return H263_3GP_ERR_IO;
    }
    return H263_3GP_OK;
}

static int scanAviIndex(FILE *file, uint64_t data_start, uint32_t size,
                 H2633gpInfo *info, AviState avi,
                 uint32_t *video_frames) {
    if (size % 16U || !seekFile(file, data_start))
        return H263_3GP_ERR_FORMAT;
    uint8_t entry[16];
    for (uint32_t offset = 0; offset < size; offset += sizeof entry) {
        if (!readExact(file, entry, sizeof entry))
            return H263_3GP_ERR_IO;
        const uint32_t id =
            (uint32_t)(entry[0]) |
            ((uint32_t)(entry[1]) << 8) |
            ((uint32_t)(entry[2]) << 16) |
            ((uint32_t)(entry[3]) << 24);
        const uint32_t packet_size =
            (uint32_t)(entry[12]) |
            ((uint32_t)(entry[13]) << 8) |
            ((uint32_t)(entry[14]) << 16) |
            ((uint32_t)(entry[15]) << 24);
        if (isAviVideoChunk(id, avi.video_stream) && packet_size) {
            ++*video_frames;
            info->max_sample_size =
                MAX(info->max_sample_size, packet_size);
        }
    }
    return H263_3GP_OK;
}

static int scanAviMovie(FILE *file, H2633gpInfo *info, AviState avi,
                 uint32_t *video_frames) {
    uint64_t cursor = avi.movi_start;
    while (cursor + 8 <= avi.movi_end) {
        if (!seekFile(file, cursor)) return H263_3GP_ERR_IO;
        uint32_t id = 0;
        uint32_t size = 0;
        uint64_t data_start = 0;
        if (!readAviChunkHeader(file, &id, &size, &data_start))
            return H263_3GP_ERR_IO;
        if (data_start > avi.movi_end ||
            (uint64_t)(size) > avi.movi_end - data_start) {
            return H263_3GP_ERR_FORMAT;
        }
        if (id == fourccLe('L', 'I', 'S', 'T')) {
            uint32_t list_type = 0;
            if (size < 4 || !readLe32(file, &list_type))
                return H263_3GP_ERR_FORMAT;
            if (list_type == fourccLe('r', 'e', 'c', ' ')) {
                cursor = data_start + 4;
                continue;
            }
        }
        if (isAviVideoChunk(id, avi.video_stream) && size) {
            ++*video_frames;
            info->max_sample_size =
                MAX(info->max_sample_size, size);
        }
        cursor = data_start + size + (size & 1U);
    }
    return H263_3GP_OK;
}

static int finalizeAviInfo(H2633gpInfo *info, AviState *avi,
                    uint32_t indexed_frames) {
    if (avi->video_stream == 0xff || !avi->movi_start ||
        avi->movi_end <= avi->movi_start ||
        !isSupportedVideoGeometry(
            info->video_codec, info->width, info->height)) {
        return H263_3GP_ERR_UNSUPPORTED;
    }
    info->frame_count =
        indexed_frames ? indexed_frames : avi->main_frame_count;
    if (!info->frame_count || !info->max_sample_size)
        return H263_3GP_ERR_FORMAT;

    uint64_t fps_num = 0;
    uint64_t fps_den = 0;
    if (avi->video_rate && avi->video_scale && avi->video_length) {
        fps_num =
            (uint64_t)(avi->video_rate) * info->frame_count;
        fps_den =
            (uint64_t)(avi->video_scale) * avi->video_length;
    } else if (avi->microseconds_per_frame) {
        fps_num = 1000000;
        fps_den = avi->microseconds_per_frame;
    }
    if (!fps_num || !fps_den) return H263_3GP_ERR_FORMAT;
    while (fps_num > UINT32_MAX || fps_den > UINT32_MAX) {
        fps_num = (fps_num + 1U) / 2U;
        fps_den = (fps_den + 1U) / 2U;
    }
    const uint32_t divisor =
        gcd32((uint32_t)(fps_num),
              (uint32_t)(fps_den));
    info->fps_num = (uint32_t)(fps_num) / divisor;
    info->fps_den = (uint32_t)(fps_den) / divisor;
    if (!info->fps_num || !info->fps_den || info->fps_num > 30U)
        return H263_3GP_ERR_UNSUPPORTED;
    info->timescale = info->fps_num;
    info->duration_ticks =
        (uint64_t)(info->frame_count) * info->fps_den;
    info->profile = 0;
    info->level = 0;
    info->container = H263_CONTAINER_AVI;
    avi->next_video_offset = avi->movi_start;
    avi->next_audio_offset = avi->movi_start;

    if (avi->audio_stream != 0xff &&
        (avi->audio_format != 1 || info->audio_channels != 1 ||
         info->audio_sample_rate != 8000 ||
         (info->audio_bits_per_sample != 8 &&
          info->audio_bits_per_sample != 16))) {
        return H263_3GP_ERR_UNSUPPORTED;
    }
    return H263_3GP_OK;
}

static int parseAviContainer(FILE *file, H2633gpInfo *info, AviState *avi) {
    memset(info, 0, sizeof(*info));
    resetAviState(avi);
    if (!seekFile(file, 0))
        return H263_3GP_ERR_IO;
    uint32_t riff = 0;
    uint32_t riff_size = 0;
    uint32_t type = 0;
    if (!readLe32(file, &riff) || !readLe32(file, &riff_size) ||
        !readLe32(file, &type)) {
        return H263_3GP_ERR_IO;
    }
    const uint64_t riff_end = 8ULL + riff_size;
    if (riff != fourccLe('R', 'I', 'F', 'F') ||
        type != fourccLe('A', 'V', 'I', ' ') ||
        riff_end < 12) {
        return H263_3GP_ERR_FORMAT;
    }

    uint64_t cursor = 12;
    uint64_t index_start = 0;
    uint32_t index_size = 0;
    bool header_is_complete = false;
    while (cursor + 8 <= riff_end) {
        if (!seekFile(file, cursor)) return H263_3GP_ERR_IO;
        uint32_t id = 0;
        uint32_t size = 0;
        uint64_t data_start = 0;
        if (!readAviChunkHeader(file, &id, &size, &data_start))
            return H263_3GP_ERR_IO;
        if (data_start > riff_end ||
            (uint64_t)(size) > riff_end - data_start) {
            return H263_3GP_ERR_FORMAT;
        }
        if (id == fourccLe('L', 'I', 'S', 'T')) {
            uint32_t list_type = 0;
            if (size < 4 || !readLe32(file, &list_type))
                return H263_3GP_ERR_FORMAT;
            if (list_type == fourccLe('h', 'd', 'r', 'l')) {
                const int result = parseAviHeaderList(
                    file, data_start + size, info, avi);
                if (result != H263_3GP_OK) return result;
            } else if (list_type == fourccLe('m', 'o', 'v', 'i')) {
                avi->movi_start = data_start + 4;
                avi->movi_end = data_start + size;
                /*
                 * A standard AVI header already supplies the frame count and
                 * maximum packet size.  Avoid seeking across the entire movi
                 * list merely to rescan idx1: on FAT that seek walks every
                 * cluster in a large file and can take tens of seconds over
                 * an emulated or slow SPI card.
                 */
                if (avi->video_stream != 0xff &&
                    (avi->video_length || avi->main_frame_count) &&
                    info->max_sample_size) {
                    header_is_complete = true;
                    break;
                }
            }
        } else if (id == fourccLe('i', 'd', 'x', '1')) {
            index_start = data_start;
            index_size = size;
        }
        cursor = data_start + size + (size & 1U);
    }

    uint32_t video_frames = 0;
    int result = H263_3GP_OK;
    if (!header_is_complete && index_start && avi->video_stream != 0xff) {
        result = scanAviIndex(
            file, index_start, index_size, info, *avi, &video_frames);
    } else if (!header_is_complete && avi->movi_start) {
        result = scanAviMovie(file, info, *avi, &video_frames);
    }
    if (result == H263_3GP_OK)
        result = finalizeAviInfo(info, avi, video_frames);
    return result;
}

static int nextAviPayload(FILE *file, AviState avi, bool video,
                   uint64_t *offset, uint32_t *size) {
    if (!file || !offset || !size) return H263_3GP_ERR_ARGUMENT;
    while (*offset + 8 <= avi.movi_end) {
        if (!seekFile(file, *offset)) return H263_3GP_ERR_IO;
        uint32_t id = 0;
        uint64_t data_start = 0;
        if (!readAviChunkHeader(file, &id, size, &data_start))
            return H263_3GP_ERR_IO;
        if (data_start > avi.movi_end ||
            (uint64_t)(*size) > avi.movi_end - data_start) {
            return H263_3GP_ERR_FORMAT;
        }
        if (id == fourccLe('L', 'I', 'S', 'T')) {
            uint32_t list_type = 0;
            if (*size < 4 || !readLe32(file, &list_type))
                return H263_3GP_ERR_FORMAT;
            if (list_type == fourccLe('r', 'e', 'c', ' ')) {
                *offset = data_start + 4;
                continue;
            }
        }
        const bool wanted =
            video ? isAviVideoChunk(id, avi.video_stream)
                  : isAviAudioChunk(id, avi.audio_stream);
        *offset = data_start + *size + (*size & 1U);
        if (wanted && *size) {
            return seekFile(file, data_start)
                       ? H263_3GP_OK
                       : H263_3GP_ERR_IO;
        }
    }
    return H263_3GP_EOF;
}

static int parseMediaHeader(FILE *file, Box mdhd, H2633gpInfo *info) {
    if (!seekFile(file, mdhd.data)) return H263_3GP_ERR_IO;
    uint32_t version_flags = 0;
    if (!readU32(file, &version_flags)) return H263_3GP_ERR_IO;
    const uint8_t version = (uint8_t)(version_flags >> 24);
    if (version == 0) {
        uint32_t ignored = 0;
        uint32_t duration = 0;
        if (!readU32(file, &ignored) || !readU32(file, &ignored) ||
            !readU32(file, &info->timescale) ||
            !readU32(file, &duration)) {
            return H263_3GP_ERR_IO;
        }
        info->duration_ticks = duration;
    } else if (version == 1) {
        uint64_t ignored = 0;
        if (!readU64(file, &ignored) || !readU64(file, &ignored) ||
            !readU32(file, &info->timescale) ||
            !readU64(file, &info->duration_ticks)) {
            return H263_3GP_ERR_IO;
        }
    } else {
        return H263_3GP_ERR_UNSUPPORTED;
    }
    return info->timescale ? H263_3GP_OK : H263_3GP_ERR_FORMAT;
}

static int parseSampleDescription(FILE *file, Box stsd, H2633gpInfo *info) {
    if (!seekFile(file, stsd.data + 4)) return H263_3GP_ERR_IO;
    uint32_t count = 0;
    if (!readU32(file, &count)) return H263_3GP_ERR_IO;
    uint64_t cursor = stsd.data + 8;
    for (uint32_t index = 0; index < count && cursor < stsd.end; ++index) {
        if (!seekFile(file, cursor)) return H263_3GP_ERR_IO;
        Box entry;
        if (!readBox(file, stsd.end, &entry)) return H263_3GP_ERR_FORMAT;
        if (entry.type == fourcc('s', '2', '6', '3')) {
            if (entry.end - entry.data < 78 ||
                !seekFile(file, entry.data + 24) ||
                !readU16(file, &info->width) ||
                !readU16(file, &info->height)) {
                return H263_3GP_ERR_FORMAT;
            }
            info->profile = 0;
            info->level = 0;
            info->video_codec = H263_VIDEO_CODEC_H263;
            Box d263;
            if (findChildFrom(file, entry, fourcc('d', '2', '6', '3'),
                              &d263, entry.data + 78) &&
                d263.end - d263.data >= 7 &&
                seekFile(file, d263.data + 5)) {
                uint8_t values[2];
                if (!readExact(file, values, sizeof values))
                    return H263_3GP_ERR_IO;
                info->level = values[0];
                info->profile = values[1];
            }
            return H263_3GP_OK;
        }
        cursor = entry.end;
    }
    return H263_3GP_ERR_UNSUPPORTED;
}

static int parseStsz(FILE *file, Box stsz, H2633gpDecoder *decoder) {
    if (!seekFile(file, stsz.data + 4) ||
        !readU32(file, &decoder->fixed_sample_size) ||
        !readU32(file, &decoder->info.frame_count)) {
        return H263_3GP_ERR_IO;
    }
    if (decoder->info.frame_count == 0) return H263_3GP_ERR_FORMAT;
    const uint64_t entries = stsz.data + 12;
    if (decoder->fixed_sample_size != 0) {
        decoder->info.max_sample_size = decoder->fixed_sample_size;
        return H263_3GP_OK;
    }
    if (stsz.end - entries <
        (uint64_t)(decoder->info.frame_count) * 4) {
        return H263_3GP_ERR_FORMAT;
    }
    if (decoder->info.frame_count >
        SIZE_MAX / sizeof(uint32_t)) {
        return H263_3GP_ERR_FORMAT;
    }
    decoder->sample_sizes = (uint32_t *)(malloc(
        (size_t)(decoder->info.frame_count) *
        sizeof(uint32_t)));
    if (!decoder->sample_sizes) return H263_3GP_ERR_MEMORY;
    if (!seekFile(file, entries)) return H263_3GP_ERR_IO;
    for (uint32_t i = 0; i < decoder->info.frame_count; ++i) {
        uint32_t size = 0;
        if (!readU32(file, &size)) return H263_3GP_ERR_IO;
        decoder->sample_sizes[i] = size;
        decoder->info.max_sample_size =
            MAX(decoder->info.max_sample_size, size);
    }
    return decoder->info.max_sample_size ? H263_3GP_OK
                                         : H263_3GP_ERR_FORMAT;
}

static int parseChunks(FILE *file, Box box, H2633gpDecoder *decoder) {
    if (!seekFile(file, box.data + 4) ||
        !readU32(file, &decoder->chunk_count)) {
        return H263_3GP_ERR_IO;
    }
    if (decoder->chunk_count == 0) return H263_3GP_ERR_FORMAT;
    const bool offsets_are_64_bit =
        box.type == fourcc('c', 'o', '6', '4');
    const uint64_t entries = box.data + 8;
    const uint64_t entry_size = offsets_are_64_bit ? 8 : 4;
    if (box.end - entries <
        (uint64_t)(decoder->chunk_count) * entry_size ||
        decoder->chunk_count >
            SIZE_MAX / sizeof(uint64_t)) {
        return H263_3GP_ERR_FORMAT;
    }
    decoder->chunk_offsets = (uint64_t *)(malloc(
        (size_t)(decoder->chunk_count) *
        sizeof(uint64_t)));
    if (!decoder->chunk_offsets) return H263_3GP_ERR_MEMORY;
    if (!seekFile(file, entries)) return H263_3GP_ERR_IO;
    for (uint32_t i = 0; i < decoder->chunk_count; ++i) {
        if (offsets_are_64_bit) {
            if (!readU64(file, &decoder->chunk_offsets[i]))
                return H263_3GP_ERR_IO;
        } else {
            uint32_t offset = 0;
            if (!readU32(file, &offset)) return H263_3GP_ERR_IO;
            decoder->chunk_offsets[i] = offset;
        }
    }
    return H263_3GP_OK;
}

static int parseStsc(FILE *file, Box stsc, H2633gpDecoder *decoder) {
    if (!seekFile(file, stsc.data + 4) ||
        !readU32(file, &decoder->stsc_count)) {
        return H263_3GP_ERR_IO;
    }
    if (decoder->stsc_count == 0 ||
        decoder->stsc_count >
            SIZE_MAX / sizeof(StscEntry)) {
        return H263_3GP_ERR_FORMAT;
    }
    decoder->stsc = (StscEntry *)(
        calloc(decoder->stsc_count, sizeof(StscEntry)));
    if (!decoder->stsc) return H263_3GP_ERR_MEMORY;
    if (stsc.end - (stsc.data + 8) <
        (uint64_t)(decoder->stsc_count) * 12) {
        return H263_3GP_ERR_FORMAT;
    }
    for (uint32_t i = 0; i < decoder->stsc_count; ++i) {
        uint32_t ignored = 0;
        if (!readU32(file, &decoder->stsc[i].first_chunk) ||
            !readU32(file, &decoder->stsc[i].samples_per_chunk) ||
            !readU32(file, &ignored)) {
            return H263_3GP_ERR_IO;
        }
        if (decoder->stsc[i].first_chunk == 0 ||
            decoder->stsc[i].samples_per_chunk == 0 ||
            (i && decoder->stsc[i].first_chunk <=
                      decoder->stsc[i - 1].first_chunk)) {
            return H263_3GP_ERR_FORMAT;
        }
    }
    return decoder->stsc[0].first_chunk == 1 ? H263_3GP_OK
                                             : H263_3GP_ERR_FORMAT;
}

static int parseStts(FILE *file, Box stts, H2633gpDecoder *decoder) {
    if (!seekFile(file, stts.data + 4) ||
        !readU32(file, &decoder->stts_count)) {
        return H263_3GP_ERR_IO;
    }
    if (decoder->stts_count != 1 ||
        decoder->stts_count >
            SIZE_MAX / sizeof(SttsEntry)) {
        return H263_3GP_ERR_UNSUPPORTED;
    }
    decoder->stts = (SttsEntry *)(
        calloc(decoder->stts_count, sizeof(SttsEntry)));
    if (!decoder->stts) return H263_3GP_ERR_MEMORY;
    if (stts.end - (stts.data + 8) <
        (uint64_t)(decoder->stts_count) * 8) {
        return H263_3GP_ERR_FORMAT;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < decoder->stts_count; ++i) {
        if (!readU32(file, &decoder->stts[i].sample_count) ||
            !readU32(file, &decoder->stts[i].sample_delta)) {
            return H263_3GP_ERR_IO;
        }
        if (decoder->stts[i].sample_count == 0 ||
            decoder->stts[i].sample_delta == 0) {
            return H263_3GP_ERR_FORMAT;
        }
        total += decoder->stts[i].sample_count;
    }
    if (total != decoder->info.frame_count) return H263_3GP_ERR_FORMAT;
    decoder->stts_remaining = decoder->stts[0].sample_count;
    const uint32_t divisor =
        gcd32(decoder->info.timescale, decoder->stts[0].sample_delta);
    decoder->info.fps_num = decoder->info.timescale / divisor;
    decoder->info.fps_den = decoder->stts[0].sample_delta / divisor;
    return H263_3GP_OK;
}

static int parseTrack(FILE *file, Box trak, H2633gpDecoder *decoder) {
    Box mdia;
    Box hdlr;
    if (!findChild(file, trak, fourcc('m', 'd', 'i', 'a'), &mdia) ||
        !findChild(file, mdia, fourcc('h', 'd', 'l', 'r'), &hdlr)) {
        return H263_3GP_ERR_FORMAT;
    }
    if (!seekFile(file, hdlr.data + 8)) return H263_3GP_ERR_IO;
    uint32_t handler = 0;
    if (!readU32(file, &handler)) return H263_3GP_ERR_IO;
    if (handler != fourcc('v', 'i', 'd', 'e'))
        return H263_3GP_ERR_UNSUPPORTED;

    Box mdhd;
    Box minf;
    Box stbl;
    Box stsd;
    Box stsz;
    Box stsc;
    Box stts;
    Box chunks;
    if (!findChild(file, mdia, fourcc('m', 'd', 'h', 'd'), &mdhd) ||
        !findChild(file, mdia, fourcc('m', 'i', 'n', 'f'), &minf) ||
        !findChild(file, minf, fourcc('s', 't', 'b', 'l'), &stbl) ||
        !findChild(file, stbl, fourcc('s', 't', 's', 'd'), &stsd) ||
        !findChild(file, stbl, fourcc('s', 't', 's', 'z'), &stsz) ||
        !findChild(file, stbl, fourcc('s', 't', 's', 'c'), &stsc) ||
        !findChild(file, stbl, fourcc('s', 't', 't', 's'), &stts)) {
        return H263_3GP_ERR_FORMAT;
    }
    if (!findChild(file, stbl, fourcc('s', 't', 'c', 'o'), &chunks) &&
        !findChild(file, stbl, fourcc('c', 'o', '6', '4'), &chunks)) {
        return H263_3GP_ERR_FORMAT;
    }

    int result = parseMediaHeader(file, mdhd, &decoder->info);
    if (result == H263_3GP_OK)
        result = parseSampleDescription(file, stsd, &decoder->info);
    if (result == H263_3GP_OK) result = parseStsz(file, stsz, decoder);
    if (result == H263_3GP_OK) result = parseChunks(file, chunks, decoder);
    if (result == H263_3GP_OK) result = parseStsc(file, stsc, decoder);
    if (result == H263_3GP_OK) result = parseStts(file, stts, decoder);
    return result;
}

static int parseContainer(FILE *file, H2633gpDecoder *decoder) {
    uint64_t size = 0;
    if (!fileSize(file, &size)) return H263_3GP_ERR_IO;
    Box root;
    root.data = 0;
    root.end = size;
    Box ftyp;
    Box moov;
    if (!findChild(file, root, fourcc('f', 't', 'y', 'p'), &ftyp) ||
        !findChild(file, root, fourcc('m', 'o', 'o', 'v'), &moov)) {
        return H263_3GP_ERR_FORMAT;
    }
    uint64_t cursor = moov.data;
    bool saw_video = false;
    while (cursor < moov.end) {
        if (!seekFile(file, cursor)) return H263_3GP_ERR_IO;
        Box child;
        if (!readBox(file, moov.end, &child)) return H263_3GP_ERR_FORMAT;
        if (child.type == fourcc('t', 'r', 'a', 'k')) {
            const int result = parseTrack(file, child, decoder);
            if (result == H263_3GP_OK) {
                saw_video = true;
                break;
            }
            if (result != H263_3GP_ERR_UNSUPPORTED) return result;
        }
        cursor = child.end;
    }
    if (!saw_video) return H263_3GP_ERR_UNSUPPORTED;
    if (!isSupportedH263Geometry(
            decoder->info.width, decoder->info.height) ||
        decoder->info.profile != 0) {
        return H263_3GP_ERR_UNSUPPORTED;
    }
    decoder->info.container = H263_CONTAINER_3GP;
    return H263_3GP_OK;
}

static int sampleSize(const H2633gpDecoder *decoder, uint32_t index,
               uint32_t *size) {
    if (decoder->fixed_sample_size) {
        *size = decoder->fixed_sample_size;
        return H263_3GP_OK;
    }
    if (!decoder->sample_sizes || index >= decoder->info.frame_count)
        return H263_3GP_ERR_FORMAT;
    *size = decoder->sample_sizes[index];
    return *size ? H263_3GP_OK : H263_3GP_ERR_FORMAT;
}

static int beginChunk(H2633gpDecoder *decoder) {
    if (decoder->chunk_index >= decoder->chunk_count)
        return H263_3GP_ERR_FORMAT;
    while (decoder->stsc_index + 1 < decoder->stsc_count &&
           decoder->chunk_index + 1 >=
               decoder->stsc[decoder->stsc_index + 1].first_chunk) {
        ++decoder->stsc_index;
    }
    decoder->samples_in_chunk =
        decoder->stsc[decoder->stsc_index].samples_per_chunk;
    if (!decoder->chunk_offsets) return H263_3GP_ERR_FORMAT;
    decoder->sample_offset =
        decoder->chunk_offsets[decoder->chunk_index];
    return H263_3GP_OK;
}

static int initializeDecoder(H2633gpDecoder *decoder, FILE *file) {
    const int32 expected_width =
        ((int32)(decoder->info.width) + 15) & -16;
    const int32 expected_height =
        ((int32)(decoder->info.height) + 15) & -16;
    decoder->buffer_width = (uint16_t)(expected_width);
    decoder->buffer_height = (uint16_t)(expected_height);
    decoder->intra_only =
        decoder->info.video_codec == H263_VIDEO_CODEC_H263 &&
        decoder->info.width != 176;
    decoder->compact_reference_enabled =
        decoder->info.video_codec ==
            H263_VIDEO_CODEC_MPEG4_SIMPLE &&
        decoder->requested_output_count == 1;
    decoder->output_count =
        decoder->compact_reference_enabled
            ? 2
            : decoder->info.video_codec ==
                      H263_VIDEO_CODEC_MPEG4_SIMPLE
                  ? decoder->requested_output_count
            : (decoder->intra_only
                   ? decoder->requested_output_count
                   : 2);
    decoder->allocated_output_count =
        decoder->compact_reference_enabled ? 1 : decoder->output_count;

    /*
     * MPEG-4 reconstructs one 16-line macroblock row at a time and commits
     * it to compact_output before the rolling planes are reused. The two
     * compact frames are the predictive reference and the displayable
     * current frame; no full byte-planar MPEG-4 frame is retained.
     *
     * Other modes reserve ordinary frame planes before PacketVideo makes
     * its smaller table allocations. Separate Y/U/V blocks avoid requiring
     * one contiguous 152,064-byte allocation at CIF.
     */
    const size_t y_bytes = (size_t)(expected_width) *
        (decoder->compact_reference_enabled ? 16 : expected_height);
    const size_t chroma_bytes = y_bytes / 4;
    decoder->output_bytes = y_bytes + 2 * chroma_bytes;
    if (decoder->compact_reference_enabled &&
        !initializeCompactFrames(
            decoder, expected_width, expected_height)) {
        return H263_3GP_ERR_FRAME_MEMORY;
    }
    for (uint8_t i = 0; i < decoder->allocated_output_count; ++i) {
        decoder->output_y[i] =
            (uint8_t *)(malloc(y_bytes));
        decoder->output_u[i] =
            (uint8_t *)(malloc(chroma_bytes));
        decoder->output_v[i] =
            (uint8_t *)(malloc(chroma_bytes));
        if (!decoder->output_y[i] || !decoder->output_u[i] ||
            !decoder->output_v[i]) {
            return H263_3GP_ERR_FRAME_MEMORY;
        }
        memset(decoder->output_y[i], 0, y_bytes);
        memset(decoder->output_u[i], 0, chroma_bytes);
        memset(decoder->output_v[i], 0, chroma_bytes);
    }

    uint8 *vol_data[1] = {NULL};
    int32 vol_size[1] = {0};
    if (decoder->info.video_codec ==
        H263_VIDEO_CODEC_MPEG4_SIMPLE) {
        if (decoder->avi.mpeg4_vol_size) {
            vol_data[0] = decoder->avi.mpeg4_vol;
            vol_size[0] = decoder->avi.mpeg4_vol_size;
        } else {
            /*
             * FFmpeg's AVI muxer writes the VOL at the start of the first
             * video packet instead of BITMAPINFOHEADER extradata. Feed that
             * packet through the same fixed-size refill path used for every
             * frame; next_video_offset is deliberately left unchanged so the
             * first VOP is decoded normally after initialization.
             */
            uint64_t first_offset = decoder->avi.next_video_offset;
            uint32_t first_size = 0;
            const int payload_result = nextAviPayload(
                file, decoder->avi, true, &first_offset, &first_size);
            if (payload_result != H263_3GP_OK || !first_size ||
                first_size > decoder->info.max_sample_size) {
                return payload_result == H263_3GP_ERR_IO
                           ? H263_3GP_ERR_IO
                           : H263_3GP_ERR_FORMAT;
            }
            const size_t initial_size = MIN(
                (size_t)(first_size), decoder->packet_capacity);
            if (!readExact(file, decoder->packet, initial_size)) {
                return H263_3GP_ERR_IO;
            }
            decoder->stream_file = file;
            decoder->stream_remaining =
                first_size - (uint32_t)(initial_size);
            decoder->stream_io_error = false;
            memset(decoder->packet + initial_size, 0, kInputPadding);
            vol_data[0] = decoder->packet;
            vol_size[0] = (int32)(initial_size);
        }
    }
    if (!PVInitVideoDecoder(&decoder->controls, vol_data, vol_size, 1,
                            decoder->info.width, decoder->info.height,
                            decoder->info.video_codec ==
                                    H263_VIDEO_CODEC_MPEG4_SIMPLE
                                ? MPEG4_MODE
                                : H263_MODE)) {
        const int result = decoder->stream_io_error
                               ? H263_3GP_ERR_IO
                               : H263_3GP_ERR_DECODER_MEMORY;
        decoder->stream_file = NULL;
        return result;
    }
    decoder->stream_file = NULL;
    if (decoder->stream_io_error) {
        PVCleanUpVideoDecoder(&decoder->controls);
        return H263_3GP_ERR_IO;
    }
    decoder->pv_ready = true;
    if (decoder->info.video_codec == H263_VIDEO_CODEC_MPEG4_SIMPLE) {
        VolInfo vol = {0};
        const bool vol_ok =
            PVGetVolInfo(&decoder->controls, &vol) != 0;
        if (!vol_ok ||
            vol.shortVideoHeader ||
            vol.dataPartitioning ||
            vol.useReverseVLC ||
            vol.scalability ||
            !((vol.profile_level_id >= 0x01 &&
               vol.profile_level_id <= 0x06) ||
              vol.profile_level_id == 0x08 ||
              vol.profile_level_id == 0x09)) {
            return H263_3GP_ERR_UNSUPPORTED;
        }
        decoder->info.profile = 1;
        decoder->info.level = (uint8_t)(vol.profile_level_id);
    }
    PVSetPostProcType(&decoder->controls, PV_NO_POST_PROC);
    int32 buffer_width = 0;
    int32 buffer_height = 0;
    PVGetBufferDimensions(&decoder->controls, &buffer_width, &buffer_height);
    if (buffer_width != expected_width ||
        buffer_height != expected_height ||
        decoder->controls.size != buffer_width * buffer_height ||
        buffer_width > UINT16_MAX || buffer_height > UINT16_MAX) {
        return H263_3GP_ERR_UNSUPPORTED;
    }
    if (decoder->compact_reference_enabled) {
        decoder->controls.currentOutputRows = 16;
        decoder->controls.outputRowGuard = compactOutputRowGuard;
        decoder->controls.outputRowGuardOpaque = decoder;
        PVSetCompactReferenceYUV420(
            &decoder->controls, &decoder->compact_reference);
    } else {
        const uint8_t reference = decoder->output_count - 1;
        PVSetReferenceYUVPlanes(
            &decoder->controls,
            decoder->output_y[reference],
            decoder->output_u[reference],
            decoder->output_v[reference]);
    }
    return H263_3GP_OK;
}


H2633gpDecoder *h263_3gp_decoder_create(void) {
    H2633gpDecoder *decoder =
        (H2633gpDecoder *)calloc(1, sizeof(H2633gpDecoder));
    if (decoder) {
        decoder->requested_output_count = 1;
        resetAviState(&decoder->avi);
    }
    return decoder;
}

void h263_3gp_decoder_destroy(H2633gpDecoder *decoder) {
    if (!decoder) return;
    clearDecoder(decoder);
    free(decoder);
}

int h263_3gp_decoder_set_output_buffer_count(H2633gpDecoder *decoder,
                                              uint8_t count) {
    if (!decoder || (count != 1 && count != 2) || decoder->pv_ready)
        return H263_3GP_ERR_ARGUMENT;
    decoder->requested_output_count = count;
    return H263_3GP_OK;
}

uint8_t h263_3gp_decoder_output_buffer_count(
    const H2633gpDecoder *decoder) {
    return decoder ? decoder->output_count : 0;
}

void h263_3gp_decoder_set_output_row_guard(
    H2633gpDecoder *decoder, H263OutputRowGuard guard, void *opaque) {
    if (!decoder) return;
    decoder->output_row_guard = guard;
    decoder->output_row_guard_opaque = opaque;
    if (decoder->compact_reference_enabled) {
        decoder->controls.outputRowGuard = compactOutputRowGuard;
        decoder->controls.outputRowGuardOpaque = decoder;
    } else {
        decoder->controls.outputRowGuard = guard;
        decoder->controls.outputRowGuardOpaque = opaque;
    }
}

int h263_avi_probe(FILE *file, H2633gpInfo *info) {
    if (!file || !info) return H263_3GP_ERR_ARGUMENT;
    AviState avi = {0};
    resetAviState(&avi);
    return parseAviContainer(file, info, &avi);
}

H263AviPcmReader *h263_avi_pcm_reader_create(void) {
    H263AviPcmReader *reader =
        (H263AviPcmReader *)calloc(1, sizeof(H263AviPcmReader));
    if (reader) resetPcmReader(reader);
    return reader;
}

void h263_avi_pcm_reader_destroy(H263AviPcmReader *reader) {
    free(reader);
}

int h263_avi_pcm_reader_open(H263AviPcmReader *reader, FILE *file,
                             H2633gpInfo *info) {
    if (!reader || !file || !info) return H263_3GP_ERR_ARGUMENT;
    resetPcmReader(reader);
    int result = parseAviContainer(file, &reader->info, &reader->avi);
    if (result == H263_3GP_OK &&
        (reader->avi.audio_stream == 0xff ||
         !reader->info.audio_sample_rate ||
         !reader->info.audio_channels ||
         !reader->info.audio_bits_per_sample)) {
        result = H263_3GP_ERR_UNSUPPORTED;
    }
    if (result != H263_3GP_OK) {
        resetPcmReader(reader);
        return result;
    }
    *info = reader->info;
    return H263_3GP_OK;
}

int h263_avi_pcm_reader_open_from_decoder(
    H263AviPcmReader *reader, const H2633gpDecoder *decoder,
    H2633gpInfo *info) {
    if (!reader || !decoder || !info || !decoder->pv_ready ||
        decoder->info.container != H263_CONTAINER_AVI ||
        decoder->avi.audio_stream == 0xff ||
        !decoder->info.audio_sample_rate ||
        !decoder->info.audio_channels ||
        !decoder->info.audio_bits_per_sample) {
        return H263_3GP_ERR_ARGUMENT;
    }
    resetPcmReader(reader);
    reader->info = decoder->info;
    reader->avi = decoder->avi;
    *info = reader->info;
    return H263_3GP_OK;
}

int h263_avi_pcm_reader_decode_next(H263AviPcmReader *reader, FILE *file,
                                    H263AviPcmFrame *frame) {
    if (!reader || !file || !frame ||
        reader->info.container != H263_CONTAINER_AVI) {
        return H263_3GP_ERR_ARGUMENT;
    }
    memset(frame, 0, sizeof(*frame));
    if (!reader->chunk_remaining) {
        uint32_t size = 0;
        const int result = nextAviPayload(
            file, reader->avi, false,
            &reader->avi.next_audio_offset, &size);
        if (result != H263_3GP_OK) return result;
        const uint32_t bytes_per_sample =
            reader->info.audio_bits_per_sample / 8U;
        if (!bytes_per_sample || !size || size % bytes_per_sample)
            return H263_3GP_ERR_FORMAT;
        reader->chunk_remaining = size;
    }

    const uint32_t bytes_per_sample =
        reader->info.audio_bits_per_sample / 8U;
    const uint32_t samples = MIN(
        H263_AVI_PCM_MAX_SAMPLES,
        reader->chunk_remaining / bytes_per_sample);
    if (!samples) return H263_3GP_ERR_FORMAT;
    const size_t byte_count =
        (size_t)(samples) * bytes_per_sample;
    if (reader->info.audio_bits_per_sample == 8) {
        if (!readExact(file, frame->samples, byte_count))
            return H263_3GP_ERR_IO;
    } else {
        uint8_t bytes[H263_AVI_PCM_MAX_SAMPLES * 2];
        if (!readExact(file, bytes, byte_count))
            return H263_3GP_ERR_IO;
        for (uint32_t index = 0; index < samples; ++index) {
            const uint16_t encoded = (uint16_t)(
                (uint16_t)(bytes[index * 2U]) |
                ((uint16_t)(
                     bytes[index * 2U + 1U])
                 << 8));
            const int16_t sample = (int16_t)(encoded);
            frame->samples[index] = (uint8_t)(
                ((int32_t)(sample) + 32768) >> 8);
        }
    }
    reader->chunk_remaining -= (uint32_t)(byte_count);
    frame->sample_count = (uint16_t)(samples);
    return H263_3GP_OK;
}

int h263_3gp_decoder_open(H2633gpDecoder *decoder, FILE *file,
                          H2633gpInfo *info) {
    if (!decoder || !file || !info) return H263_3GP_ERR_ARGUMENT;
    clearDecoder(decoder);
    uint8_t signature[12] = {0};
    int result =
        seekFile(file, 0) && readExact(file, signature, sizeof signature)
            ? H263_3GP_OK
            : H263_3GP_ERR_IO;
    if (result == H263_3GP_OK) {
        result =
            !memcmp(signature, "RIFF", 4) &&
                    !memcmp(signature + 8, "AVI ", 4)
                ? parseAviContainer(
                      file, &decoder->info, &decoder->avi)
                : parseContainer(file, decoder);
    }
    if (result == H263_3GP_OK) {
        decoder->packet_capacity = MIN(
            (size_t)(decoder->info.max_sample_size),
            (size_t)(H263_PACKET_BUFFER_BYTES));
        decoder->packet = (uint8_t *)(
            malloc(decoder->packet_capacity + kInputPadding));
        if (!decoder->packet) result = H263_3GP_ERR_PACKET_MEMORY;
    }
    decoder->controls.readBitstreamData = refillPacketBuffer;
    decoder->controls.appData.object = decoder;
#if PV_H263_STAGE_PROFILE
    decoder->controls.decodeProfile = &decoder->profile;
#endif
    if (result == H263_3GP_OK) {
        result = initializeDecoder(decoder, file);
        if (result == H263_3GP_ERR_FRAME_MEMORY &&
            decoder->requested_output_count == 2 &&
            (decoder->intra_only ||
             decoder->info.video_codec ==
                 H263_VIDEO_CODEC_MPEG4_SIMPLE)) {
            /*
             * CIF H.263 is intra-only. MPEG-4 can retry with two packed
             * Y6/U5/V5 pictures plus the rolling row workspace. Neither
             * fallback requires reparsing the container or reallocating the
             * compressed packet buffer.
             */
            clearOutputFrames(decoder);
            clearCompactReference(decoder);
            decoder->requested_output_count = 1;
            result = initializeDecoder(decoder, file);
        }
    }
    if (result != H263_3GP_OK) {
        clearDecoder(decoder);
        return result;
    }
    *info = decoder->info;
    return H263_3GP_OK;
}

int h263_3gp_decoder_decode_next(H2633gpDecoder *decoder, FILE *file,
                                 H2633gpFrame *frame) {
    if (!decoder || !file || !frame || !decoder->pv_ready)
        return H263_3GP_ERR_ARGUMENT;
    if (decoder->sample_index >= decoder->info.frame_count)
        return H263_3GP_EOF;
    H263_PROFILE_START(total_start);
    uint32_t size = 0;
    if (decoder->info.container == H263_CONTAINER_AVI) {
        const int result = nextAviPayload(
            file, decoder->avi, true,
            &decoder->avi.next_video_offset, &size);
        if (result != H263_3GP_OK) return result;
        if (!size || size > decoder->info.max_sample_size)
            return H263_3GP_ERR_FORMAT;
    } else {
        if (decoder->sample_in_chunk == 0) {
            const int result = beginChunk(decoder);
            if (result != H263_3GP_OK) return result;
        }
        const int result =
            sampleSize(decoder, decoder->sample_index, &size);
        if (result != H263_3GP_OK) return result;
        if (size > decoder->info.max_sample_size)
            return H263_3GP_ERR_FORMAT;
        if (!seekFile(file, decoder->sample_offset)) {
            return H263_3GP_ERR_IO;
        }
    }
    decoder->stream_file = file;
    decoder->stream_remaining = size;
    decoder->stream_io_error = false;
    const size_t initial_size = MIN(
        (size_t)(size), decoder->packet_capacity);
    H263_PROFILE_START(initial_input_start);
    if (!readExact(file, decoder->packet, initial_size)) {
        decoder->stream_file = NULL;
        return H263_3GP_ERR_IO;
    }
    H263_PROFILE_ADD(decoder, input_cycles, initial_input_start);
    H263_PROFILE_COUNT(decoder, input_bytes, initial_size);
    decoder->stream_remaining -= (uint32_t)(initial_size);
    memset(decoder->packet + initial_size, 0, kInputPadding);

    uint8 *bitstream = decoder->packet;
    int32 input_size = (int32)(initial_size);
    uint32 timestamp = (uint32_t)(
        MIN(decoder->timestamp, UINT32_MAX));
    uint use_external_timestamp = 1;
    VopHeaderInfo header = {0};
    const uint8_t output_index =
        decoder->compact_reference_enabled ||
                decoder->output_count == 1
            ? 0
            : (decoder->sample_index & 1U);
    uint8_t *output = decoder->output_y[output_index];
    H263_PROFILE_START(header_start);
    const Bool header_ok =
        PVDecodeVopHeader(
            &decoder->controls, &bitstream, &timestamp,
            &input_size, &header, &use_external_timestamp, output);
    H263_PROFILE_ADD(decoder, header_cycles, header_start);
    if (!header_ok) {
        decoder->stream_file = NULL;
        if (decoder->stream_io_error)
            return H263_3GP_ERR_IO;
        return H263_3GP_ERR_DECODE;
    }
    if (decoder->intra_only && header.frameType != MP4_I_FRAME) {
        decoder->stream_file = NULL;
        return H263_3GP_ERR_UNSUPPORTED;
    }
    if (decoder->info.video_codec == H263_VIDEO_CODEC_MPEG4_SIMPLE &&
        header.frameType != MP4_I_FRAME &&
        header.frameType != MP4_P_FRAME) {
        decoder->stream_file = NULL;
        return H263_3GP_ERR_UNSUPPORTED;
    }
    PVSetCurrentYUVPlanes(
        &decoder->controls,
        decoder->output_y[output_index],
        decoder->output_u[output_index],
        decoder->output_v[output_index]);
    decoder->next_compact_row = 0;
    int32 width = 0;
    int32 height = 0;
    PVGetVideoDimensions(&decoder->controls, &width, &height);
    if (width != decoder->info.width || height != decoder->info.height) {
        decoder->stream_file = NULL;
        return H263_3GP_ERR_UNSUPPORTED;
    }
    H263_PROFILE_START(body_start);
    const Bool body_ok =
        PVDecodeVopBody(&decoder->controls, &input_size);
    H263_PROFILE_ADD(decoder, body_cycles, body_start);
    if (!body_ok) {
        decoder->stream_file = NULL;
        if (decoder->stream_io_error)
            return H263_3GP_ERR_IO;
        return H263_3GP_ERR_DECODE;
    }
    decoder->stream_file = NULL;
    if (decoder->stream_io_error)
        return H263_3GP_ERR_IO;
    if (decoder->compact_reference_enabled) {
        if (header.vopCoded) {
            compactOutputRowGuard(
                decoder, decoder->buffer_height);
        } else {
            H263_PROFILE_START(compact_copy_start);
            copyCompactFrame(
                &decoder->compact_output,
                &decoder->compact_reference);
            H263_PROFILE_ADD(
                decoder, compact_copy_cycles, compact_copy_start);
        }
        {
            CompactYuv420Frame completed =
                decoder->compact_reference;
            decoder->compact_reference =
                decoder->compact_output;
            decoder->compact_output = completed;
        }
        PVSetCompactReferenceYUV420(
            &decoder->controls, &decoder->compact_reference);
    }

    const uint32_t duration =
        decoder->info.container == H263_CONTAINER_AVI
            ? decoder->info.fps_den
            : decoder->stts[decoder->stts_index].sample_delta;
    frame->storage_mode =
        decoder->compact_reference_enabled
            ? H263_FRAME_STORAGE_Y6_U5_V5
            : H263_FRAME_STORAGE_YUV420;
    frame->compact = decoder->compact_reference_enabled
                         ? decoder->compact_reference
                         : (CompactYuv420Frame){0};
    frame->y = decoder->compact_reference_enabled
                   ? NULL
                   : decoder->output_y[output_index];
    frame->u = decoder->compact_reference_enabled
                   ? NULL
                   : decoder->output_u[output_index];
    frame->v = decoder->compact_reference_enabled
                   ? NULL
                   : decoder->output_v[output_index];
    frame->width = decoder->info.width;
    frame->height = decoder->info.height;
    frame->y_stride = decoder->buffer_width;
    frame->chroma_stride = decoder->buffer_width / 2;
    frame->timestamp_ticks = decoder->timestamp;
    frame->duration_ticks = duration;
    frame->index = decoder->sample_index;

    if (decoder->info.container != H263_CONTAINER_AVI) {
        decoder->sample_offset += size;
        ++decoder->sample_in_chunk;
        if (decoder->sample_in_chunk == decoder->samples_in_chunk) {
            decoder->sample_in_chunk = 0;
            ++decoder->chunk_index;
        }
        if (--decoder->stts_remaining == 0 &&
            decoder->stts_index + 1 < decoder->stts_count) {
            ++decoder->stts_index;
            decoder->stts_remaining =
                decoder->stts[decoder->stts_index].sample_count;
        }
    }
    ++decoder->sample_index;
    decoder->timestamp += duration;
    H263_PROFILE_COUNT(decoder, frames, 1);
    if (header.frameType == MP4_I_FRAME) {
        H263_PROFILE_COUNT(decoder, i_frames, 1);
    } else if (header.frameType == MP4_P_FRAME) {
        H263_PROFILE_COUNT(decoder, p_frames, 1);
    }
    H263_PROFILE_ADD(decoder, total_cycles, total_start);
    return H263_3GP_OK;
}

size_t h263_3gp_decoder_memory_bytes(const H2633gpDecoder *decoder) {
    if (!decoder) return 0;
    return sizeof(*decoder) + decoder->packet_capacity + kInputPadding +
           decoder->output_bytes * decoder->allocated_output_count +
           (decoder->compact_reference_enabled
                ? 2 * compact_yuv420_frame_storage_bytes(
                          decoder->buffer_width,
                          decoder->buffer_height)
                : 0) +
           (decoder->sample_sizes
                ? (size_t)(decoder->info.frame_count) *
                      sizeof(uint32_t)
                : 0) +
           (decoder->chunk_offsets
                ? (size_t)(decoder->chunk_count) *
                      sizeof(uint64_t)
                : 0) +
           (size_t)(decoder->stsc_count) * sizeof(StscEntry) +
           (size_t)(decoder->stts_count) * sizeof(SttsEntry) +
           (decoder->pv_ready
                ? (size_t)(PVGetDecMemoryUsage(
                      (VideoDecControls *)(&decoder->controls)))
                : 0);
}

const H263DecodeProfile *h263_3gp_decoder_decode_profile(
    const H2633gpDecoder *decoder) {
#if PV_H263_STAGE_PROFILE
    return decoder ? &decoder->profile : NULL;
#else
    (void)decoder;
    return NULL;
#endif
}

void h263_3gp_decoder_decode_profile_reset(H2633gpDecoder *decoder) {
#if PV_H263_STAGE_PROFILE
    if (decoder) memset(&decoder->profile, 0, sizeof(decoder->profile));
#else
    (void)decoder;
#endif
}

const char *h263_3gp_strerror(int result) {
    switch (result) {
        case H263_3GP_OK:
            return "success";
        case H263_3GP_EOF:
            return "end of stream";
        case H263_3GP_ERR_ARGUMENT:
            return "invalid argument";
        case H263_3GP_ERR_IO:
            return "I/O error";
        case H263_3GP_ERR_FORMAT:
            return "invalid H.263/MPEG-4 container";
        case H263_3GP_ERR_UNSUPPORTED:
            return "unsupported H.263/MPEG-4 profile";
        case H263_3GP_ERR_MEMORY:
            return "out of memory";
        case H263_3GP_ERR_FRAME_MEMORY:
            return "frame buffer memory";
        case H263_3GP_ERR_DECODER_MEMORY:
            return "decoder table memory";
        case H263_3GP_ERR_PACKET_MEMORY:
            return "compressed packet memory";
        case H263_3GP_ERR_DECODE:
            return "H.263/MPEG-4 decode error";
        default:
            return "unknown H.263/MPEG-4 container error";
    }
}

