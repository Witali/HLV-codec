#include "mjpeg_avi_decoder.hpp"

#include <algorithm>
#include <climits>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#ifdef MJPEG_PHASE_TIMING
#include "esp_cpu.h"
#endif

namespace {

constexpr char kTag[] = "mjpeg-avi";
constexpr uint32_t kFallbackFrameBytes = 128 * 1024;
constexpr uint32_t kMaximumFrameBytes = 1024 * 1024;
constexpr size_t kMinimumInputBufferBytes = 1024;
constexpr size_t kMaximumInputBufferBytes = 64 * 1024;
constexpr unsigned kIoAttempts = 3;
constexpr uint32_t kIoRetryDelayUs = 2000;

#ifndef MJPEG_INPUT_BUFFER_BYTES
#define MJPEG_INPUT_BUFFER_BYTES (8U * 1024U)
#endif

bool isJpegSofMarker(uint8_t marker) {
    return marker >= 0xc0U && marker <= 0xcfU &&
           marker != 0xc4U && marker != 0xc8U && marker != 0xccU;
}

bool prepareJpegDecodeHeight(
    uint8_t *jpeg, size_t size, uint16_t expected_width,
    uint16_t expected_height, uint16_t *decode_height) {
    size_t offset = 2U;
    if (!decode_height) return false;
    *decode_height = expected_height;
    if ((expected_height & 7U) == 0U) return true;
    if (!jpeg || size < 4U || jpeg[0] != 0xffU || jpeg[1] != 0xd8U)
        return false;

    while (offset + 1U < size) {
        if (jpeg[offset++] != 0xffU) return false;
        while (offset < size && jpeg[offset] == 0xffU) ++offset;
        if (offset >= size) return false;
        const uint8_t marker = jpeg[offset++];
        if (marker == 0x00U) return false;
        if (marker == 0xd8U || marker == 0x01U ||
            (marker >= 0xd0U && marker <= 0xd7U)) {
            continue;
        }
        if (marker == 0xd9U || marker == 0xdaU ||
            offset + 2U > size) {
            return false;
        }
        const uint16_t length = static_cast<uint16_t>(
            (static_cast<uint16_t>(jpeg[offset]) << 8) |
            jpeg[offset + 1U]);
        if (length < 2U || offset + length > size) return false;
        if (isJpegSofMarker(marker)) {
            if (length < 8U) return false;
            const uint16_t height = static_cast<uint16_t>(
                (static_cast<uint16_t>(jpeg[offset + 3U]) << 8) |
                jpeg[offset + 4U]);
            const uint16_t width = static_cast<uint16_t>(
                (static_cast<uint16_t>(jpeg[offset + 5U]) << 8) |
                jpeg[offset + 6U]);
            const uint8_t components = jpeg[offset + 7U];
            if (height != expected_height || width != expected_width ||
                components == 0U ||
                length < static_cast<uint16_t>(8U + 3U * components)) {
                return false;
            }
            uint8_t max_vertical_sampling = 0;
            for (size_t component = 0; component < components; ++component) {
                const uint8_t vertical_sampling =
                    jpeg[offset + 8U + component * 3U + 1U] & 0x0fU;
                max_vertical_sampling =
                    std::max(max_vertical_sampling, vertical_sampling);
            }
            if (!max_vertical_sampling) return false;
            const uint16_t aligned_height =
                static_cast<uint16_t>((expected_height + 7U) & ~7U);
            const uint16_t mcu_height =
                static_cast<uint16_t>(8U * max_vertical_sampling);
            if ((expected_height + mcu_height - 1U) / mcu_height !=
                (aligned_height + mcu_height - 1U) / mcu_height) {
                return false;
            }
            jpeg[offset + 3U] =
                static_cast<uint8_t>(aligned_height >> 8);
            jpeg[offset + 4U] = static_cast<uint8_t>(aligned_height);
            *decode_height = aligned_height;
            return true;
        }
        offset += length;
    }
    return false;
}

constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

uint16_t readLe16(const uint8_t *bytes) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(bytes[0]) |
        (static_cast<uint16_t>(bytes[1]) << 8));
}

