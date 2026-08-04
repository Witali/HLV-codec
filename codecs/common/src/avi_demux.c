#include "avi_demux.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#ifndef AVI_DEMUX_CURSOR_AWARE_FINISH
#define AVI_DEMUX_CURSOR_AWARE_FINISH 1
#endif

#ifndef AVI_DEMUX_FINISH_PROFILE
#define AVI_DEMUX_FINISH_PROFILE 0
#endif

#if AVI_DEMUX_FINISH_PROFILE
static AviDemuxFinishStats finish_stats;
#define FINISH_COUNT(field) (++finish_stats.field)
#else
#define FINISH_COUNT(field) ((void)0)
#endif

typedef struct AviStreamHeader {
    uint32_t type;
    uint32_t handler;
    uint32_t scale;
    uint32_t rate;
    uint32_t length;
    uint32_t suggested_buffer;
} AviStreamHeader;

uint32_t avi_demux_fourcc(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a |
           ((uint32_t)(uint8_t)b << 8) |
           ((uint32_t)(uint8_t)c << 16) |
           ((uint32_t)(uint8_t)d << 24);
}

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_exact(FILE *file, void *buffer, size_t size) {
    return file && (!size || buffer) && fread(buffer, 1, size, file) == size;
}

static int seek_absolute(FILE *file, long offset) {
    return file && offset >= 0 && fseek(file, offset, SEEK_SET) == 0;
}

static int read_chunk_header(FILE *file, uint32_t *id, uint32_t *size,
                             long *payload) {
    uint8_t header[8];
    if (!read_exact(file, header, sizeof(header))) return 0;
    *id = read_le32(header);
    *size = read_le32(header + 4);
    *payload = ftell(file);
    return *payload >= 0;
}

static int padded_end(long payload, uint32_t size, long *end) {
    uint64_t value;
    if (payload < 0 || !end) return 0;
    value = (uint64_t)payload + size + (size & 1U);
    if (value > LONG_MAX) return 0;
    *end = (long)value;
    return 1;
}

static int skip_chunk(FILE *file, long payload, uint32_t size) {
    long end;
    return padded_end(payload, size, &end) && seek_absolute(file, end);
}

static int stream_number(uint32_t id) {
    uint8_t digits[2];
    int value = 0;
    unsigned i;
    digits[0] = (uint8_t)id;
    digits[1] = (uint8_t)(id >> 8);
    for (i = 0; i < 2; ++i) {
        int digit;
        if (digits[i] >= '0' && digits[i] <= '9')
            digit = digits[i] - '0';
        else if (digits[i] >= 'A' && digits[i] <= 'F')
            digit = digits[i] - 'A' + 10;
        else if (digits[i] >= 'a' && digits[i] <= 'f')
            digit = digits[i] - 'a' + 10;
        else
            return -1;
        value = (value << 4) | digit;
    }
    return value;
}

static int chunk_kind(uint32_t id, uint8_t stream,
                      AviDemuxPacketKind kind) {
    const uint8_t c2 = (uint8_t)(id >> 16);
    const uint8_t c3 = (uint8_t)(id >> 24);
    if (stream_number(id) != stream) return 0;
    if (kind == AVI_DEMUX_PACKET_VIDEO)
        return c2 == 'd' && (c3 == 'c' || c3 == 'b');
    return kind == AVI_DEMUX_PACKET_AUDIO && c2 == 'w' && c3 == 'b';
}

