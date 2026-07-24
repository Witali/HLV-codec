#include "mjpeg_avi_decoder.hpp"

#include <algorithm>
#include <climits>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp32/rom/tjpgd.h"

namespace {

constexpr char kTag[] = "mjpeg-avi";
constexpr size_t kJpegWorkBytes = 4096;
constexpr uint32_t kFallbackFrameBytes = 128 * 1024;
constexpr uint32_t kMaximumFrameBytes = 1024 * 1024;
constexpr unsigned kIoAttempts = 3;
constexpr uint32_t kIoRetryDelayUs = 2000;

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

struct DecodeContext {
    const uint8_t *input = nullptr;
    size_t input_size = 0;
    size_t input_offset = 0;
    uint16_t *strip = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t strip_y = UINT16_MAX;
    uint16_t strip_rows = 0;
    MjpegAviStripOutput output = nullptr;
    void *output_context = nullptr;
    bool output_failed = false;
};

UINT jpegInput(JDEC *decoder, BYTE *buffer, UINT requested) {
    auto *context = static_cast<DecodeContext *>(decoder->device);
    if (!context || context->input_offset > context->input_size) return 0;
    const size_t available = context->input_size - context->input_offset;
    const size_t count = std::min<size_t>(requested, available);
    if (buffer && count) {
        std::memcpy(buffer, context->input + context->input_offset, count);
    }
    context->input_offset += count;
    return static_cast<UINT>(count);
}

UINT jpegOutput(JDEC *decoder, void *bitmap, JRECT *rect) {
    auto *context = static_cast<DecodeContext *>(decoder->device);
    if (!context || !bitmap || !rect ||
        rect->right >= context->width || rect->bottom >= context->height)
        return 0;
    if (context->strip_y == UINT16_MAX) {
        context->strip_y = rect->top;
        context->strip_rows =
            static_cast<uint16_t>(rect->bottom - rect->top + 1U);
    } else if (rect->top != context->strip_y ||
               rect->bottom - rect->top + 1U != context->strip_rows) {
        return 0;
    }
    if (context->strip_rows > 16U) return 0;

    const auto *rgb = static_cast<const uint8_t *>(bitmap);
    const size_t block_width = rect->right - rect->left + 1U;
    for (size_t y = rect->top; y <= rect->bottom; ++y) {
        uint16_t *destination =
            context->strip +
            (y - context->strip_y) * context->width + rect->left;
        for (size_t x = 0; x < block_width; ++x) {
            const uint8_t red = *rgb++;
            const uint8_t green = *rgb++;
            const uint8_t blue = *rgb++;
            destination[x] =
                static_cast<uint16_t>(((red & 0xf8U) << 8) |
                                      ((green & 0xfcU) << 3) |
                                      (blue >> 3));
        }
    }
    if (rect->right + 1U == context->width) {
        if (!context->output(
                context->output_context, context->strip,
                context->strip_y, context->strip_rows)) {
            context->output_failed = true;
            return 0;
        }
        context->strip_y = UINT16_MAX;
        context->strip_rows = 0;
    }
    return 1;
}

}  // namespace

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

int MjpegAviDecoder::begin(FILE *file, MjpegAviInfo *info) {
    end();
    int result = mjpeg_avi_read_info(file, &info_);
    if (result != MJPEG_AVI_OK) return result;
    if (info) *info = info_;

    compressed_capacity_ = info_.max_video_frame_size;
    packet_index_ = 0;
    packet_offset_ = -1;
    compressed_ = static_cast<uint8_t *>(
        heap_caps_malloc(compressed_capacity_, MALLOC_CAP_8BIT));
    strip_ = static_cast<uint16_t *>(
        heap_caps_malloc(stripBufferBytes(), MALLOC_CAP_8BIT));
    work_buffer_ = static_cast<uint8_t *>(
        heap_caps_malloc(kJpegWorkBytes, MALLOC_CAP_8BIT));
    if (!ready()) {
        end();
        return MJPEG_AVI_ERR_MEMORY;
    }
    ESP_LOGI(kTag,
             "MJPEG buffers: compressed=%u, RGB565 strip=%u, work=%u bytes",
             static_cast<unsigned>(compressed_capacity_),
             static_cast<unsigned>(stripBufferBytes()),
             static_cast<unsigned>(kJpegWorkBytes));
    return MJPEG_AVI_OK;
}

void MjpegAviDecoder::end() {
    heap_caps_free(compressed_);
    heap_caps_free(strip_);
    heap_caps_free(work_buffer_);
    compressed_ = nullptr;
    strip_ = nullptr;
    work_buffer_ = nullptr;
    compressed_capacity_ = 0;
    packet_index_ = 0;
    packet_offset_ = -1;
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
    if (size < 4 || size > compressed_capacity_) {
        ESP_LOGE(kTag,
                 "Packet %u size %u exceeds range 4..%u at %ld",
                 static_cast<unsigned>(packet_index_),
                 static_cast<unsigned>(size),
                 static_cast<unsigned>(compressed_capacity_),
                 payload_start);
        return MJPEG_AVI_ERR_RANGE;
    }
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
    packet->jpeg = compressed_;
    packet->jpeg_size = size;
    ++packet_index_;
    return MJPEG_AVI_OK;
}

int MjpegAviDecoder::decode(const MjpegAviPacket &packet,
                            MjpegAviStripOutput output,
                            void *output_context) {
    if (!ready() || !packet.jpeg || !packet.jpeg_size || !output)
        return MJPEG_AVI_ERR_ARGUMENT;
    DecodeContext context{
        packet.jpeg, packet.jpeg_size, 0, strip_,
        info_.width, info_.height, UINT16_MAX, 0,
        output, output_context, false};
    JDEC decoder{};
    const JRESULT prepare = jd_prepare(
        &decoder, jpegInput, work_buffer_, kJpegWorkBytes, &context);
    if (prepare != JDR_OK || decoder.width != info_.width ||
        decoder.height != info_.height) {
        ESP_LOGE(kTag, "jd_prepare failed: %d (%ux%u)",
                 static_cast<int>(prepare), decoder.width, decoder.height);
        return MJPEG_AVI_ERR_DECODE;
    }
    const JRESULT decoded = jd_decomp(&decoder, jpegOutput, 0);
    if (context.output_failed) return MJPEG_AVI_ERR_IO;
    if (decoded != JDR_OK || context.strip_y != UINT16_MAX) {
        ESP_LOGE(kTag, "jd_decomp failed: %d",
                 static_cast<int>(decoded));
        return MJPEG_AVI_ERR_DECODE;
    }
    return MJPEG_AVI_OK;
}
