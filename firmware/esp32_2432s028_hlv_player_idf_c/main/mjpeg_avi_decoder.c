#include "mjpeg_avi_decoder.h"

#include <limits.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#ifdef MJPEG_PHASE_TIMING
#include "esp_cpu.h"
#endif

#ifdef MJPEG_FIXED_RGB565
void mjpeg_install_fixed_rgb565(void);
#endif

#define FALLBACK_FRAME_BYTES (128U * 1024U)
#define MAXIMUM_FRAME_BYTES (1024U * 1024U)
#define IO_ATTEMPTS 3U
#define IO_RETRY_DELAY_US 2000U

static const char *const k_tag = "mjpeg-avi";

typedef struct {
    uint32_t type;
    uint32_t handler;
    uint32_t scale;
    uint32_t rate;
    uint32_t length;
    uint32_t suggested_buffer;
} stream_header_t;

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

static size_t min_size(size_t left, size_t right) {
    return left < right ? left : right;
}

static uint32_t max_u32(uint32_t left, uint32_t right) {
    return left > right ? left : right;
}

static uint16_t min_u16(uint16_t left, uint16_t right) {
    return left < right ? left : right;
}

static bool is_jpeg_sof_marker(uint8_t marker) {
    return marker >= 0xc0U && marker <= 0xcfU &&
           marker != 0xc4U && marker != 0xc8U && marker != 0xccU;
}

static bool prepare_jpeg_decode_height(
    uint8_t *jpeg, size_t size, uint16_t expected_width,
    uint16_t expected_height, uint16_t *decode_height) {
    size_t offset = 2U;

    if (decode_height == NULL) return false;
    *decode_height = expected_height;
    if ((expected_height & 7U) == 0U) return true;
    if (jpeg == NULL || size < 4U ||
        jpeg[0] != 0xffU || jpeg[1] != 0xd8U) {
        return false;
    }

    while (offset + 1U < size) {
        uint8_t marker;
        uint16_t length;
        if (jpeg[offset++] != 0xffU) return false;
        while (offset < size && jpeg[offset] == 0xffU) ++offset;
        if (offset >= size) return false;
        marker = jpeg[offset++];
        if (marker == 0x00U) return false;
        if (marker == 0xd8U ||
            marker == 0x01U ||
            (marker >= 0xd0U && marker <= 0xd7U)) {
            continue;
        }
        if (marker == 0xd9U || marker == 0xdaU ||
            offset + 2U > size) {
            return false;
        }
        length = (uint16_t)(((uint16_t)jpeg[offset] << 8) |
                            jpeg[offset + 1U]);
        if (length < 2U || offset + length > size) return false;
        if (is_jpeg_sof_marker(marker)) {
            uint16_t height;
            uint16_t width;
            uint16_t aligned_height;
            uint8_t components;
            uint8_t max_vertical_sampling = 0;
            size_t component;
            uint16_t mcu_height;
            if (length < 8U) return false;
            height = (uint16_t)(((uint16_t)jpeg[offset + 3U] << 8) |
                                jpeg[offset + 4U]);
            width = (uint16_t)(((uint16_t)jpeg[offset + 5U] << 8) |
                               jpeg[offset + 6U]);
            components = jpeg[offset + 7U];
            if (height != expected_height || width != expected_width ||
                components == 0U ||
                length < (uint16_t)(8U + 3U * components)) {
                return false;
            }
            for (component = 0; component < components; ++component) {
                const uint8_t vertical_sampling =
                    jpeg[offset + 8U + component * 3U + 1U] & 0x0fU;
                if (vertical_sampling > max_vertical_sampling) {
                    max_vertical_sampling = vertical_sampling;
                }
            }
            if (max_vertical_sampling == 0U) return false;
            aligned_height =
                (uint16_t)((expected_height + 7U) & ~7U);
            mcu_height = (uint16_t)(8U * max_vertical_sampling);
            if ((uint16_t)((expected_height + mcu_height - 1U) /
                           mcu_height) !=
                (uint16_t)((aligned_height + mcu_height - 1U) /
                           mcu_height)) {
                return false;
            }
            jpeg[offset + 3U] = (uint8_t)(aligned_height >> 8);
            jpeg[offset + 4U] = (uint8_t)aligned_height;
            *decode_height = aligned_height;
            return true;
        }
        offset += length;
    }
    return false;
}