uint32_t readLe32(const uint8_t *bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

bool seekAbsolute(FILE *file, long position);

bool readExact(FILE *file, void *buffer, size_t size) {
    if (!file || (!buffer && size)) return false;
    const long start = std::ftell(file);
    if (start < 0) return false;
    for (unsigned attempt = 0; attempt < kIoAttempts; ++attempt) {
        const size_t received = std::fread(buffer, 1, size, file);
        if (received == size) return true;
        if (attempt + 1U == kIoAttempts) return false;
        std::clearerr(file);
        if (!seekAbsolute(file, start)) return false;
        esp_rom_delay_us(kIoRetryDelayUs);
    }
    return false;
}

bool seekAbsolute(FILE *file, long position) {
    if (!file || position < 0) return false;
    for (unsigned attempt = 0; attempt < kIoAttempts; ++attempt) {
        std::clearerr(file);
        if (std::fseek(file, position, SEEK_SET) == 0) return true;
        if (attempt + 1U < kIoAttempts)
            esp_rom_delay_us(kIoRetryDelayUs);
    }
    return false;
}

bool skipChunk(FILE *file, long data_start, uint32_t size) {
    const uint64_t end = static_cast<uint64_t>(data_start) + size +
                         (size & 1U);
    return end <= LONG_MAX &&
           seekAbsolute(file, static_cast<long>(end));
}

bool readChunkHeader(FILE *file, uint32_t *id, uint32_t *size,
                     long *data_start) {
    uint8_t header[8];
    if (!readExact(file, header, sizeof header)) return false;
    *id = readLe32(header);
    *size = readLe32(header + 4);
    *data_start = std::ftell(file);
    return *data_start >= 0;
}

int streamNumber(uint32_t id) {
    const auto hex = [](uint8_t value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        return -1;
    };
    const int high = hex(static_cast<uint8_t>(id));
    const int low = hex(static_cast<uint8_t>(id >> 8));
    return high < 0 || low < 0 ? -1 : (high << 4) | low;
}

bool isVideoChunk(uint32_t id, uint8_t stream) {
    const uint8_t c2 = static_cast<uint8_t>(id >> 16);
    const uint8_t c3 = static_cast<uint8_t>(id >> 24);
    return streamNumber(id) == stream &&
           ((c2 == 'd' && c3 == 'c') ||
            (c2 == 'd' && c3 == 'b'));
}

bool isAudioChunk(uint32_t id, uint8_t stream) {
    return streamNumber(id) == stream &&
           static_cast<uint8_t>(id >> 16) == 'w' &&
           static_cast<uint8_t>(id >> 24) == 'b';
}

struct StreamHeader {
    uint32_t type = 0;
    uint32_t handler = 0;
    uint32_t scale = 0;
    uint32_t rate = 0;
    uint32_t length = 0;
    uint32_t suggested_buffer = 0;
};

int parseStreamList(FILE *file, long end, uint8_t stream_index,
                    MjpegAviInfo *info) {
    StreamHeader stream{};
    uint8_t format[40]{};
    size_t format_size = 0;

    while (std::ftell(file) >= 0 && std::ftell(file) + 8 <= end) {
        uint32_t id = 0;
        uint32_t size = 0;
        long data_start = 0;
        if (!readChunkHeader(file, &id, &size, &data_start))
            return MJPEG_AVI_ERR_IO;
        const uint64_t data_end =
            static_cast<uint64_t>(data_start) + size;
        if (data_end > static_cast<uint64_t>(end))
            return MJPEG_AVI_ERR_FORMAT;

        if (id == fourcc('s', 't', 'r', 'h')) {
            uint8_t bytes[56]{};
            const size_t wanted = std::min<size_t>(size, sizeof bytes);
            if (wanted < 48 || !readExact(file, bytes, wanted))
                return MJPEG_AVI_ERR_FORMAT;
            stream.type = readLe32(bytes);
            stream.handler = readLe32(bytes + 4);
            stream.scale = readLe32(bytes + 20);
            stream.rate = readLe32(bytes + 24);
            stream.length = readLe32(bytes + 32);
            stream.suggested_buffer = readLe32(bytes + 36);
        } else if (id == fourcc('s', 't', 'r', 'f')) {
            format_size = std::min<size_t>(size, sizeof format);
            if (!readExact(file, format, format_size))
                return MJPEG_AVI_ERR_IO;
        }
        if (!skipChunk(file, data_start, size))
            return MJPEG_AVI_ERR_IO;
    }

    if (stream.type == fourcc('v', 'i', 'd', 's') &&
        (stream.handler == fourcc('M', 'J', 'P', 'G') ||
         stream.handler == fourcc('m', 'j', 'p', 'g'))) {
        if (!stream.scale || !stream.rate || format_size < 20)
            return MJPEG_AVI_ERR_FORMAT;
        const uint32_t compression = readLe32(format + 16);
        if (compression != fourcc('M', 'J', 'P', 'G') &&
            compression != fourcc('m', 'j', 'p', 'g'))
            return MJPEG_AVI_ERR_FORMAT;
        const uint32_t width = readLe32(format + 4);
        const int32_t signed_height =
            static_cast<int32_t>(readLe32(format + 8));
        const uint32_t height = signed_height < 0
                                    ? static_cast<uint32_t>(-signed_height)
                                    : static_cast<uint32_t>(signed_height);
        if (!width || !height || width > UINT16_MAX ||
            height > UINT16_MAX)
            return MJPEG_AVI_ERR_RANGE;
        info->video_stream = stream_index;
        info->width = static_cast<uint16_t>(width);
        info->height = static_cast<uint16_t>(height);
        info->fps_num = stream.rate;
        info->fps_den = stream.scale;
        info->frame_count = stream.length;
        info->max_video_frame_size = stream.suggested_buffer;
    } else if (stream.type == fourcc('a', 'u', 'd', 's')) {
        if (format_size < 16 || readLe16(format) != 1)
            return MJPEG_AVI_ERR_FORMAT;
        info->audio_stream = stream_index;
        info->audio_channels =
            static_cast<uint8_t>(readLe16(format + 2));
        info->audio_sample_rate = readLe32(format + 4);
        info->audio_bits_per_sample =
            static_cast<uint8_t>(readLe16(format + 14));
    }
    return MJPEG_AVI_OK;
}

int parseHeaderList(FILE *file, long end, MjpegAviInfo *info) {
    uint8_t stream_index = 0;
    while (std::ftell(file) >= 0 && std::ftell(file) + 8 <= end) {
        uint32_t id = 0;
        uint32_t size = 0;
        long data_start = 0;
        if (!readChunkHeader(file, &id, &size, &data_start))
            return MJPEG_AVI_ERR_IO;
        const uint64_t data_end =
            static_cast<uint64_t>(data_start) + size;
        if (data_end > static_cast<uint64_t>(end))
            return MJPEG_AVI_ERR_FORMAT;

        if (id == fourcc('L', 'I', 'S', 'T')) {
            uint8_t list_type[4];
            if (size < 4 || !readExact(file, list_type, sizeof list_type))
                return MJPEG_AVI_ERR_FORMAT;
            if (readLe32(list_type) == fourcc('s', 't', 'r', 'l')) {
                const int result = parseStreamList(
                    file, data_start + static_cast<long>(size),
                    stream_index++, info);
                if (result != MJPEG_AVI_OK) return result;
            }
        }
        if (!skipChunk(file, data_start, size))
            return MJPEG_AVI_ERR_IO;
    }
    return MJPEG_AVI_OK;
}

int scanIndex(FILE *file, long data_start, uint32_t size,
              MjpegAviInfo *info) {
    if (size % 16U) return MJPEG_AVI_ERR_FORMAT;
    if (!seekAbsolute(file, data_start)) return MJPEG_AVI_ERR_IO;
    uint8_t entry[16];
    for (uint32_t offset = 0; offset < size; offset += sizeof entry) {
        if (!readExact(file, entry, sizeof entry))
            return MJPEG_AVI_ERR_IO;
        const uint32_t id = readLe32(entry);
        if (isVideoChunk(id, info->video_stream)) {
            info->max_video_frame_size =
                std::max(info->max_video_frame_size,
                         readLe32(entry + 12));
        }
    }
    return MJPEG_AVI_OK;
}

int nextPayload(FILE *file, const MjpegAviInfo &info, bool video,
                uint32_t *size) {
    if (!file || !size) return MJPEG_AVI_ERR_ARGUMENT;
    for (;;) {
        const long position = std::ftell(file);
        if (position < 0) return MJPEG_AVI_ERR_IO;
        if (position >= info.movi_end) return MJPEG_AVI_EOF;

        uint32_t id = 0;
        long data_start = 0;
        if (!readChunkHeader(file, &id, size, &data_start)) {
            return std::feof(file) ? MJPEG_AVI_EOF
                                   : MJPEG_AVI_ERR_IO;
        }
        if (id == fourcc('L', 'I', 'S', 'T')) {
            uint8_t list_type[4];
            if (*size < 4 ||
                !readExact(file, list_type, sizeof list_type))
                return MJPEG_AVI_ERR_FORMAT;
            // LIST "rec " contains ordinary stream chunks. Continue at its
            // first child instead of skipping the complete list.
            if (readLe32(list_type) == fourcc('r', 'e', 'c', ' '))
                continue;
        }

        const bool wanted =
            video ? isVideoChunk(id, info.video_stream)
                  : isAudioChunk(id, info.audio_stream);
        if (wanted) return MJPEG_AVI_OK;
        if (!skipChunk(file, data_start, *size))
            return MJPEG_AVI_ERR_IO;
    }
}

}  // namespace

