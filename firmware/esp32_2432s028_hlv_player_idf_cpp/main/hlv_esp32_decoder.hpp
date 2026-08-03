#pragma once

#include <stddef.h>
#include <stdio.h>

#include "hlv1.h"

class HlvEsp32Decoder {
public:
    // One refill buffer streams arbitrarily large packets from SD while the
    // decoder consumes their video bits.
    static constexpr size_t kStreamBufferBytes = 7680;

    int begin(const HLV1Header &header, bool compact_y7_u6_v6);
    void end();

    bool ready() const { return decoder_ != nullptr; }
    size_t streamBufferBytes() const { return kStreamBufferBytes; }
    bool dmaBuffer() const { return dma_buffer_; }
    bool compactYuv() const { return compact_yuv_; }
    bool singleReference() const { return single_reference_; }
    void setReferenceRowGuard(HLV1ReferenceRowGuard guard, void *opaque);

    int decodeNext(FILE *file, const HLV1Frame **frame,
                   HLV1Packet *packet_info = nullptr,
                   HLV1StageProfile *profile = nullptr);
    int decodeNextCatchup(FILE *file, const HLV1Frame **frame,
                          HLV1Packet *packet_info,
                          HLV1StageProfile *profile,
                          bool skip_predictive, bool *skipped);

private:
    HLV1Decoder *decoder_ = nullptr;
    uint8_t *stream_buffer_ = nullptr;
    bool dma_buffer_ = false;
    bool compact_yuv_ = false;
    bool single_reference_ = false;
};
