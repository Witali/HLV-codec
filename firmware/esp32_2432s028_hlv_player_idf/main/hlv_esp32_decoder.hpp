#pragma once

#include <stddef.h>
#include <stdio.h>

#include "hlv1.h"

class HlvEsp32Decoder {
public:
    // Nine blocks fit a worst-case 320x180 v13 LITERAL key frame plus one
    // frame interval of mono audio. The previous eight-block pool topped out
    // at 61,440 bytes, below the 65,280 bytes of literal data
    // (65,520 bytes including one mode byte per padded macroblock).
    static constexpr size_t kPacketBlockCount = 9;
    // Each allocation remains below the fragmented heap's largest block.
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
