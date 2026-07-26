#ifndef HLV_BPV_ESP32_DECODER_H
#define HLV_BPV_ESP32_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "bpv1.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    BPV1Header header;
    BPV1Decoder *decoder;
} bpv_esp32_decoder_t;

int bpv_esp32_decoder_begin(
    bpv_esp32_decoder_t *context, FILE *file, BPV1Header *header);
void bpv_esp32_decoder_end(bpv_esp32_decoder_t *context);

static inline bool bpv_esp32_decoder_ready(
    const bpv_esp32_decoder_t *context) {
    return context && context->decoder != NULL;
}

static inline const BPV1Header *bpv_esp32_decoder_header(
    const bpv_esp32_decoder_t *context) {
    return context ? &context->header : NULL;
}

static inline size_t bpv_esp32_decoder_memory_bytes(
    const bpv_esp32_decoder_t *context) {
    return context
               ? bpv1_decoder_memory_bytes(context->decoder)
               : 0;
}

static inline size_t bpv_esp32_decoder_packet_capacity(
    const bpv_esp32_decoder_t *context) {
    return context
               ? bpv1_decoder_packet_capacity(context->decoder)
               : 0;
}

static inline int bpv_esp32_decoder_read_packet(
    bpv_esp32_decoder_t *context, FILE *file, BPV1Packet *packet) {
    return bpv1_decoder_read_packet(context->decoder, file, packet);
}

static inline int bpv_esp32_decoder_decode(
    bpv_esp32_decoder_t *context, const BPV1Packet *packet,
    const BPV1Frame **frame) {
    return bpv1_decoder_decode(context->decoder, packet, frame);
}

#ifdef __cplusplus
}
#endif

#endif