#ifdef MJPEG_FIXED_RGB565
extern "C" void mjpeg_install_fixed_rgb565();
#endif

const char *mjpeg_avi_strerror(int result) {
    switch (result) {
        case MJPEG_AVI_OK: return "success";
        case MJPEG_AVI_EOF: return "end of file";
        case MJPEG_AVI_ERR_ARGUMENT: return "invalid argument";
        case MJPEG_AVI_ERR_MEMORY: return "not enough memory";
        case MJPEG_AVI_ERR_IO: return "I/O error";
        case MJPEG_AVI_ERR_FORMAT: return "unsupported AVI/MJPEG format";
        case MJPEG_AVI_ERR_RANGE: return "AVI value is out of range";
        case MJPEG_AVI_ERR_DECODE: return "JPEG decode error";
        default: return "unknown MJPEG error";
    }
}

int mjpeg_avi_read_info(FILE *file, MjpegAviInfo *info) {
    if (!file || !info) return MJPEG_AVI_ERR_ARGUMENT;
    *info = {};
    if (!seekAbsolute(file, 0)) return MJPEG_AVI_ERR_IO;

    uint8_t riff[12];
    if (!readExact(file, riff, sizeof riff)) return MJPEG_AVI_ERR_IO;
    if (readLe32(riff) != fourcc('R', 'I', 'F', 'F') ||
        readLe32(riff + 8) != fourcc('A', 'V', 'I', ' '))
        return MJPEG_AVI_ERR_FORMAT;
    const uint32_t riff_size = readLe32(riff + 4);
    const uint64_t riff_end64 = 8ULL + riff_size;
    if (riff_end64 > LONG_MAX || riff_end64 < 12)
        return MJPEG_AVI_ERR_RANGE;
    const long riff_end = static_cast<long>(riff_end64);

    while (std::ftell(file) >= 0 && std::ftell(file) + 8 <= riff_end) {
        uint32_t id = 0;
        uint32_t size = 0;
        long data_start = 0;
        if (!readChunkHeader(file, &id, &size, &data_start))
            return MJPEG_AVI_ERR_IO;
        const uint64_t data_end =
            static_cast<uint64_t>(data_start) + size;
        if (data_end > static_cast<uint64_t>(riff_end))
            return MJPEG_AVI_ERR_FORMAT;

        if (id == fourcc('L', 'I', 'S', 'T')) {
            uint8_t list_type[4];
            if (size < 4 || !readExact(file, list_type, sizeof list_type))
                return MJPEG_AVI_ERR_FORMAT;
            const uint32_t type = readLe32(list_type);
            if (type == fourcc('h', 'd', 'r', 'l')) {
                const int result = parseHeaderList(
                    file, data_start + static_cast<long>(size), info);
                if (result != MJPEG_AVI_OK) return result;
            } else if (type == fourcc('m', 'o', 'v', 'i')) {
                info->movi_start = std::ftell(file);
                info->movi_end =
                    data_start + static_cast<long>(size);
            }
        } else if (id == fourcc('i', 'd', 'x', '1') &&
                   info->video_stream != 0xff) {
            const int result = scanIndex(file, data_start, size, info);
            if (result != MJPEG_AVI_OK) return result;
        }
        if (!skipChunk(file, data_start, size))
            return MJPEG_AVI_ERR_IO;
    }

    if (info->video_stream == 0xff || !info->width || !info->height ||
        !info->fps_num || !info->fps_den || !info->movi_start ||
        info->movi_end <= info->movi_start)
        return MJPEG_AVI_ERR_FORMAT;
    if (info->audio_stream != 0xff &&
        (info->audio_channels != 1 ||
         info->audio_bits_per_sample != 8 ||
         !info->audio_sample_rate))
        return MJPEG_AVI_ERR_FORMAT;
    if (!info->max_video_frame_size)
        info->max_video_frame_size = kFallbackFrameBytes;
    if (info->max_video_frame_size > kMaximumFrameBytes)
        return MJPEG_AVI_ERR_RANGE;
    return seekAbsolute(file, info->movi_start)
               ? MJPEG_AVI_OK
               : MJPEG_AVI_ERR_IO;
}

