#ifndef AMRNB_3GP_H
#define AMRNB_3GP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    AMRNB_3GP_OK = 0,
    AMRNB_3GP_EOF = 1,
    AMRNB_3GP_ERR_ARGUMENT = -1,
    AMRNB_3GP_ERR_IO = -2,
    AMRNB_3GP_ERR_FORMAT = -3,
    AMRNB_3GP_ERR_UNSUPPORTED = -4,
    AMRNB_3GP_ERR_MEMORY = -5,
    AMRNB_3GP_ERR_DECODE = -6,
};

enum {
    AMRNB_SAMPLE_RATE = 8000,
    AMRNB_SAMPLES_PER_FRAME = 160,
};

typedef struct AmrNb3gpInfo {
    uint32_t timescale;
    uint64_t duration_ticks;
    uint32_t frame_count;
    uint32_t max_sample_size;
    uint16_t sample_rate;
    uint8_t channels;
    uint16_t mode_set;
    uint8_t frames_per_sample;
} AmrNb3gpInfo;

typedef struct AmrNb3gpFrame {
    const int16_t *samples;
    uint16_t sample_count;
    uint8_t frame_type;
    uint64_t timestamp_ticks;
    uint32_t duration_ticks;
    uint32_t index;
} AmrNb3gpFrame;

typedef struct AmrNb3gpDecoder AmrNb3gpDecoder;

AmrNb3gpDecoder *amrnb_3gp_decoder_create(void);
void amrnb_3gp_decoder_destroy(AmrNb3gpDecoder *decoder);

/*
 * Opens the first AMR-NB audio track. The embedded profile accepts a `samr`
 * mono track at 8 kHz with one 20 ms AMR frame in each container sample.
 */
int amrnb_3gp_decoder_open(AmrNb3gpDecoder *decoder, FILE *file,
                           AmrNb3gpInfo *info);
int amrnb_3gp_decoder_decode_next(AmrNb3gpDecoder *decoder, FILE *file,
                                  AmrNb3gpFrame *frame);

size_t amrnb_3gp_decoder_memory_bytes(const AmrNb3gpDecoder *decoder);
const char *amrnb_3gp_strerror(int result);

#ifdef __cplusplus
}
#endif

#endif
