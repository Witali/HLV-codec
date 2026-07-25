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

typedef struct Divx3Frame {
    const uint8_t *y;
    const uint8_t *cb;
    const uint8_t *cr;
    uint16_t width;
    uint16_t height;
    uint16_t y_stride;
    uint16_t c_stride;
    uint32_t frame_number;
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

size_t divx3_decoder_memory_bytes(const Divx3Decoder *decoder);
const char *divx3_strerror(int result);

#ifdef __cplusplus
}
#endif

#endif
