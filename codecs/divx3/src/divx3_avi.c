#include "divx3_avi.h"

#include <limits.h>
#include <string.h>

enum {
    DIVX3_AVI_FALLBACK_PACKET_BYTES = 128 * 1024,
    DIVX3_AVI_MAX_PACKET_BYTES = 1024 * 1024,
};

typedef struct {
    uint32_t type;
    uint32_t handler;
    uint32_t scale;
    uint32_t rate;
    uint32_t length;
    uint32_t suggested_buffer;
} AviStreamHeader;

static uint32_t fourcc(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a |
           ((uint32_t)(uint8_t)b << 8) |
           ((uint32_t)(uint8_t)c << 16) |
           ((uint32_t)(uint8_t)d << 24);
}

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static int read_exact(FILE *file, void *buffer, size_t size) {
    return file && (!size || buffer) &&
           fread(buffer, 1, size, file) == size;
}

static int seek_absolute(FILE *file, long position) {
    return file && position >= 0 && fseek(file, position, SEEK_SET) == 0;
}

static int skip_chunk(FILE *file, long data_start, uint32_t size) {
    uint64_t end = (uint64_t)data_start + size + (size & 1U);
    return end <= LONG_MAX && seek_absolute(file, (long)end);
}

static int read_chunk_header(FILE *file, uint32_t *id, uint32_t *size,
                             long *data_start) {
    uint8_t header[8];
    if (!read_exact(file, header, sizeof(header))) return 0;
    *id = read_le32(header);
    *size = read_le32(header + 4);
    *data_start = ftell(file);
    return *data_start >= 0;
}

static int stream_number(uint32_t id) {
    int high;
    int low;
    uint8_t high_byte = (uint8_t)id;
    uint8_t low_byte = (uint8_t)(id >> 8);
    if (high_byte >= '0' && high_byte <= '9')
        high = high_byte - '0';
    else if (high_byte >= 'A' && high_byte <= 'F')
        high = high_byte - 'A' + 10;
    else if (high_byte >= 'a' && high_byte <= 'f')
        high = high_byte - 'a' + 10;
    else
        return -1;
    if (low_byte >= '0' && low_byte <= '9')
        low = low_byte - '0';
    else if (low_byte >= 'A' && low_byte <= 'F')
        low = low_byte - 'A' + 10;
    else if (low_byte >= 'a' && low_byte <= 'f')
        low = low_byte - 'a' + 10;
    else
        return -1;
    return (high << 4) | low;
}

static int is_video_chunk(uint32_t id, uint8_t stream) {
    uint8_t c2 = (uint8_t)(id >> 16);
    uint8_t c3 = (uint8_t)(id >> 24);
    return stream_number(id) == stream &&
           ((c2 == 'd' && c3 == 'c') ||
            (c2 == 'd' && c3 == 'b'));
}

static int is_audio_chunk(uint32_t id, uint8_t stream) {
    return stream_number(id) == stream &&
           (uint8_t)(id >> 16) == 'w' &&
           (uint8_t)(id >> 24) == 'b';
}

int divx3_avi_is_v3_fourcc(uint32_t value) {
    static const char aliases[][4] = {
        {'D', 'I', 'V', '3'}, {'M', 'P', '4', '3'},
        {'D', 'I', 'V', '4'}, {'D', 'I', 'V', '5'},
        {'D', 'I', 'V', '6'}, {'A', 'P', '4', '1'},
        {'C', 'O', 'L', '1'}, {'C', 'O', 'L', '0'},
        {'M', 'P', 'G', '3'}, {'D', 'V', 'X', '3'},
        {'3', 'I', 'V', '1'}, {'3', 'I', 'V', 'D'},
    };
    size_t index;
    uint8_t bytes[4];
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
    for (index = 0; index < sizeof(aliases) / sizeof(aliases[0]);
         ++index) {
        unsigned byte;
        int matches = 1;
        for (byte = 0; byte < 4; ++byte) {
            uint8_t actual = bytes[byte];
            uint8_t expected = (uint8_t)aliases[index][byte];
            if (actual >= 'a' && actual <= 'z') actual -= 32;
            if (actual != expected) {
                matches = 0;
                break;
            }
        }
        if (matches) return 1;
    }
    return 0;
}