static int parse_stream_list(FILE *file, long end, uint8_t index,
                             AviDemuxInfo *info) {
    AviStreamHeader stream;
    uint8_t format[40 + AVI_DEMUX_MAX_VIDEO_FORMAT_EXTRA];
    size_t format_size = 0;
    uint32_t format_chunk_size = 0;
    memset(&stream, 0, sizeof(stream));
    memset(format, 0, sizeof(format));
    while (ftell(file) >= 0 && ftell(file) + 8 <= end) {
        uint32_t id;
        uint32_t size;
        long payload;
        uint64_t data_end;
        if (!read_chunk_header(file, &id, &size, &payload))
            return AVI_DEMUX_ERR_IO;
        data_end = (uint64_t)payload + size;
        if (data_end > (uint64_t)end) return AVI_DEMUX_ERR_FORMAT;
        if (id == avi_demux_fourcc('s', 't', 'r', 'h')) {
            uint8_t bytes[56];
            const size_t wanted = size < sizeof(bytes) ? size : sizeof(bytes);
            memset(bytes, 0, sizeof(bytes));
            if (wanted < 48 || !read_exact(file, bytes, wanted))
                return AVI_DEMUX_ERR_FORMAT;
            stream.type = read_le32(bytes);
            stream.handler = read_le32(bytes + 4);
            stream.scale = read_le32(bytes + 20);
            stream.rate = read_le32(bytes + 24);
            stream.length = read_le32(bytes + 32);
            stream.suggested_buffer = read_le32(bytes + 36);
        } else if (id == avi_demux_fourcc('s', 't', 'r', 'f')) {
            format_chunk_size = size;
            format_size = size < sizeof(format) ? size : sizeof(format);
            if (!read_exact(file, format, format_size))
                return AVI_DEMUX_ERR_IO;
        }
        if (!skip_chunk(file, payload, size)) return AVI_DEMUX_ERR_IO;
    }

    if (stream.type == avi_demux_fourcc('v', 'i', 'd', 's') &&
        info->video.stream_index == AVI_DEMUX_NO_STREAM) {
        int32_t signed_height;
        uint32_t width;
        uint32_t height;
        if (format_size < 20) return AVI_DEMUX_ERR_FORMAT;
        if (format_chunk_size > sizeof(format)) return AVI_DEMUX_ERR_RANGE;
        width = read_le32(format + 4);
        signed_height = (int32_t)read_le32(format + 8);
        height = signed_height < 0 ? (uint32_t)(-(int64_t)signed_height)
                                   : (uint32_t)signed_height;
        if (!width || !height || width > UINT16_MAX || height > UINT16_MAX)
            return AVI_DEMUX_ERR_RANGE;
        info->video.handler_fourcc = stream.handler;
        info->video.compression_fourcc = read_le32(format + 16);
        info->video.scale = stream.scale;
        info->video.rate = stream.rate;
        info->video.frame_count = stream.length;
        info->video.suggested_buffer_size = stream.suggested_buffer;
        info->video.width = (uint16_t)width;
        info->video.height = (uint16_t)height;
        if (format_size > 40) {
            info->video.format_extra_size = (uint16_t)(format_size - 40);
            memcpy(info->video.format_extra, format + 40,
                   info->video.format_extra_size);
        }
        info->video.stream_index = index;
    } else if (stream.type == avi_demux_fourcc('a', 'u', 'd', 's') &&
               info->audio.stream_index == AVI_DEMUX_NO_STREAM) {
        if (format_size < 16) return AVI_DEMUX_ERR_FORMAT;
        info->audio.format_tag = read_le16(format);
        info->audio.channels = read_le16(format + 2);
        info->audio.sample_rate = read_le32(format + 4);
        info->audio.average_bytes_per_second = read_le32(format + 8);
        info->audio.block_align = read_le16(format + 12);
        info->audio.bits_per_sample = read_le16(format + 14);
        if (format_size >= 18) {
            info->audio.extra_size = read_le16(format + 16);
            if (info->audio.extra_size >= 2 && format_size >= 20)
                info->audio.samples_per_block = read_le16(format + 18);
        }
        info->audio.stream_index = index;
    }
    return AVI_DEMUX_OK;
}