int mjpeg_avi_next_audio_chunk(FILE *file, const MjpegAviInfo &info,
                               uint32_t *payload_size) {
    if (info.audio_stream == 0xff) return MJPEG_AVI_EOF;
    return nextPayload(file, info, false, payload_size);
}

int MjpegAviDecoder::begin(FILE *file, MjpegAviInfo *info,
                           bool need_strip) {
#ifdef MJPEG_STREAMING_INPUT
    if (MJPEG_INPUT_BUFFER_BYTES < kMinimumInputBufferBytes ||
        MJPEG_INPUT_BUFFER_BYTES > kMaximumInputBufferBytes) {
        return MJPEG_AVI_ERR_RANGE;
    }
#endif
    end();
    int result = mjpeg_avi_read_info(file, &info_);
    if (result != MJPEG_AVI_OK) return result;
    if (info) *info = info_;

#ifdef MJPEG_STREAMING_INPUT
    compressed_capacity_ = MJPEG_INPUT_BUFFER_BYTES;
#else
    compressed_capacity_ = info_.max_video_frame_size;
#endif
    packet_index_ = 0;
    packet_offset_ = -1;
    decode_height_ = info_.height;
    need_strip_ = need_strip;
    compressed_ = static_cast<uint8_t *>(
        heap_caps_malloc(compressed_capacity_, MALLOC_CAP_8BIT));
    if (need_strip_) {
        strip_ = static_cast<uint16_t *>(
            heap_caps_aligned_alloc(
                16, stripBufferBytes(), MALLOC_CAP_8BIT));
    }
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    config.block_enable = true;
#ifdef MJPEG_FIXED_RGB565
    mjpeg_install_fixed_rgb565();
#endif
    jpeg_dec_handle_t decoder = nullptr;
    if (jpeg_dec_open(&config, &decoder) == JPEG_ERR_OK) {
        decoder_ = decoder;
    }
    if (!ready()) {
        end();
        return MJPEG_AVI_ERR_MEMORY;
    }
    ESP_LOGI(kTag,
             "MJPEG buffers: compressed input=%u, RGB565 strip=%u bytes",
             static_cast<unsigned>(compressed_capacity_),
             static_cast<unsigned>(
                 strip_ ? stripBufferBytes() : 0));
    return MJPEG_AVI_OK;
}

