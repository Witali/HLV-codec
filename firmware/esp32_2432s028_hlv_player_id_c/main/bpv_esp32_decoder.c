#include "bpv_esp32_decoder.h"

#include <string.h>

int bpv_esp32_decoder_begin(
    bpv_esp32_decoder_t *context, FILE *file, BPV1Header *header) {
    int result;
    if (!context || !file) return BPV1_ERR_ARGUMENT;
    bpv_esp32_decoder_end(context);
    result = bpv1_header_read(file, &context->header);
    if (result != BPV1_OK) return result;
    context->decoder = bpv1_decoder_create(&context->header);
    if (!context->decoder) {
        memset(&context->header, 0, sizeof(context->header));
        return BPV1_ERR_MEMORY;
    }
    if (header) *header = context->header;
    return BPV1_OK;
}

void bpv_esp32_decoder_end(bpv_esp32_decoder_t *context) {
    if (!context) return;
    bpv1_decoder_destroy(context->decoder);
    context->decoder = NULL;
    memset(&context->header, 0, sizeof(context->header));
}