static int parse_header_list(FILE *file, long end, AviDemuxInfo *info) {
    unsigned stream_index = 0;
    while (ftell(file) >= 0 && ftell(file) + 8 <= end) {
        uint32_t id;
        uint32_t size;
        long payload;
        uint64_t data_end;
        if (!read_chunk_header(file, &id, &size, &payload))
            return AVI_DEMUX_ERR_IO;
        data_end = (uint64_t)payload + size;
        if (data_end > (uint64_t)end) return AVI_DEMUX_ERR_FORMAT;
        if (id == avi_demux_fourcc('a', 'v', 'i', 'h')) {
            uint8_t avih[56];
            const size_t wanted = size < sizeof(avih) ? size : sizeof(avih);
            memset(avih, 0, sizeof(avih));
            if (wanted < 20 || !read_exact(file, avih, wanted))
                return AVI_DEMUX_ERR_FORMAT;
            info->microseconds_per_frame = read_le32(avih);
            info->main_frame_count = read_le32(avih + 16);
        } else if (id == avi_demux_fourcc('L', 'I', 'S', 'T')) {
            uint8_t type[4];
            if (size < 4 || !read_exact(file, type, sizeof(type)))
                return AVI_DEMUX_ERR_FORMAT;
            if (read_le32(type) == avi_demux_fourcc('s', 't', 'r', 'l')) {
                int result;
                if (stream_index >= AVI_DEMUX_NO_STREAM)
                    return AVI_DEMUX_ERR_RANGE;
                result = parse_stream_list(file, payload + (long)size,
                                           (uint8_t)stream_index, info);
                if (result != AVI_DEMUX_OK) return result;
                ++stream_index;
            }
        }
        if (!skip_chunk(file, payload, size)) return AVI_DEMUX_ERR_IO;
    }
    return AVI_DEMUX_OK;
}

int avi_demux_read_info(FILE *file, AviDemuxInfo *info) {
    uint8_t riff[12];
    uint64_t riff_end64;
    long riff_end;
    if (!file || !info) return AVI_DEMUX_ERR_ARGUMENT;
    memset(info, 0, sizeof(*info));
    info->video.stream_index = AVI_DEMUX_NO_STREAM;
    info->audio.stream_index = AVI_DEMUX_NO_STREAM;
    if (!seek_absolute(file, 0)) return AVI_DEMUX_ERR_IO;
    if (!read_exact(file, riff, sizeof(riff))) return AVI_DEMUX_ERR_IO;
    if (read_le32(riff) != avi_demux_fourcc('R', 'I', 'F', 'F') ||
        read_le32(riff + 8) != avi_demux_fourcc('A', 'V', 'I', ' '))
        return AVI_DEMUX_ERR_FORMAT;
    riff_end64 = UINT64_C(8) + read_le32(riff + 4);
    if (riff_end64 < 12 || riff_end64 > LONG_MAX)
        return AVI_DEMUX_ERR_RANGE;
    riff_end = (long)riff_end64;

    while (ftell(file) >= 0 && ftell(file) + 8 <= riff_end) {
        uint32_t id;
        uint32_t size;
        long payload;
        uint64_t data_end;
        if (!read_chunk_header(file, &id, &size, &payload))
            return AVI_DEMUX_ERR_IO;
        data_end = (uint64_t)payload + size;
        if (data_end > (uint64_t)riff_end) return AVI_DEMUX_ERR_FORMAT;
        if (id == avi_demux_fourcc('L', 'I', 'S', 'T')) {
            uint8_t type[4];
            int result = AVI_DEMUX_OK;
            if (size < 4 || !read_exact(file, type, sizeof(type)))
                return AVI_DEMUX_ERR_FORMAT;
            if (read_le32(type) == avi_demux_fourcc('h', 'd', 'r', 'l'))
                result = parse_header_list(file, payload + (long)size, info);
            else if (read_le32(type) ==
                     avi_demux_fourcc('m', 'o', 'v', 'i')) {
                info->movi_start = ftell(file);
                info->movi_end = payload + (long)size;
            }
            if (result != AVI_DEMUX_OK) return result;
        }
        /* Do not seek across a potentially very large movi list merely to
         * reach idx1. strh already carries frame count and buffer size; the
         * sequential packet reader validates every actual chunk later. This
         * avoids a full FAT cluster walk at open time on ESP32 SD cards. */
        if (info->movi_start) break;
        if (!skip_chunk(file, payload, size)) return AVI_DEMUX_ERR_IO;
    }
    if (info->video.stream_index == AVI_DEMUX_NO_STREAM ||
        !info->video.width || !info->video.height || !info->movi_start ||
        info->movi_end <= info->movi_start)
        return AVI_DEMUX_ERR_FORMAT;
    if (!info->video.max_packet_size)
        info->video.max_packet_size = info->video.suggested_buffer_size;
    return seek_absolute(file, info->movi_start) ? AVI_DEMUX_OK
                                                 : AVI_DEMUX_ERR_IO;
}