static int parse_stream_list(FILE *file, long end, uint8_t stream_index,
                             Divx3AviInfo *info) {
    AviStreamHeader stream;
    uint8_t format[64];
    size_t format_size = 0;
    memset(&stream, 0, sizeof(stream));
    memset(format, 0, sizeof(format));
    while (ftell(file) >= 0 && ftell(file) + 8 <= end) {
        uint32_t id;
        uint32_t size;
        long data_start;
        uint64_t data_end;
        if (!read_chunk_header(file, &id, &size, &data_start))
            return DIVX3_AVI_ERR_IO;
        data_end = (uint64_t)data_start + size;
        if (data_end > (uint64_t)end) return DIVX3_AVI_ERR_FORMAT;
        if (id == fourcc('s', 't', 'r', 'h')) {
            uint8_t bytes[56];
            size_t wanted = size < sizeof(bytes) ? size : sizeof(bytes);
            memset(bytes, 0, sizeof(bytes));
            if (wanted < 48 || !read_exact(file, bytes, wanted))
                return DIVX3_AVI_ERR_FORMAT;
            stream.type = read_le32(bytes);
            stream.handler = read_le32(bytes + 4);
            stream.scale = read_le32(bytes + 20);
            stream.rate = read_le32(bytes + 24);
            stream.length = read_le32(bytes + 32);
            stream.suggested_buffer = read_le32(bytes + 36);
        } else if (id == fourcc('s', 't', 'r', 'f')) {
            format_size = size < sizeof(format) ? size : sizeof(format);
            if (!read_exact(file, format, format_size))
                return DIVX3_AVI_ERR_IO;
        }
        if (!skip_chunk(file, data_start, size))
            return DIVX3_AVI_ERR_IO;
    }
    if (stream.type == fourcc('v', 'i', 'd', 's') &&
        (divx3_avi_is_v3_fourcc(stream.handler) ||
         (format_size >= 20 &&
          divx3_avi_is_v3_fourcc(read_le32(format + 16))))) {
        uint32_t width;
        int32_t signed_height;
        uint32_t height;
        if (!stream.scale || !stream.rate || format_size < 20)
            return DIVX3_AVI_ERR_FORMAT;
        if (!divx3_avi_is_v3_fourcc(read_le32(format + 16)))
            return DIVX3_AVI_ERR_FORMAT;
        width = read_le32(format + 4);
        signed_height = (int32_t)read_le32(format + 8);
        height = signed_height < 0 ? (uint32_t)(-(int64_t)signed_height)
                                   : (uint32_t)signed_height;
        if (!width || !height || width > UINT16_MAX ||
            height > UINT16_MAX)
            return DIVX3_AVI_ERR_RANGE;
        info->video_stream = stream_index;
        info->width = (uint16_t)width;
        info->height = (uint16_t)height;
        info->fps_num = stream.rate;
        info->fps_den = stream.scale;
        info->frame_count = stream.length;
        info->max_video_packet_size = stream.suggested_buffer;
    } else if (stream.type == fourcc('a', 'u', 'd', 's')) {
        if (format_size < 16) return DIVX3_AVI_ERR_FORMAT;
        if (read_le16(format) == 1 &&
            read_le16(format + 2) == 1 &&
            read_le16(format + 14) == 8 &&
            read_le32(format + 4) != 0) {
            info->audio_stream = stream_index;
            info->audio_channels = 1;
            info->audio_sample_rate = read_le32(format + 4);
            info->audio_bits_per_sample = 8;
        }
    }
    return DIVX3_AVI_OK;
}

static int parse_header_list(FILE *file, long end, Divx3AviInfo *info) {
    uint8_t stream_index = 0;
    while (ftell(file) >= 0 && ftell(file) + 8 <= end) {
        uint32_t id;
        uint32_t size;
        long data_start;
        uint64_t data_end;
        if (!read_chunk_header(file, &id, &size, &data_start))
            return DIVX3_AVI_ERR_IO;
        data_end = (uint64_t)data_start + size;
        if (data_end > (uint64_t)end) return DIVX3_AVI_ERR_FORMAT;
        if (id == fourcc('L', 'I', 'S', 'T')) {
            uint8_t list_type[4];
            if (size < 4 ||
                !read_exact(file, list_type, sizeof(list_type)))
                return DIVX3_AVI_ERR_FORMAT;
            if (read_le32(list_type) == fourcc('s', 't', 'r', 'l')) {
                int result = parse_stream_list(
                    file, data_start + (long)size, stream_index++, info);
                if (result != DIVX3_AVI_OK) return result;
            }
        }
        if (!skip_chunk(file, data_start, size))
            return DIVX3_AVI_ERR_IO;
    }
    return DIVX3_AVI_OK;
}

