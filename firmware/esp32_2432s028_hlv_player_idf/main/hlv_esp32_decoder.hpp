#pragma once

#include <stddef.h>
#include <stdio.h>

#include "hlv1.h"

class HlvEsp32Decoder {
public:
    static constexpr size_t kPacketBlockCount = 8;
    // 8 x 7680 = 61440 bytes, enough for the measured 60538-byte maximum
    // packet while retaining the fragmented-heap-safe eight-block layout.
    static constexpr size_t kPacketBlockBytes = 7680;

    int begin(const HLV1Header &header, bool compact_y6_u5_v5);
    void end();

    bool ready() const { return decoder_ != nullptr; }
    size_t packetCapacity() const {
        return kPacketBlockCount * kPacketBlockBytes;
    }
    size_t dmaBlockCount() const { return dma_block_count_; }
    bool compactYuv() const { return compact_yuv_; }

    int readPacket(FILE *file, HLV1Packet *packet);
    int decode(const HLV1Packet *packet, const HLV1Frame **frame);

private:
    HLV1Decoder *decoder_ = nullptr;
    uint8_t *packet_blocks_[kPacketBlockCount]{};
    size_t dma_block_count_ = 0;
    bool compact_yuv_ = false;
};
