#pragma once

#include <stddef.h>
#include <stdio.h>

#include <hlv1.h>

/* ESP32-specific decoder front end.  The portable codec keeps using ordinary
 * contiguous packets; this class owns a reusable DMA-capable segmented packet
 * pool for the memory-constrained player. */
class HlvEsp32Decoder {
public:
    static constexpr size_t kPacketBlockCount = 8;
    static constexpr size_t kPacketBlockBytes = 8192;

    int begin(const HLV1Header &header);
    void end();

    bool ready() const { return decoder_ != nullptr; }
    size_t packetCapacity() const {
        return kPacketBlockCount * kPacketBlockBytes;
    }
    size_t dmaBlockCount() const { return dma_block_count_; }

    int readPacket(FILE *file, HLV1Packet *packet);
    int decode(const HLV1Packet *packet, const HLV1Frame **frame);

private:
    HLV1Decoder *decoder_ = nullptr;
    uint8_t *packet_blocks_[kPacketBlockCount]{};
    size_t dma_block_count_ = 0;
};
