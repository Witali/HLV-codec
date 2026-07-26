#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "hlv1.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Nine blocks fit a worst-case 320x180 v13 LITERAL key frame plus one
 * frame interval of mono audio. The previous eight-block pool topped out
 * at 61,440 bytes, below the 65,280 bytes of literal data
 * (65,520 bytes including one mode byte per padded macroblock).
 */
#define HLV_ESP32_PACKET_BLOCK_COUNT 9U
/* Each allocation remains below the fragmented heap's largest block. */
#define HLV_ESP32_PACKET_BLOCK_BYTES 7680U

typedef struct {
    HLV1Decoder *decoder;
    uint8_t *packet_blocks[HLV_ESP32_PACKET_BLOCK_COUNT];
    size_t dma_block_count;
    bool compact_yuv;
} hlv_esp32_decoder_t;

int hlv_esp32_decoder_begin(hlv_esp32_decoder_t *decoder,
                            const HLV1Header *header,
                            bool compact_y6_u5_v5);
void hlv_esp32_decoder_end(hlv_esp32_decoder_t *decoder);
bool hlv_esp32_decoder_ready(const hlv_esp32_decoder_t *decoder);
size_t hlv_esp32_decoder_packet_capacity(
    const hlv_esp32_decoder_t *decoder);
size_t hlv_esp32_decoder_dma_block_count(
    const hlv_esp32_decoder_t *decoder);
bool hlv_esp32_decoder_compact_yuv(const hlv_esp32_decoder_t *decoder);
int hlv_esp32_decoder_read_packet(hlv_esp32_decoder_t *decoder,
                                  FILE *file,
                                  HLV1Packet *packet);
int hlv_esp32_decoder_decode(hlv_esp32_decoder_t *decoder,
                             const HLV1Packet *packet,
                             const HLV1Frame **frame);

#ifdef __cplusplus
}
#endif