static bool seek_absolute(FILE *file, long position);

static bool read_exact(FILE *file, void *buffer, size_t size) {
    long start;
    unsigned attempt;

    if (file == NULL || (buffer == NULL && size != 0U)) {
        return false;
    }
    start = ftell(file);
    if (start < 0) {
        return false;
    }
    for (attempt = 0; attempt < IO_ATTEMPTS; ++attempt) {
        size_t received = fread(buffer, 1, size, file);
        if (received == size) {
            return true;
        }
        if (attempt + 1U == IO_ATTEMPTS) {
            return false;
        }
        clearerr(file);
        if (!seek_absolute(file, start)) {
            return false;
        }
        esp_rom_delay_us(IO_RETRY_DELAY_US);
    }
    return false;
}

static bool seek_absolute(FILE *file, long position) {
    unsigned attempt;

    if (file == NULL || position < 0) {
        return false;
    }
    for (attempt = 0; attempt < IO_ATTEMPTS; ++attempt) {
        clearerr(file);
        if (fseek(file, position, SEEK_SET) == 0) {
            return true;
        }
        if (attempt + 1U < IO_ATTEMPTS) {
            esp_rom_delay_us(IO_RETRY_DELAY_US);
        }
    }
    return false;
}

static bool skip_chunk(FILE *file, long data_start, uint32_t size) {
    uint64_t end = (uint64_t)data_start + size + (size & 1U);
    return end <= LONG_MAX && seek_absolute(file, (long)end);
}

static bool read_chunk_header(FILE *file,
                              uint32_t *id,
                              uint32_t *size,
                              long *data_start) {
    uint8_t header[8];
    if (!read_exact(file, header, sizeof header)) {
        return false;
    }
    *id = read_le32(header);
    *size = read_le32(header + 4);
    *data_start = ftell(file);
    return *data_start >= 0;
}

