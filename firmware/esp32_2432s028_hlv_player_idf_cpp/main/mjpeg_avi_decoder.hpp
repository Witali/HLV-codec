#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

enum MjpegAviResult {
    MJPEG_AVI_OK = 0,
    MJPEG_AVI_EOF = 1,
    MJPEG_AVI_ERR_ARGUMENT = -1,
    MJPEG_AVI_ERR_MEMORY = -2,
    MJPEG_AVI_ERR_IO = -3,
    MJPEG_AVI_ERR_FORMAT = -4,
    MJPEG_AVI_ERR_RANGE = -5,
    MJPEG_AVI_ERR_DECODE = -6,
};

struct MjpegAviInfo {
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
    uint32_t frame_count = 0;
    uint32_t audio_sample_rate = 0;
    uint32_t max_video_frame_size = 0;
    uint8_t audio_channels = 0;
    uint8_t audio_bits_per_sample = 0;
    uint8_t video_stream = 0xff;
    uint8_t audio_stream = 0xff;
    long movi_start = 0;
    long movi_end = 0;
};

struct MjpegAviPacket {
    const uint8_t *jpeg = nullptr;
    size_t jpeg_size = 0;
};

struct MjpegAviDecodeCycles {
    uint32_t parse_header = 0;
    uint32_t geometry = 0;
    uint32_t process = 0;
};

using MjpegAviStripOutput = bool (*)(
    void *context, const uint16_t *rgb565, uint16_t y, uint16_t rows);
using MjpegAviStripAcquire = uint16_t *(*)(
    void *context, uint16_t y, uint16_t rows);

const char *mjpeg_avi_strerror(int result);
int mjpeg_avi_read_info(FILE *file, MjpegAviInfo *info);

// Find the next PCM audio chunk and leave the file positioned at its payload.
// The caller reads exactly payload_size bytes and consumes one padding byte
// when payload_size is odd.
int mjpeg_avi_next_audio_chunk(FILE *file, const MjpegAviInfo &info,
                               uint32_t *payload_size);

class MjpegAviDecoder {
public:
    int begin(FILE *file, MjpegAviInfo *info, bool need_strip = true);
    void end();

    bool ready() const {
        return compressed_ != nullptr && decoder_ != nullptr &&
               (!need_strip_ || strip_ != nullptr);
    }
    const MjpegAviInfo &info() const { return info_; }
    size_t compressedCapacity() const { return compressed_capacity_; }
    long lastPacketOffset() const { return packet_offset_; }
    const MjpegAviDecodeCycles &lastDecodeCycles() const {
        return last_decode_cycles_;
    }
    size_t stripBufferBytes() const {
        return static_cast<size_t>(info_.width) * kStripRows *
               sizeof(uint16_t);
    }

    int readPacket(FILE *file, MjpegAviPacket *packet);
    int decode(const MjpegAviPacket &packet, MjpegAviStripOutput output,
               void *output_context);
    int decodeDirect(const MjpegAviPacket &packet,
                     MjpegAviStripAcquire acquire,
                     MjpegAviStripOutput output, void *output_context);

private:
    static constexpr size_t kStripRows = 16;

    MjpegAviInfo info_{};
    uint8_t *compressed_ = nullptr;
    size_t compressed_capacity_ = 0;
    uint16_t *strip_ = nullptr;
    void *decoder_ = nullptr;
    uint32_t packet_index_ = 0;
    long packet_offset_ = -1;
    uint16_t decode_height_ = 0;
    bool need_strip_ = false;
    MjpegAviDecodeCycles last_decode_cycles_{};

    int decodeImpl(const MjpegAviPacket &packet,
                   MjpegAviStripAcquire acquire,
                   MjpegAviStripOutput output, void *output_context);
};