static int scan_index(FILE *file, long data_start, uint32_t size,
                      Divx3AviInfo *info) {
    uint8_t entry[16];
    uint32_t offset;
    if (size % sizeof(entry)) return DIVX3_AVI_ERR_FORMAT;
    if (!seek_absolute(file, data_start)) return DIVX3_AVI_ERR_IO;
    for (offset = 0; offset < size; offset += sizeof(entry)) {
        uint32_t packet_size;
        if (!read_exact(file, entry, sizeof(entry)))
            return DIVX3_AVI_ERR_IO;
        if (!is_video_chunk(read_le32(entry), info->video_stream))
            continue;
        packet_size = read_le32(entry + 12);
        if (packet_size > info->max_video_packet_size)
            info->max_video_packet_size = packet_size;
    }
    return DIVX3_AVI_OK;
}

static int next_payload(FILE *file, const Divx3AviInfo *info,
                        int video, uint32_t *size) {
    if (!file || !info || !size) return DIVX3_AVI_ERR_ARGUMENT;
    for (;;) {
        long position = ftell(file);
        uint32_t id;
        long data_start;
        int wanted;
        if (position < 0) return DIVX3_AVI_ERR_IO;
        if (position >= info->movi_end) return DIVX3_AVI_EOF;
        if (!read_chunk_header(file, &id, size, &data_start))
            return feof(file) ? DIVX3_AVI_EOF : DIVX3_AVI_ERR_IO;
        if (id == fourcc('L', 'I', 'S', 'T')) {
            uint8_t list_type[4];
            if (*size < 4 ||
                !read_exact(file, list_type, sizeof(list_type)))
                return DIVX3_AVI_ERR_FORMAT;
            if (read_le32(list_type) == fourcc('r', 'e', 'c', ' '))
                continue;
        }
        wanted = video ? is_video_chunk(id, info->video_stream)
                       : is_audio_chunk(id, info->audio_stream);
        if (wanted && !*size) {
            if (!skip_chunk(file, data_start, 0))
                return DIVX3_AVI_ERR_IO;
            continue;
        }
        if (wanted) return DIVX3_AVI_OK;
        if (!skip_chunk(file, data_start, *size))
            return DIVX3_AVI_ERR_IO;
    }
}

int divx3_avi_read_info(FILE *file, Divx3AviInfo *info) {
    uint8_t riff[12];
    uint32_t riff_size;
    uint64_t riff_end64;
    long riff_end;
    if (!file || !info) return DIVX3_AVI_ERR_ARGUMENT;
    memset(info, 0, sizeof(*info));
    info->video_stream = 0xff;
    info->audio_stream = 0xff;
    if (!seek_absolute(file, 0)) return DIVX3_AVI_ERR_IO;
    if (!read_exact(file, riff, sizeof(riff)))
        return DIVX3_AVI_ERR_IO;
    if (read_le32(riff) != fourcc('R', 'I', 'F', 'F') ||
        read_le32(riff + 8) != fourcc('A', 'V', 'I', ' '))
        return DIVX3_AVI_ERR_FORMAT;
    riff_size = read_le32(riff + 4);
    riff_end64 = 8ULL + riff_size;
    if (riff_end64 > LONG_MAX || riff_end64 < 12)
        return DIVX3_AVI_ERR_RANGE;
    riff_end = (long)riff_end64;
    while (ftell(file) >= 0 && ftell(file) + 8 <= riff_end) {
        uint32_t id;
        uint32_t size;
        long data_start;
        uint64_t data_end;
        if (!read_chunk_header(file, &id, &size, &data_start))
            return DIVX3_AVI_ERR_IO;
        data_end = (uint64_t)data_start + size;
        if (data_end > (uint64_t)riff_end) return DIVX3_AVI_ERR_FORMAT;
        if (id == fourcc('L', 'I', 'S', 'T')) {
            uint8_t list_type[4];
            uint32_t type;
            if (size < 4 ||
                !read_exact(file, list_type, sizeof(list_type)))
                return DIVX3_AVI_ERR_FORMAT;
            type = read_le32(list_type);
            if (type == fourcc('h', 'd', 'r', 'l')) {
                int result = parse_header_list(
                    file, data_start + (long)size, info);
                if (result != DIVX3_AVI_OK) return result;
            } else if (type == fourcc('m', 'o', 'v', 'i')) {
                info->movi_start = ftell(file);
                info->movi_end = data_start + (long)size;
            }
        } else if (id == fourcc('i', 'd', 'x', '1') &&
                   info->video_stream != 0xff) {
            int result = scan_index(file, data_start, size, info);
            if (result != DIVX3_AVI_OK) return result;
        }
        if (!skip_chunk(file, data_start, size))
            return DIVX3_AVI_ERR_IO;
    }
    if (info->video_stream == 0xff || !info->width || !info->height ||
        !info->fps_num || !info->fps_den || !info->movi_start ||
        info->movi_end <= info->movi_start)
        return DIVX3_AVI_ERR_FORMAT;
    if (!info->max_video_packet_size)
        info->max_video_packet_size = DIVX3_AVI_FALLBACK_PACKET_BYTES;
    if (info->max_video_packet_size > DIVX3_AVI_MAX_PACKET_BYTES)
        return DIVX3_AVI_ERR_RANGE;
    return seek_absolute(file, info->movi_start)
               ? DIVX3_AVI_OK
               : DIVX3_AVI_ERR_IO;
}

