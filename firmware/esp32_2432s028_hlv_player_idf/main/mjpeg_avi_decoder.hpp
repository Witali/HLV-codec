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

using MjpegAviStripOutput = bool (*)(
    void *context, const uint16_t *rgb565, uint16_t y, uint16_t rows);

const char *mjpeg_avi_strerror(int result);
int mjpeg_avi_read_info(FILE *file, MjpegAviInfo *info);

// Find the next PCM audio chunk and leave the file positioned at its payload.
// The caller reads exactly payload_size bytes and consumes one padding byte
// when payload_size is odd.
int mjpeg_avi_next_audio_chunk(FILE *file, const MjpegAviInfo &info,
                               uint32_t *payload_size);

class MjpegAviDecoder {
public:
    int begin(FILE *file, MjpegAviInfo *info);
    void end();

    bool ready() const {
        return compressed_ != nullptr && strip_ != nullptr &&
               work_buffer_ != nullptr;
    }
    const MjpegAviInfo &info() const { return info_; }
    size_t compressedCapacity() const { return compressed_capacity_; }
    size_t stripBufferBytes() const {
        return static_cast<size_t>(info_.width) * kStripRows *
               sizeof(uint16_t);
    }

    int readPacket(FILE *file, MjpegAviPacket *packet);
    int decode(const MjpegAviPacket &packet, MjpegAviStripOutput output,
               void *output_context);

private:
    static constexpr size_t kStripRows = 16;

    MjpegAviInfo info_{};
    uint8_t *compressed_ = nullptr;
    size_t compressed_capacity_ = 0;
    uint16_t *strip_ = nullptr;
    uint8_t *work_buffer_ = nullptr;
};
