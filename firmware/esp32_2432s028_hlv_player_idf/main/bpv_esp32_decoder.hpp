#pragma once

#include <cstddef>
#include <cstdio>

#include "bpv1.h"

class BpvEsp32Decoder {
public:
    int begin(FILE *file, BPV1Header *header);
    void end();

    bool ready() const { return decoder_ != nullptr; }
    const BPV1Header &header() const { return header_; }
    size_t memoryBytes() const {
        return bpv1_decoder_memory_bytes(decoder_);
    }
    size_t packetCapacity() const {
        return bpv1_decoder_packet_capacity(decoder_);
    }

    int readPacket(FILE *file, BPV1Packet *packet) {
        return bpv1_decoder_read_packet(decoder_, file, packet);
    }
    int decode(const BPV1Packet *packet, const BPV1Frame **frame) {
        return bpv1_decoder_decode(decoder_, packet, frame);
    }

private:
    BPV1Header header_{};
    BPV1Decoder *decoder_ = nullptr;
};
