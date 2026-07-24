#ifndef BPV1_H
#define BPV1_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BPV1_VERSION 4
#define BPV1_AUDIO_VERSION 3
#define BPV1_VIDEO_VERSION 2
#define BPV1_LEGACY_VERSION 1
#define BPV1_BLOCK_SIZE 4
#define BPV1_RECORD_BYTES 9
#define BPV1_PATTERN_BYTES 4
#define BPV1_COLORS_PER_PALETTE 16
#define BPV1_PALETTE_COUNT 64
#define BPV1_MAX_PALETTE_BYTES \
    (BPV1_PALETTE_COUNT * BPV1_COLORS_PER_PALETTE * 3)

#define BPV1_AUDIO_NONE 0
#define BPV1_AUDIO_PCM_U8 1

enum {
    BPV1_OK = 0,
    BPV1_EOF = 1,
    BPV1_ERR_ARGUMENT = -1,
    BPV1_ERR_MEMORY = -2,
    BPV1_ERR_IO = -3,
    BPV1_ERR_FORMAT = -4,
    BPV1_ERR_RANGE = -5,
    BPV1_ERR_DECODE = -6
};

typedef struct {
    uint8_t version;
    uint16_t width;
    uint16_t height;
    uint32_t frame_count;
    uint16_t fps_num;
    uint16_t fps_den;
    uint16_t keyframe_interval;
    uint16_t max_block_dictionary;
    uint16_t max_pattern_dictionary;
    uint8_t search_radius;
    uint16_t audio_sample_rate;
    uint8_t audio_codec;
    uint8_t audio_channels;
    uint8_t palette_count;
    uint8_t palette[BPV1_MAX_PALETTE_BYTES];
} BPV1Header;

typedef struct {
    uint8_t keyframe;
    uint32_t frame_bytes;
    uint32_t mode_bytes;
    uint32_t audio_bytes;
} BPV1FrameInfo;

typedef struct {
    BPV1FrameInfo info;
    const uint8_t *data;
    size_t size;
    const uint8_t *audio_data;
    size_t audio_size;
} BPV1Packet;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t blocks_x;
    uint16_t blocks_y;
    uint32_t block_count;
    uint32_t frame_index;
    uint8_t keyframe;
    const uint8_t *blocks;
    const uint8_t *palette;
} BPV1Frame;

typedef struct BPV1Decoder BPV1Decoder;

const char *bpv1_strerror(int result);

/*
 * Read and validate the fixed header. Versions 1 through 3 store one palette
 * bank in the header; v4 carries a complete active bank in every keyframe.
 */
int bpv1_header_read(FILE *file, BPV1Header *header);

/*
 * Read one frame header and leave the file positioned at its payload.  The
 * header occupies nine bytes in v1/v2 and thirteen bytes in v3/v4. This is
 * useful for seek-index construction.
 */
int bpv1_frame_info_read(FILE *file, const BPV1Header *header,
                         BPV1FrameInfo *info);

BPV1Decoder *bpv1_decoder_create(const BPV1Header *header);
void bpv1_decoder_destroy(BPV1Decoder *decoder);
void bpv1_decoder_reset(BPV1Decoder *decoder);
size_t bpv1_decoder_packet_capacity(const BPV1Decoder *decoder);
size_t bpv1_decoder_memory_bytes(const BPV1Decoder *decoder);

/*
 * The packet payload belongs to the decoder and remains valid until the next
 * packet read or decoder destruction.
 */
int bpv1_decoder_read_packet(BPV1Decoder *decoder, FILE *file,
                             BPV1Packet *packet);
int bpv1_decoder_decode(BPV1Decoder *decoder, const BPV1Packet *packet,
                        const BPV1Frame **frame);

size_t bpv1_packet_audio_size(const BPV1Packet *packet);
const uint8_t *bpv1_packet_audio_data(const BPV1Packet *packet);

/* Render one source row without allocating a complete RGB framebuffer. */
int bpv1_frame_render_rgb24_row(const BPV1Header *header,
                                const BPV1Frame *frame, uint16_t y,
                                uint8_t *rgb, size_t rgb_bytes);
int bpv1_frame_render_rgb565_row(const BPV1Header *header,
                                 const BPV1Frame *frame, uint16_t y,
                                 uint16_t *rgb565, size_t pixels);
/*
 * Render consecutive source rows into a strided RGB565 buffer. Processing
 * rows in 4-row block groups reuses each block's four converted colours.
 */
int bpv1_frame_render_rgb565_rows(const BPV1Header *header,
                                  const BPV1Frame *frame, uint16_t y,
                                  uint16_t rows, uint16_t *rgb565,
                                  size_t stride_pixels, size_t pixels);

#ifdef __cplusplus
}
#endif

#endif