void MjpegAviDecoder::end() {
    if (decoder_)
        jpeg_dec_close(static_cast<jpeg_dec_handle_t>(decoder_));
    heap_caps_free(compressed_);
    heap_caps_free(strip_);
    compressed_ = nullptr;
    strip_ = nullptr;
    decoder_ = nullptr;
    compressed_capacity_ = 0;
    packet_index_ = 0;
    packet_offset_ = -1;
    stream_file_ = nullptr;
    stream_remaining_ = 0;
    entropy_stream_ = {};
    decode_height_ = 0;
    stream_failed_ = false;
    need_strip_ = false;
    info_ = {};
}

int MjpegAviDecoder::readPacket(FILE *file, MjpegAviPacket *packet) {
    if (!ready() || !file || !packet) return MJPEG_AVI_ERR_ARGUMENT;
    *packet = {};
    packet_offset_ = std::ftell(file);
    if (packet_offset_ < 0) return MJPEG_AVI_ERR_IO;
    uint32_t size = 0;
    const int result = nextPayload(file, info_, true, &size);
    if (result != MJPEG_AVI_OK) {
        if (result != MJPEG_AVI_EOF) {
            ESP_LOGE(kTag, "Packet %u scan failed at %ld: %s",
                     static_cast<unsigned>(packet_index_),
                     file ? std::ftell(file) : -1L,
                     mjpeg_avi_strerror(result));
        }
        return result;
    }
    const long payload_start = std::ftell(file);
    if (payload_start < 0) {
        ESP_LOGE(kTag, "Packet %u has no readable payload position",
                 static_cast<unsigned>(packet_index_));
        return MJPEG_AVI_ERR_IO;
    }
    if (size < 4 ||
#ifdef MJPEG_STREAMING_INPUT
        size > kMaximumFrameBytes
#else
        size > compressed_capacity_
#endif
    ) {
        ESP_LOGE(kTag,
                 "Packet %u size %u exceeds range 4..%u at %ld",
                 static_cast<unsigned>(packet_index_),
                 static_cast<unsigned>(size),
#ifdef MJPEG_STREAMING_INPUT
                 static_cast<unsigned>(kMaximumFrameBytes),
#else
                 static_cast<unsigned>(compressed_capacity_),
#endif
                 payload_start);
        return MJPEG_AVI_ERR_RANGE;
    }
#ifdef MJPEG_STREAMING_INPUT
    const uint64_t next_position =
        static_cast<uint64_t>(payload_start) + size + (size & 1U);
    if (next_position > LONG_MAX ||
        !seekAbsolute(file, static_cast<long>(next_position))) {
        return MJPEG_AVI_ERR_IO;
    }
    packet->file = file;
    packet->payload_offset = payload_start;
    packet->next_offset = static_cast<long>(next_position);
    packet->jpeg_size = size;
#else
    if (!readExact(file, compressed_, size)) {
        ESP_LOGE(kTag, "Packet %u payload read failed at %ld (%u bytes)",
                 static_cast<unsigned>(packet_index_), payload_start,
                 static_cast<unsigned>(size));
        return MJPEG_AVI_ERR_IO;
    }
    const uint64_t next_position =
        static_cast<uint64_t>(payload_start) + size + (size & 1U);
    if (next_position > LONG_MAX ||
        !seekAbsolute(file, static_cast<long>(next_position))) {
        ESP_LOGE(kTag, "Packet %u padding seek failed after %ld",
                 static_cast<unsigned>(packet_index_), payload_start);
        return MJPEG_AVI_ERR_IO;
    }
    if (compressed_[0] != 0xff || compressed_[1] != 0xd8 ||
        compressed_[size - 2] != 0xff || compressed_[size - 1] != 0xd9) {
        ESP_LOGE(kTag,
                 "Packet %u JPEG markers are invalid at %ld "
                 "(%02x%02x..%02x%02x)",
                 static_cast<unsigned>(packet_index_), payload_start,
                 compressed_[0], compressed_[1],
                 compressed_[size - 2], compressed_[size - 1]);
        return MJPEG_AVI_ERR_FORMAT;
    }
    if (!prepareJpegDecodeHeight(
            compressed_, size, info_.width, info_.height,
            &decode_height_)) {
        ESP_LOGE(kTag,
                 "Packet %u cannot prepare JPEG height %u for hardware",
                 static_cast<unsigned>(packet_index_),
                 static_cast<unsigned>(info_.height));
        return MJPEG_AVI_ERR_FORMAT;
    }
    packet->jpeg = compressed_;
    packet->jpeg_size = size;
#endif
    ++packet_index_;
    return MJPEG_AVI_OK;
}

