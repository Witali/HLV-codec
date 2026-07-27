#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "hlv1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One refill buffer streams arbitrarily large packets from SD. */
#define HLV_ESP32_STREAM_BUFFER_BYTES 7680U

typedef struct {
    HLV1Decoder *decoder;
    uint8_t *stream_buffer;
    bool dma_buffer;
    bool compact_yuv;
} hlv_esp32_decoder_t;

int hlv_esp32_decoder_begin(hlv_esp32_decoder_t *decoder,
                            const HLV1Header *header,
                            bool compact_y7_u6_v6);
void hlv_esp32_decoder_end(hlv_esp32_decoder_t *decoder);
bool hlv_esp32_decoder_ready(const hlv_esp32_decoder_t *decoder);
size_t hlv_esp32_decoder_stream_buffer_bytes(
    const hlv_esp32_decoder_t *decoder);
bool hlv_esp32_decoder_dma_buffer(
    const hlv_esp32_decoder_t *decoder);
bool hlv_esp32_decoder_compact_yuv(const hlv_esp32_decoder_t *decoder);
int hlv_esp32_decoder_decode_next(hlv_esp32_decoder_t *decoder,
                                  FILE *file,
                                  const HLV1Frame **frame,
                                  HLV1Packet *packet_info);

#ifdef __cplusplus
}
#endif
