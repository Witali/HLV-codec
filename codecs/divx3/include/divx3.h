#ifndef HLV_DIVX3_H
#define HLV_DIVX3_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DIVX3_OK = 0,
    DIVX3_ERR_ARGUMENT = -1,
    DIVX3_ERR_MEMORY = -2,
    DIVX3_ERR_BITSTREAM = -3,
    DIVX3_ERR_UNSUPPORTED = -4,
};

typedef struct Divx3Decoder Divx3Decoder;

enum {
    DIVX3_FRAME_STORAGE_YUV420 = 0,
    DIVX3_FRAME_STORAGE_Y6_U5_V5 = 1,
    DIVX3_STREAM_BUFFER_BYTES = 4 * 1024
};

typedef size_t (*Divx3ReadFunction)(
    void *context, uint8_t *buffer, size_t capacity);

/* Sequential reader wrapper used after a caller has consumed one byte while
 * probing the picture header.  The saved byte is returned first, followed by
 * data from the wrapped reader, without repositioning the input stream. */
typedef struct Divx3ReplayReader {
    Divx3ReadFunction read;
    void *read_context;
    uint8_t prefix;
    uint8_t prefix_pending;
} Divx3ReplayReader;

size_t divx3_replay_read(
    void *context, uint8_t *buffer, size_t capacity);

typedef struct Divx3Frame {
    const uint8_t *y;
    const uint8_t *cb;
    const uint8_t *cr;
    const int8_t *correction_y;
    const int8_t *correction_cb;
    const int8_t *correction_cr;
    uint16_t width;
    uint16_t height;
    uint16_t y_stride;
    uint16_t c_stride;
    uint16_t correction_stride_y;
    uint16_t correction_stride_c;
    uint32_t frame_number;
    uint8_t storage_mode;
    uint8_t intra;
} Divx3Frame;

/*
 * Create an MS-MPEG4 v3 (DivX 3 / DIV3 / MP43) decoder.
 *
 * The decoder stores two padded YUV420 frames because P-frames reference the
 * preceding decoded picture. Dimensions are supplied by the container; the
 * MS-MPEG4 v3 elementary frame does not carry them.
 */
Divx3Decoder *divx3_decoder_create(uint16_t width, uint16_t height);

/*
 * Create the embedded decoder with packed Y6/U5/V5 reference frames and one
 * signed Q4 average-error correction per 8x8 plane block. This reduces memory
 * at the cost of non-bit-exact predictive references.
 */
Divx3Decoder *divx3_decoder_create_y6_u5_v5(uint16_t width, uint16_t height);
void divx3_decoder_destroy(Divx3Decoder *decoder);

/*
 * Decode one complete AVI video packet in display order.
 *
 * I- and P-pictures are supported. B-pictures and malformed/unsupported
 * picture modes return DIVX3_ERR_UNSUPPORTED or DIVX3_ERR_BITSTREAM without
 * replacing the last successfully decoded reference frame.
 */
int divx3_decoder_decode(Divx3Decoder *decoder, const uint8_t *packet,
                         size_t packet_size, Divx3Frame *frame);

/*
 * Decode one packet sequentially through the decoder-owned fixed 4 KB refill
 * buffer. The callback may return fewer than capacity bytes, but must return
 * zero on EOF or an input error. No complete compressed packet is retained.
 */
int divx3_decoder_decode_stream(
    Divx3Decoder *decoder, size_t packet_size,
    Divx3ReadFunction read, void *read_context, Divx3Frame *frame);

/* Inspect the seven-bit picture header available in the first packet byte
 * without changing decoder state.  This lets a player discard a late
 * predictive packet and resume decoding at the next independent I-picture. */
int divx3_packet_probe_intra(const uint8_t *prefix, size_t prefix_size,
                             int *intra);

size_t divx3_decoder_memory_bytes(const Divx3Decoder *decoder);
const char *divx3_strerror(int result);

#ifdef __cplusplus
}
#endif

#endif
