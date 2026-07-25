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
    H263_3GP_ERR_FRAME_MEMORY = -7,
    H263_3GP_ERR_DECODER_MEMORY = -8,
    H263_3GP_ERR_PACKET_MEMORY = -9,
};

enum {
    H263_CONTAINER_UNKNOWN = 0,
    H263_CONTAINER_3GP = 1,
    H263_CONTAINER_AVI = 2,
};

#define H263_AVI_PCM_MAX_SAMPLES 256

typedef struct H2633gpInfo {
    uint16_t width;
    uint16_t height;
    uint32_t timescale;
    uint64_t duration_ticks;
    uint32_t frame_count;
    uint32_t max_sample_size;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t audio_sample_rate;
    uint8_t profile;
    uint8_t level;
    uint8_t container;
    uint8_t audio_channels;
    uint8_t audio_bits_per_sample;
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
typedef struct H263AviPcmReader H263AviPcmReader;

typedef struct H263AviPcmFrame {
    uint8_t samples[H263_AVI_PCM_MAX_SAMPLES];
    uint16_t sample_count;
} H263AviPcmFrame;

H2633gpDecoder *h263_3gp_decoder_create(void);
void h263_3gp_decoder_destroy(H2633gpDecoder *decoder);

/*
 * Opens the first H.263 video track in either 3GP or AVI. The embedded
 * profiles accept 176x144 QCIF plus intra-only H.263+ custom sizes 256x144,
 * 256x192, 320x180, and 320x240. 3GP audio is handled by the companion
 * AMR-NB decoder; AVI accepts mono PCM at 8 kHz through the reader below.
 */
int h263_3gp_decoder_open(H2633gpDecoder *decoder, FILE *file,
                          H2633gpInfo *info);
int h263_3gp_decoder_decode_next(H2633gpDecoder *decoder, FILE *file,
                                 H2633gpFrame *frame);

size_t h263_3gp_decoder_memory_bytes(const H2633gpDecoder *decoder);
const char *h263_3gp_strerror(int result);

/* Probe the bounded H.263/AVI profile without allocating video buffers. */
int h263_avi_probe(FILE *file, H2633gpInfo *info);

/*
 * Stream mono AVI PCM through an independent FILE cursor. Signed 16-bit
 * little-endian samples are converted to unsigned 8-bit samples for the
 * project players; unsigned 8-bit PCM is passed through.
 */
H263AviPcmReader *h263_avi_pcm_reader_create(void);
void h263_avi_pcm_reader_destroy(H263AviPcmReader *reader);
int h263_avi_pcm_reader_open(H263AviPcmReader *reader, FILE *file,
                             H2633gpInfo *info);
int h263_avi_pcm_reader_decode_next(H263AviPcmReader *reader, FILE *file,
                                    H263AviPcmFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