int MjpegAviDecoder::decode(const MjpegAviPacket &packet,
                            MjpegAviStripOutput output,
                            void *output_context) {
    return decodeImpl(packet, nullptr, output, output_context);
}

int MjpegAviDecoder::decodeDirect(const MjpegAviPacket &packet,
                                  MjpegAviStripAcquire acquire,
                                  MjpegAviStripOutput output,
                                  void *output_context) {
    if (!acquire) return MJPEG_AVI_ERR_ARGUMENT;
    return decodeImpl(packet, acquire, output, output_context);
}

size_t MjpegAviDecoder::refillStream(
    void *context, uint8_t *destination, size_t capacity) {
    auto *decoder = static_cast<MjpegAviDecoder *>(context);
    if (!decoder || !decoder->stream_file_ || !destination ||
        !capacity || !decoder->stream_remaining_) {
        return 0;
    }
    const size_t bytes = std::min<size_t>(
        capacity, decoder->stream_remaining_);
#ifdef MJPEG_PHASE_TIMING
    const uint32_t start = esp_cpu_get_cycle_count();
#endif
    if (!readExact(decoder->stream_file_, destination, bytes)) {
#ifdef MJPEG_PHASE_TIMING
        decoder->last_decode_cycles_.input +=
            esp_cpu_get_cycle_count() - start;
#endif
        decoder->stream_failed_ = true;
        return 0;
    }
#ifdef MJPEG_PHASE_TIMING
    decoder->last_decode_cycles_.input +=
        esp_cpu_get_cycle_count() - start;
#endif
    decoder->stream_remaining_ -= static_cast<uint32_t>(bytes);
    return bytes;
}

