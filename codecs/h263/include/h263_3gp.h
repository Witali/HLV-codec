#ifndef H263_3GP_H
#define H263_3GP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    H263_3GP_OK = 0,
    H263_3GP_EOF = 1,
    H263_3GP_ERR_ARGUMENT = -1,
    H263_3GP_ERR_IO = -2,
    H263_3GP_ERR_FORMAT = -3,
    H263_3GP_ERR_UNSUPPORTED = -4,
    H263_3GP_ERR_MEMORY = -5,
    H263_3GP_ERR_DECODE = -6,
};

typedef struct H2633gpInfo {
    uint16_t width;
    uint16_t height;
    uint32_t timescale;
    uint64_t duration_ticks;
    uint32_t frame_count;
    uint32_t max_sample_size;
    uint32_t fps_num;
    uint32_t fps_den;
    uint8_t profile;
    uint8_t level;
} H2633gpInfo;

typedef struct H2633gpFrame {
    const uint8_t *y;
    const uint8_t *u;
    const uint8_t *v;
    uint16_t width;
    uint16_t height;
    uint16_t y_stride;
    uint16_t chroma_stride;
    uint64_t timestamp_ticks;
    uint32_t duration_ticks;
    uint32_t index;
} H2633gpFrame;

typedef struct H2633gpDecoder H2633gpDecoder;

H2633gpDecoder *h263_3gp_decoder_create(void);
void h263_3gp_decoder_destroy(H2633gpDecoder *decoder);

/*
 * Opens the first H.263 video track. The initial embedded profile deliberately
 * accepts only the standard QCIF geometry (176x144) and baseline profile 0.
 * Other tracks are ignored here; the companion AMR-NB decoder opens `samr`
 * audio through an independent file cursor.
 */
int h263_3gp_decoder_open(H2633gpDecoder *decoder, FILE *file,
                          H2633gpInfo *info);
int h263_3gp_decoder_decode_next(H2633gpDecoder *decoder, FILE *file,
                                 H2633gpFrame *frame);

size_t h263_3gp_decoder_memory_bytes(const H2633gpDecoder *decoder);
const char *h263_3gp_strerror(int result);

#ifdef __cplusplus
}
#endif

#endif
