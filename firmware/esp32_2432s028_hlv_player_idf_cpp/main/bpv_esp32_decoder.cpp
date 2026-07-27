#include "bpv_esp32_decoder.hpp"

int BpvEsp32Decoder::begin(FILE *file, BPV1Header *header) {
    end();
    if (!file) return BPV1_ERR_ARGUMENT;
    int result = bpv1_header_read(file, &header_);
    if (result != BPV1_OK) return result;
    decoder_ = bpv1_decoder_create(&header_);
    if (!decoder_) {
        header_ = {};
        return BPV1_ERR_MEMORY;
    }
    if (header) *header = header_;
    return BPV1_OK;
}

void BpvEsp32Decoder::end() {
    bpv1_decoder_destroy(decoder_);
    decoder_ = nullptr;
    header_ = {};
}
