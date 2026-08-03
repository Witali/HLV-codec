#ifndef IMA_ADPCM_H
#define IMA_ADPCM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Self-contained mono IMA ADPCM block used by HLV and BPV audio packets.
 *
 * Bytes 0..1: initial signed 16-bit predictor, little endian
 * Byte 2:     initial IMA step-table index (0..88)
 * Byte 3:     reserved, zero
 * Bytes 4..5: decoded sample count, little endian
 * Bytes 6..:  standard IMA codes, low nibble first
 *
 * The initial predictor is the first decoded sample. Independent blocks make
 * seeking and video-frame dropping safe without carrying audio state across
 * packets. The sample-count field permits either an odd or even interval.
 */
#define IMA_ADPCM_BLOCK_HEADER_SIZE 6U
#define IMA_ADPCM_MAX_BLOCK_SAMPLES 4096U

typedef struct IMAADPCMState {
    int predictor;
    int step_index;
} IMAADPCMState;

size_t ima_adpcm_block_size(size_t sample_count);

int ima_adpcm_block_header_read(const uint8_t *header, size_t header_size,
                                IMAADPCMState *state,
                                uint16_t *sample_count);

int ima_adpcm_block_header_write(uint8_t *header, size_t header_size,
                                 const IMAADPCMState *state,
                                 uint16_t sample_count);

uint8_t ima_adpcm_encode_sample(IMAADPCMState *state, int16_t sample);
int16_t ima_adpcm_decode_nibble(IMAADPCMState *state, uint8_t code);

int ima_adpcm_encode_block(const int16_t *samples, size_t sample_count,
                           uint8_t *encoded, size_t encoded_capacity,
                           size_t *encoded_size);

int ima_adpcm_decode_block(const uint8_t *encoded, size_t encoded_size,
                           int16_t *samples, size_t sample_capacity,
                           size_t *sample_count);

#ifdef __cplusplus
}
#endif

#endif