int avi_demux_next_packet(FILE *file, const AviDemuxInfo *info,
                          AviDemuxPacketKind kind,
                          AviDemuxPacket *packet) {
    uint8_t wanted_stream;
    if (!file || !info || !packet ||
        (kind != AVI_DEMUX_PACKET_VIDEO && kind != AVI_DEMUX_PACKET_AUDIO))
        return AVI_DEMUX_ERR_ARGUMENT;
    wanted_stream = kind == AVI_DEMUX_PACKET_VIDEO
                        ? info->video.stream_index
                        : info->audio.stream_index;
    if (wanted_stream == AVI_DEMUX_NO_STREAM) return AVI_DEMUX_EOF;
    for (;;) {
        long position = ftell(file);
        uint32_t id;
        uint32_t size;
        long payload;
        if (position < 0) return AVI_DEMUX_ERR_IO;
        if (position >= info->movi_end) return AVI_DEMUX_EOF;
        if (!read_chunk_header(file, &id, &size, &payload))
            return feof(file) ? AVI_DEMUX_EOF : AVI_DEMUX_ERR_IO;
        if (id == avi_demux_fourcc('L', 'I', 'S', 'T')) {
            uint8_t type[4];
            if (size < 4 || !read_exact(file, type, sizeof(type)))
                return AVI_DEMUX_ERR_FORMAT;
            if (read_le32(type) == avi_demux_fourcc('r', 'e', 'c', ' '))
                continue;
        }
        if (chunk_kind(id, wanted_stream, kind)) {
            if (!size) {
                if (!skip_chunk(file, payload, size))
                    return AVI_DEMUX_ERR_IO;
                continue;
            }
            memset(packet, 0, sizeof(*packet));
            packet->chunk_id = id;
            packet->payload_size = size;
            packet->payload_offset = payload;
            packet->stream_index = wanted_stream;
            packet->kind = kind;
            if (!padded_end(payload, size, &packet->next_offset))
                return AVI_DEMUX_ERR_RANGE;
            return AVI_DEMUX_OK;
        }
        if (!skip_chunk(file, payload, size)) return AVI_DEMUX_ERR_IO;
    }
}

int avi_demux_finish_packet(FILE *file, const AviDemuxPacket *packet) {
    if (!file || !packet) return AVI_DEMUX_ERR_ARGUMENT;
#if AVI_DEMUX_CURSOR_AWARE_FINISH
    {
        long current = ftell(file);
        if (current < 0) return AVI_DEMUX_ERR_IO;
        if (current == packet->next_offset) {
            FINISH_COUNT(cursor_matches);
            return AVI_DEMUX_OK;
        }
        if (current < LONG_MAX && current + 1L == packet->next_offset) {
            uint8_t byte;
            if (!read_exact(file, &byte, 1U)) return AVI_DEMUX_ERR_IO;
            FINISH_COUNT(sequential_single_bytes);
            return AVI_DEMUX_OK;
        }
    }
#endif
    FINISH_COUNT(fallback_seeks);
    return seek_absolute(file, packet->next_offset) ? AVI_DEMUX_OK
                                                    : AVI_DEMUX_ERR_IO;
}

void avi_demux_finish_stats_reset(void) {
#if AVI_DEMUX_FINISH_PROFILE
    memset(&finish_stats, 0, sizeof(finish_stats));
#endif
}

void avi_demux_finish_stats_get(AviDemuxFinishStats *stats) {
    if (!stats) return;
#if AVI_DEMUX_FINISH_PROFILE
    *stats = finish_stats;
#else
    memset(stats, 0, sizeof(*stats));
#endif
}

const char *avi_demux_strerror(int result) {
    switch (result) {
        case AVI_DEMUX_OK: return "success";
        case AVI_DEMUX_EOF: return "end of AVI stream";
        case AVI_DEMUX_ERR_ARGUMENT: return "invalid AVI demux argument";
        case AVI_DEMUX_ERR_IO: return "AVI I/O error";
        case AVI_DEMUX_ERR_FORMAT: return "invalid AVI container";
        case AVI_DEMUX_ERR_RANGE: return "AVI value is out of range";
        default: return "unknown AVI demux error";
    }
}