int MjpegAviDecoder::decodeImpl(
    const MjpegAviPacket &packet, MjpegAviStripAcquire acquire,
    MjpegAviStripOutput output, void *output_context) {
#ifdef MJPEG_PHASE_TIMING
    last_decode_cycles_ = {};
#endif
    if (!ready() ||
#ifdef MJPEG_STREAMING_INPUT
        !packet.file || packet.payload_offset < 0 ||
        packet.next_offset < 0 ||
#else
        !packet.jpeg ||
#endif
        !packet.jpeg_size || !output)
        return MJPEG_AVI_ERR_ARGUMENT;
    if (!acquire && !strip_) return MJPEG_AVI_ERR_ARGUMENT;
    jpeg_dec_io_t io{};
    jpeg_dec_header_info_t header{};
#ifdef MJPEG_STREAMING_INPUT
    if (!seekAbsolute(packet.file, packet.payload_offset))
        return MJPEG_AVI_ERR_IO;
    const size_t initial_bytes =
        std::min(compressed_capacity_, packet.jpeg_size);
#ifdef MJPEG_PHASE_TIMING
    uint32_t input_start = esp_cpu_get_cycle_count();
#endif
    if (!readExact(packet.file, compressed_, initial_bytes))
        return MJPEG_AVI_ERR_IO;
#ifdef MJPEG_PHASE_TIMING
    last_decode_cycles_.input +=
        esp_cpu_get_cycle_count() - input_start;
#endif
    if (!prepareJpegDecodeHeight(
            compressed_, initial_bytes, info_.width, info_.height,
            &decode_height_)) {
        seekAbsolute(packet.file, packet.next_offset);
        return MJPEG_AVI_ERR_FORMAT;
    }
    stream_file_ = packet.file;
    stream_remaining_ =
        static_cast<uint32_t>(packet.jpeg_size - initial_bytes);
    stream_failed_ = false;
    io.inbuf = compressed_;
    io.inbuf_len = static_cast<int>(initial_bytes);
#else
    io.inbuf = const_cast<uint8_t *>(packet.jpeg);
    io.inbuf_len = static_cast<int>(packet.jpeg_size);
#endif
    auto decoder = static_cast<jpeg_dec_handle_t>(decoder_);
#ifdef MJPEG_PHASE_TIMING
    uint32_t phase_start = esp_cpu_get_cycle_count();
#endif
    const jpeg_error_t header_result =
        jpeg_dec_parse_header(decoder, &io, &header);
#ifdef MJPEG_PHASE_TIMING
    last_decode_cycles_.parse_header =
        esp_cpu_get_cycle_count() - phase_start;
#endif
    if (header_result != JPEG_ERR_OK ||
        header.width != info_.width || header.height != decode_height_) {
        ESP_LOGE(kTag, "esp_new_jpeg header failed (%ux%u)",
                 header.width, header.height);
#ifdef MJPEG_STREAMING_INPUT
        seekAbsolute(packet.file, packet.next_offset);
#endif
        return MJPEG_AVI_ERR_DECODE;
    }

    int output_bytes = 0;
    int process_count = 0;
#ifdef MJPEG_PHASE_TIMING
    phase_start = esp_cpu_get_cycle_count();
#endif
    const jpeg_error_t output_length_result =
        jpeg_dec_get_outbuf_len(decoder, &output_bytes);
    const jpeg_error_t process_count_result =
        jpeg_dec_get_process_count(decoder, &process_count);
#ifdef MJPEG_PHASE_TIMING
    last_decode_cycles_.geometry =
        esp_cpu_get_cycle_count() - phase_start;
#endif
    if (output_length_result !=
            JPEG_ERR_OK ||
        process_count_result !=
            JPEG_ERR_OK ||
        output_bytes <= 0 ||
        output_bytes > static_cast<int>(stripBufferBytes()) ||
        output_bytes %
            (info_.width * static_cast<int>(sizeof(uint16_t))) ||
        process_count <= 0) {
        ESP_LOGE(kTag, "esp_new_jpeg block geometry failed");
#ifdef MJPEG_STREAMING_INPUT
        seekAbsolute(packet.file, packet.next_offset);
#endif
        return MJPEG_AVI_ERR_DECODE;
    }

    const uint16_t block_rows = static_cast<uint16_t>(
        output_bytes / (info_.width * sizeof(uint16_t)));
#ifdef MJPEG_STREAMING_INPUT
    if (!mjpeg_huffman_stream_attach(
            &entropy_stream_, decoder, compressed_,
            compressed_capacity_, refillStream, this)) {
        seekAbsolute(packet.file, packet.next_offset);
        return MJPEG_AVI_ERR_DECODE;
    }
#endif
    uint16_t decoded_y = 0;
    for (int block = 0; block < process_count; ++block) {
        const uint16_t expected_rows = std::min<uint16_t>(
            block_rows, decode_height_ - decoded_y);
        const uint16_t visible_rows =
            decoded_y < info_.height
                ? std::min<uint16_t>(
                      expected_rows, info_.height - decoded_y)
                : 0;
        uint16_t *destination =
            acquire ? acquire(output_context, decoded_y, visible_rows)
                    : strip_;
        if (!destination) {
#ifdef MJPEG_STREAMING_INPUT
            mjpeg_huffman_stream_detach(&entropy_stream_);
            seekAbsolute(packet.file, packet.next_offset);
#endif
            return MJPEG_AVI_ERR_IO;
        }
        io.outbuf = reinterpret_cast<uint8_t *>(destination);
#ifdef MJPEG_PHASE_TIMING
        phase_start = esp_cpu_get_cycle_count();
#endif
        const jpeg_error_t process_result =
            jpeg_dec_process(decoder, &io);
#ifdef MJPEG_PHASE_TIMING
        last_decode_cycles_.process +=
            esp_cpu_get_cycle_count() - phase_start;
#endif
        if (process_result != JPEG_ERR_OK ||
            io.out_size <= 0 ||
            io.out_size %
                (info_.width * static_cast<int>(sizeof(uint16_t)))) {
            ESP_LOGE(kTag, "esp_new_jpeg block %d failed", block);
#ifdef MJPEG_STREAMING_INPUT
            mjpeg_huffman_stream_detach(&entropy_stream_);
            seekAbsolute(packet.file, packet.next_offset);
#endif
            return MJPEG_AVI_ERR_DECODE;
        }
        const uint16_t rows = static_cast<uint16_t>(
            io.out_size / (info_.width * sizeof(uint16_t)));
        if (rows != expected_rows ||
            visible_rows == 0 ||
            !output(
                output_context, destination, decoded_y, visible_rows)) {
#ifdef MJPEG_STREAMING_INPUT
            mjpeg_huffman_stream_detach(&entropy_stream_);
            seekAbsolute(packet.file, packet.next_offset);
#endif
            return MJPEG_AVI_ERR_IO;
        }
        decoded_y = static_cast<uint16_t>(decoded_y + rows);
    }
#ifdef MJPEG_STREAMING_INPUT
    mjpeg_huffman_stream_detach(&entropy_stream_);
    if (!seekAbsolute(packet.file, packet.next_offset) || stream_failed_)
        return MJPEG_AVI_ERR_IO;
#endif
    return decoded_y == decode_height_ ? MJPEG_AVI_OK
                                       : MJPEG_AVI_ERR_DECODE;
}