static int hex_digit(uint8_t value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

static int stream_number(uint32_t id) {
    int high = hex_digit((uint8_t)id);
    int low = hex_digit((uint8_t)(id >> 8));
    return high < 0 || low < 0 ? -1 : (high << 4) | low;
}

static bool is_video_chunk(uint32_t id, uint8_t stream) {
    uint8_t c2 = (uint8_t)(id >> 16);
    uint8_t c3 = (uint8_t)(id >> 24);
    return stream_number(id) == stream &&
           ((c2 == 'd' && c3 == 'c') ||
            (c2 == 'd' && c3 == 'b'));
}

static bool is_audio_chunk(uint32_t id, uint8_t stream) {
    return stream_number(id) == stream &&
           (uint8_t)(id >> 16) == 'w' &&
           (uint8_t)(id >> 24) == 'b';
}

static int parse_stream_list(FILE *file,
                             long end,
                             uint8_t stream_index,
                             mjpeg_avi_info_t *info) {
    stream_header_t stream = {0};
    uint8_t format[40] = {0};
    size_t format_size = 0;

    while (ftell(file) >= 0 && ftell(file) + 8 <= end) {
        uint32_t id = 0;
        uint32_t size = 0;
        long data_start = 0;
        uint64_t data_end;
        if (!read_chunk_header(file, &id, &size, &data_start)) {
            return MJPEG_AVI_ERR_IO;
        }
        data_end = (uint64_t)data_start + size;
        if (data_end > (uint64_t)end) {
            return MJPEG_AVI_ERR_FORMAT;
        }

        if (id == fourcc('s', 't', 'r', 'h')) {
            uint8_t bytes[56] = {0};
            size_t wanted = min_size(size, sizeof bytes);
            if (wanted < 48U || !read_exact(file, bytes, wanted)) {
                return MJPEG_AVI_ERR_FORMAT;
            }
            stream.type = read_le32(bytes);
            stream.handler = read_le32(bytes + 4);
            stream.scale = read_le32(bytes + 20);
            stream.rate = read_le32(bytes + 24);
            stream.length = read_le32(bytes + 32);
            stream.suggested_buffer = read_le32(bytes + 36);
        } else if (id == fourcc('s', 't', 'r', 'f')) {
            format_size = min_size(size, sizeof format);
            if (!read_exact(file, format, format_size)) {
                return MJPEG_AVI_ERR_IO;
            }
        }
        if (!skip_chunk(file, data_start, size)) {
            return MJPEG_AVI_ERR_IO;
        }
    }

    if (stream.type == fourcc('v', 'i', 'd', 's') &&
        (stream.handler == fourcc('M', 'J', 'P', 'G') ||
         stream.handler == fourcc('m', 'j', 'p', 'g'))) {
        uint32_t compression;
        uint32_t width;
        int32_t signed_height;
        uint32_t height;
        if (stream.scale == 0U || stream.rate == 0U ||
            format_size < 20U) {
            return MJPEG_AVI_ERR_FORMAT;
        }
        compression = read_le32(format + 16);
        if (compression != fourcc('M', 'J', 'P', 'G') &&
            compression != fourcc('m', 'j', 'p', 'g')) {
            return MJPEG_AVI_ERR_FORMAT;
        }
        width = read_le32(format + 4);
        signed_height = (int32_t)read_le32(format + 8);
        height = signed_height < 0
                     ? (uint32_t)(-signed_height)
                     : (uint32_t)signed_height;
        if (width == 0U || height == 0U ||
            width > UINT16_MAX || height > UINT16_MAX) {
            return MJPEG_AVI_ERR_RANGE;
        }
        info->video_stream = stream_index;
        info->width = (uint16_t)width;
        info->height = (uint16_t)height;
        info->fps_num = stream.rate;
        info->fps_den = stream.scale;
        info->frame_count = stream.length;
        info->max_video_frame_size = stream.suggested_buffer;
    } else if (stream.type == fourcc('a', 'u', 'd', 's')) {
        if (format_size < 16U || read_le16(format) != 1U) {
            return MJPEG_AVI_ERR_FORMAT;
        }
        info->audio_stream = stream_index;
        info->audio_channels = (uint8_t)read_le16(format + 2);
        info->audio_sample_rate = read_le32(format + 4);
        info->audio_bits_per_sample =
            (uint8_t)read_le16(format + 14);
    }
    return MJPEG_AVI_OK;
}

static int parse_header_list(FILE *file,
                             long end,
                             mjpeg_avi_info_t *info) {
    uint8_t stream_index = 0;
    while (ftell(file) >= 0 && ftell(file) + 8 <= end) {
        uint32_t id = 0;
        uint32_t size = 0;
        long data_start = 0;
        uint64_t data_end;
        if (!read_chunk_header(file, &id, &size, &data_start)) {
            return MJPEG_AVI_ERR_IO;
        }
        data_end = (uint64_t)data_start + size;
        if (data_end > (uint64_t)end) {
            return MJPEG_AVI_ERR_FORMAT;
        }

        if (id == fourcc('L', 'I', 'S', 'T')) {
            uint8_t list_type[4];
            if (size < 4U ||
                !read_exact(file, list_type, sizeof list_type)) {
                return MJPEG_AVI_ERR_FORMAT;
            }
            if (read_le32(list_type) ==
                fourcc('s', 't', 'r', 'l')) {
                int result = parse_stream_list(
                    file, data_start + (long)size, stream_index++, info);
                if (result != MJPEG_AVI_OK) {
                    return result;
                }
            }
        }
        if (!skip_chunk(file, data_start, size)) {
            return MJPEG_AVI_ERR_IO;
        }
    }
    return MJPEG_AVI_OK;
}

static int scan_index(FILE *file,
                      long data_start,
                      uint32_t size,
                      mjpeg_avi_info_t *info) {
    uint8_t entry[16];
    uint32_t offset;
    if (size % 16U != 0U) {
        return MJPEG_AVI_ERR_FORMAT;
    }
    if (!seek_absolute(file, data_start)) {
        return MJPEG_AVI_ERR_IO;
    }
    for (offset = 0; offset < size; offset += sizeof entry) {
        uint32_t id;
        if (!read_exact(file, entry, sizeof entry)) {
            return MJPEG_AVI_ERR_IO;
        }
        id = read_le32(entry);
        if (is_video_chunk(id, info->video_stream)) {
            info->max_video_frame_size =
                max_u32(info->max_video_frame_size,
                        read_le32(entry + 12));
        }
    }
    return MJPEG_AVI_OK;
}

static int next_payload(FILE *file,
                        const mjpeg_avi_info_t *info,
                        bool video,
                        uint32_t *size) {
    if (file == NULL || info == NULL || size == NULL) {
        return MJPEG_AVI_ERR_ARGUMENT;
    }
    for (;;) {
        long position = ftell(file);
        uint32_t id = 0;
        long data_start = 0;
        bool wanted;
        if (position < 0) {
            return MJPEG_AVI_ERR_IO;
        }
        if (position >= info->movi_end) {
            return MJPEG_AVI_EOF;
        }
        if (!read_chunk_header(file, &id, size, &data_start)) {
            return feof(file) ? MJPEG_AVI_EOF : MJPEG_AVI_ERR_IO;
        }
        if (id == fourcc('L', 'I', 'S', 'T')) {
            uint8_t list_type[4];
            if (*size < 4U ||
                !read_exact(file, list_type, sizeof list_type)) {
                return MJPEG_AVI_ERR_FORMAT;
            }
            /* LIST "rec " contains ordinary stream chunks. */
            if (read_le32(list_type) ==
                fourcc('r', 'e', 'c', ' ')) {
                continue;
            }
        }
        wanted = video
                     ? is_video_chunk(id, info->video_stream)
                     : is_audio_chunk(id, info->audio_stream);
        if (wanted) {
            return MJPEG_AVI_OK;
        }
        if (!skip_chunk(file, data_start, *size)) {
            return MJPEG_AVI_ERR_IO;
        }
    }
}

const char *mjpeg_avi_strerror(int result) {
    switch (result) {
        case MJPEG_AVI_OK: return "success";
        case MJPEG_AVI_EOF: return "end of file";
        case MJPEG_AVI_ERR_ARGUMENT: return "invalid argument";
        case MJPEG_AVI_ERR_MEMORY: return "not enough memory";
        case MJPEG_AVI_ERR_IO: return "I/O error";
        case MJPEG_AVI_ERR_FORMAT:
            return "unsupported AVI/MJPEG format";
        case MJPEG_AVI_ERR_RANGE: return "AVI value is out of range";
        case MJPEG_AVI_ERR_DECODE: return "JPEG decode error";
        default: return "unknown MJPEG error";
    }
}

int mjpeg_avi_read_info(FILE *file, mjpeg_avi_info_t *info) {
    uint8_t riff[12];
    uint32_t riff_size;
    uint64_t riff_end64;
    long riff_end;

    if (file == NULL || info == NULL) {
        return MJPEG_AVI_ERR_ARGUMENT;
    }
    memset(info, 0, sizeof *info);
    info->video_stream = 0xffU;
    info->audio_stream = 0xffU;
    if (!seek_absolute(file, 0)) {
        return MJPEG_AVI_ERR_IO;
    }
    if (!read_exact(file, riff, sizeof riff)) {
        return MJPEG_AVI_ERR_IO;
    }
    if (read_le32(riff) != fourcc('R', 'I', 'F', 'F') ||
        read_le32(riff + 8) != fourcc('A', 'V', 'I', ' ')) {
        return MJPEG_AVI_ERR_FORMAT;
    }
    riff_size = read_le32(riff + 4);
    riff_end64 = UINT64_C(8) + riff_size;
    if (riff_end64 > LONG_MAX || riff_end64 < 12U) {
        return MJPEG_AVI_ERR_RANGE;
    }
    riff_end = (long)riff_end64;

    while (ftell(file) >= 0 && ftell(file) + 8 <= riff_end) {
        uint32_t id = 0;
        uint32_t size = 0;
        long data_start = 0;
        uint64_t data_end;
        if (!read_chunk_header(file, &id, &size, &data_start)) {
            return MJPEG_AVI_ERR_IO;
        }
        data_end = (uint64_t)data_start + size;
        if (data_end > (uint64_t)riff_end) {
            return MJPEG_AVI_ERR_FORMAT;
        }
        if (id == fourcc('L', 'I', 'S', 'T')) {
            uint8_t list_type[4];
            uint32_t type;
            if (size < 4U ||
                !read_exact(file, list_type, sizeof list_type)) {
                return MJPEG_AVI_ERR_FORMAT;
            }
            type = read_le32(list_type);
            if (type == fourcc('h', 'd', 'r', 'l')) {
                int result = parse_header_list(
                    file, data_start + (long)size, info);
                if (result != MJPEG_AVI_OK) {
                    return result;
                }
            } else if (type == fourcc('m', 'o', 'v', 'i')) {
                info->movi_start = ftell(file);
                info->movi_end = data_start + (long)size;
            }
        } else if (id == fourcc('i', 'd', 'x', '1') &&
                   info->video_stream != 0xffU) {
            int result = scan_index(file, data_start, size, info);
            if (result != MJPEG_AVI_OK) {
                return result;
            }
        }
        if (!skip_chunk(file, data_start, size)) {
            return MJPEG_AVI_ERR_IO;
        }
    }

    if (info->video_stream == 0xffU ||
        info->width == 0U || info->height == 0U ||
        info->fps_num == 0U || info->fps_den == 0U ||
        info->movi_start == 0 ||
        info->movi_end <= info->movi_start) {
        return MJPEG_AVI_ERR_FORMAT;
    }
    if (info->audio_stream != 0xffU &&
        (info->audio_channels != 1U ||
         info->audio_bits_per_sample != 8U ||
         info->audio_sample_rate == 0U)) {
        return MJPEG_AVI_ERR_FORMAT;
    }
    if (info->max_video_frame_size == 0U) {
        info->max_video_frame_size = FALLBACK_FRAME_BYTES;
    }
    if (info->max_video_frame_size > MAXIMUM_FRAME_BYTES) {
        return MJPEG_AVI_ERR_RANGE;
    }
    return seek_absolute(file, info->movi_start)
               ? MJPEG_AVI_OK
               : MJPEG_AVI_ERR_IO;
}

int mjpeg_avi_next_audio_chunk(FILE *file,
                               const mjpeg_avi_info_t *info,
                               uint32_t *payload_size) {
    if (info == NULL) {
        return MJPEG_AVI_ERR_ARGUMENT;
    }
    if (info->audio_stream == 0xffU) {
        return MJPEG_AVI_EOF;
    }
    return next_payload(file, info, false, payload_size);
}

bool mjpeg_avi_decoder_ready(const mjpeg_avi_decoder_t *decoder) {
    return decoder != NULL &&
           decoder->compressed != NULL &&
           decoder->decoder != NULL &&
           (!decoder->need_strip || decoder->strip != NULL);
}

size_t mjpeg_avi_decoder_strip_buffer_bytes(
    const mjpeg_avi_decoder_t *decoder) {
    return decoder != NULL
               ? (size_t)decoder->info.width * MJPEG_AVI_STRIP_ROWS *
                     sizeof(uint16_t)
               : 0U;
}

int mjpeg_avi_decoder_begin(mjpeg_avi_decoder_t *decoder,
                            FILE *file,
                            mjpeg_avi_info_t *info,
                            bool need_strip) {
    int result;
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    jpeg_dec_handle_t handle = NULL;

    if (decoder == NULL) {
        return MJPEG_AVI_ERR_ARGUMENT;
    }
#ifdef MJPEG_FIXED_RGB565
    mjpeg_install_fixed_rgb565();
#endif
    mjpeg_avi_decoder_end(decoder);
    result = mjpeg_avi_read_info(file, &decoder->info);
    if (result != MJPEG_AVI_OK) {
        return result;
    }
    if (info != NULL) {
        *info = decoder->info;
    }

    decoder->compressed_capacity =
        decoder->info.max_video_frame_size;
    decoder->packet_offset = -1;
    decoder->decode_height = decoder->info.height;
    decoder->need_strip = need_strip;
    decoder->compressed =
        (uint8_t *)heap_caps_malloc(decoder->compressed_capacity,
                                    MALLOC_CAP_8BIT);
    if (decoder->need_strip) {
        decoder->strip = (uint16_t *)heap_caps_aligned_alloc(
            16, mjpeg_avi_decoder_strip_buffer_bytes(decoder),
            MALLOC_CAP_8BIT);
    }
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    config.block_enable = true;
    if (jpeg_dec_open(&config, &handle) == JPEG_ERR_OK) {
        decoder->decoder = handle;
    }
    if (!mjpeg_avi_decoder_ready(decoder)) {
        mjpeg_avi_decoder_end(decoder);
        return MJPEG_AVI_ERR_MEMORY;
    }
    ESP_LOGI(k_tag,
             "MJPEG buffers: compressed=%u, RGB565 strip=%u bytes",
             (unsigned)decoder->compressed_capacity,
             (unsigned)(decoder->strip != NULL
                            ? mjpeg_avi_decoder_strip_buffer_bytes(decoder)
                            : 0U));
    return MJPEG_AVI_OK;
}

void mjpeg_avi_decoder_end(mjpeg_avi_decoder_t *decoder) {
    if (decoder == NULL) {
        return;
    }
    if (decoder->decoder != NULL) {
        jpeg_dec_close((jpeg_dec_handle_t)decoder->decoder);
    }
    heap_caps_free(decoder->compressed);
    heap_caps_free(decoder->strip);
    memset(decoder, 0, sizeof *decoder);
    decoder->packet_offset = -1;
}

const mjpeg_avi_info_t *mjpeg_avi_decoder_info(
    const mjpeg_avi_decoder_t *decoder) {
    return decoder != NULL ? &decoder->info : NULL;
}

size_t mjpeg_avi_decoder_compressed_capacity(
    const mjpeg_avi_decoder_t *decoder) {
    return decoder != NULL ? decoder->compressed_capacity : 0U;
}

long mjpeg_avi_decoder_last_packet_offset(
    const mjpeg_avi_decoder_t *decoder) {
    return decoder != NULL ? decoder->packet_offset : -1L;
}

const mjpeg_avi_decode_cycles_t *mjpeg_avi_decoder_last_decode_cycles(
    const mjpeg_avi_decoder_t *decoder) {
    return decoder != NULL ? &decoder->last_decode_cycles : NULL;
}

int mjpeg_avi_decoder_read_packet(mjpeg_avi_decoder_t *decoder,
                                  FILE *file,
                                  mjpeg_avi_packet_t *packet) {
    uint32_t size = 0;
    int result;
    long payload_start;
    uint64_t next_position;

    if (!mjpeg_avi_decoder_ready(decoder) ||
        file == NULL || packet == NULL) {
        return MJPEG_AVI_ERR_ARGUMENT;
    }
    memset(packet, 0, sizeof *packet);
    decoder->packet_offset = ftell(file);
    if (decoder->packet_offset < 0) {
        return MJPEG_AVI_ERR_IO;
    }
    result = next_payload(file, &decoder->info, true, &size);
    if (result != MJPEG_AVI_OK) {
        if (result != MJPEG_AVI_EOF) {
            ESP_LOGE(k_tag, "Packet %u scan failed at %ld: %s",
                     (unsigned)decoder->packet_index,
                     ftell(file), mjpeg_avi_strerror(result));
        }
        return result;
    }
    payload_start = ftell(file);
    if (payload_start < 0) {
        ESP_LOGE(k_tag, "Packet %u has no readable payload position",
                 (unsigned)decoder->packet_index);
        return MJPEG_AVI_ERR_IO;
    }
    if (size < 4U || size > decoder->compressed_capacity) {
        ESP_LOGE(k_tag,
                 "Packet %u size %u exceeds range 4..%u at %ld",
                 (unsigned)decoder->packet_index, (unsigned)size,
                 (unsigned)decoder->compressed_capacity, payload_start);
        return MJPEG_AVI_ERR_RANGE;
    }
    if (!read_exact(file, decoder->compressed, size)) {
        ESP_LOGE(k_tag,
                 "Packet %u payload read failed at %ld (%u bytes)",
                 (unsigned)decoder->packet_index, payload_start,
                 (unsigned)size);
        return MJPEG_AVI_ERR_IO;
    }
    next_position =
        (uint64_t)payload_start + size + (size & 1U);
    if (next_position > LONG_MAX ||
        !seek_absolute(file, (long)next_position)) {
        ESP_LOGE(k_tag, "Packet %u padding seek failed after %ld",
                 (unsigned)decoder->packet_index, payload_start);
        return MJPEG_AVI_ERR_IO;
    }
    if (decoder->compressed[0] != 0xffU ||
        decoder->compressed[1] != 0xd8U ||
        decoder->compressed[size - 2U] != 0xffU ||
        decoder->compressed[size - 1U] != 0xd9U) {
        ESP_LOGE(k_tag,
                 "Packet %u JPEG markers are invalid at %ld "
                 "(%02x%02x..%02x%02x)",
                 (unsigned)decoder->packet_index, payload_start,
                 decoder->compressed[0], decoder->compressed[1],
                 decoder->compressed[size - 2U],
                 decoder->compressed[size - 1U]);
        return MJPEG_AVI_ERR_FORMAT;
    }
    if (!prepare_jpeg_decode_height(
            decoder->compressed, size, decoder->info.width,
            decoder->info.height, &decoder->decode_height)) {
        ESP_LOGE(k_tag,
                 "Packet %u cannot prepare JPEG height %u for hardware",
                 (unsigned)decoder->packet_index,
                 (unsigned)decoder->info.height);
        return MJPEG_AVI_ERR_FORMAT;
    }
    packet->jpeg = decoder->compressed;
    packet->jpeg_size = size;
    ++decoder->packet_index;
    return MJPEG_AVI_OK;
}

static int decode_impl(mjpeg_avi_decoder_t *decoder,
                       const mjpeg_avi_packet_t *packet,
                       mjpeg_avi_strip_acquire_t acquire,
                       mjpeg_avi_strip_output_t output,
                       void *output_context) {
    jpeg_dec_io_t io = {0};
    jpeg_dec_header_info_t header = {0};
    jpeg_dec_handle_t handle;
    jpeg_error_t header_result;
    jpeg_error_t output_length_result;
    jpeg_error_t process_count_result;
    int output_bytes = 0;
    int process_count = 0;
    uint16_t block_rows;
    uint16_t decoded_y = 0;
    int block;
#ifdef MJPEG_PHASE_TIMING
    uint32_t phase_start;
    memset(&decoder->last_decode_cycles, 0,
           sizeof decoder->last_decode_cycles);
#endif

    if (!mjpeg_avi_decoder_ready(decoder) || packet == NULL ||
        packet->jpeg == NULL || packet->jpeg_size == 0U ||
        output == NULL) {
        return MJPEG_AVI_ERR_ARGUMENT;
    }
    if (acquire == NULL && decoder->strip == NULL) {
        return MJPEG_AVI_ERR_ARGUMENT;
    }
    io.inbuf = (uint8_t *)packet->jpeg;
    io.inbuf_len = (int)packet->jpeg_size;
    handle = (jpeg_dec_handle_t)decoder->decoder;
#ifdef MJPEG_PHASE_TIMING
    phase_start = esp_cpu_get_cycle_count();
#endif
    header_result = jpeg_dec_parse_header(handle, &io, &header);
#ifdef MJPEG_PHASE_TIMING
    decoder->last_decode_cycles.parse_header =
        esp_cpu_get_cycle_count() - phase_start;
#endif
    if (header_result != JPEG_ERR_OK ||
        header.width != decoder->info.width ||
        header.height != decoder->decode_height) {
        ESP_LOGE(k_tag, "esp_new_jpeg header failed (%ux%u)",
                 header.width, header.height);
        return MJPEG_AVI_ERR_DECODE;
    }

#ifdef MJPEG_PHASE_TIMING
    phase_start = esp_cpu_get_cycle_count();
#endif
    output_length_result =
        jpeg_dec_get_outbuf_len(handle, &output_bytes);
    process_count_result =
        jpeg_dec_get_process_count(handle, &process_count);
#ifdef MJPEG_PHASE_TIMING
    decoder->last_decode_cycles.geometry =
        esp_cpu_get_cycle_count() - phase_start;
#endif
    if (output_length_result != JPEG_ERR_OK ||
        process_count_result != JPEG_ERR_OK ||
        output_bytes <= 0 ||
        output_bytes >
            (int)mjpeg_avi_decoder_strip_buffer_bytes(decoder) ||
        output_bytes %
            (decoder->info.width * (int)sizeof(uint16_t)) != 0 ||
        process_count <= 0) {
        ESP_LOGE(k_tag, "esp_new_jpeg block geometry failed");
        return MJPEG_AVI_ERR_DECODE;
    }

    block_rows = (uint16_t)(
        output_bytes /
        (decoder->info.width * sizeof(uint16_t)));
    for (block = 0; block < process_count; ++block) {
        const uint16_t expected_rows = min_u16(
            block_rows, (uint16_t)(decoder->decode_height - decoded_y));
        const uint16_t visible_rows =
            decoded_y < decoder->info.height
                ? min_u16(
                      expected_rows,
                      (uint16_t)(decoder->info.height - decoded_y))
                : 0U;
        uint16_t *destination =
            acquire != NULL
                ? acquire(output_context, decoded_y, visible_rows)
                : decoder->strip;
        jpeg_error_t process_result;
        uint16_t rows;
        if (destination == NULL) {
            return MJPEG_AVI_ERR_IO;
        }
        io.outbuf = (uint8_t *)destination;
#ifdef MJPEG_PHASE_TIMING
        phase_start = esp_cpu_get_cycle_count();
#endif
        process_result = jpeg_dec_process(handle, &io);
#ifdef MJPEG_PHASE_TIMING
        decoder->last_decode_cycles.process +=
            esp_cpu_get_cycle_count() - phase_start;
#endif
        if (process_result != JPEG_ERR_OK ||
            io.out_size <= 0 ||
            io.out_size %
                (decoder->info.width * (int)sizeof(uint16_t)) != 0) {
            ESP_LOGE(k_tag, "esp_new_jpeg block %d failed", block);
            return MJPEG_AVI_ERR_DECODE;
        }
        rows = (uint16_t)(
            io.out_size /
            (decoder->info.width * sizeof(uint16_t)));
        if (rows != expected_rows ||
            visible_rows == 0U ||
            !output(
                output_context, destination, decoded_y, visible_rows)) {
            return MJPEG_AVI_ERR_IO;
        }
        decoded_y = (uint16_t)(decoded_y + rows);
    }
    return decoded_y == decoder->decode_height
               ? MJPEG_AVI_OK
               : MJPEG_AVI_ERR_DECODE;
}

int mjpeg_avi_decoder_decode(mjpeg_avi_decoder_t *decoder,
                             const mjpeg_avi_packet_t *packet,
                             mjpeg_avi_strip_output_t output,
                             void *output_context) {
    return decode_impl(decoder, packet, NULL, output, output_context);
}

int mjpeg_avi_decoder_decode_direct(
    mjpeg_avi_decoder_t *decoder,
    const mjpeg_avi_packet_t *packet,
    mjpeg_avi_strip_acquire_t acquire,
    mjpeg_avi_strip_output_t output,
    void *output_context) {
    if (acquire == NULL) {
        return MJPEG_AVI_ERR_ARGUMENT;
    }
    return decode_impl(decoder, packet, acquire, output, output_context);
}