int divx3_avi_begin_video_packet(
    FILE *file, const Divx3AviInfo *info, uint32_t *packet_size,
    long *next_offset) {
    uint32_t size;
    long payload_start;
    uint64_t next_position;
    int result;
    if (!file || !info || !packet_size || !next_offset)
        return DIVX3_AVI_ERR_ARGUMENT;
    result = next_payload(file, info, 1, &size);
    if (result != DIVX3_AVI_OK) return result;
    *packet_size = size;
    payload_start = ftell(file);
    if (payload_start < 0) return DIVX3_AVI_ERR_IO;
    if (!size) return DIVX3_AVI_ERR_RANGE;
    next_position = (uint64_t)payload_start + size + (size & 1U);
    if (next_position > LONG_MAX)
        return DIVX3_AVI_ERR_RANGE;
    *next_offset = (long)next_position;
    return DIVX3_AVI_OK;
}

int divx3_avi_finish_video_packet(FILE *file, long next_offset) {
    return seek_absolute(file, next_offset)
               ? DIVX3_AVI_OK
               : DIVX3_AVI_ERR_IO;
}

int divx3_avi_read_video_packet(FILE *file, const Divx3AviInfo *info,
                                uint8_t *buffer, size_t capacity,
                                size_t *packet_size) {
    uint32_t size;
    long next_offset;
    int result;
    if (!file || !info || !buffer || !capacity || !packet_size)
        return DIVX3_AVI_ERR_ARGUMENT;
    result = divx3_avi_begin_video_packet(
        file, info, &size, &next_offset);
    if (result != DIVX3_AVI_OK) return result;
    *packet_size = size;
    if (size > capacity) return DIVX3_AVI_ERR_RANGE;
    if (!read_exact(file, buffer, size))
        return DIVX3_AVI_ERR_IO;
    return divx3_avi_finish_video_packet(file, next_offset);
}

int divx3_avi_next_audio_chunk(FILE *file, const Divx3AviInfo *info,
                               uint32_t *payload_size) {
    if (!info || info->audio_stream == 0xff) return DIVX3_AVI_EOF;
    return next_payload(file, info, 0, payload_size);
}

const char *divx3_avi_strerror(int result) {
    switch (result) {
        case DIVX3_AVI_OK: return "success";
        case DIVX3_AVI_EOF: return "end of file";
        case DIVX3_AVI_ERR_ARGUMENT: return "invalid AVI argument";
        case DIVX3_AVI_ERR_IO: return "AVI I/O error";
        case DIVX3_AVI_ERR_FORMAT: return "unsupported DivX 3 AVI";
        case DIVX3_AVI_ERR_RANGE: return "DivX 3 AVI value is out of range";
        default: return "unknown DivX 3 AVI error";
    }
}
