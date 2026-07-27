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
    void setProfileClock(BPV1ProfileClockMicros clock, void *opaque) {
        bpv1_decoder_set_profile_clock(decoder_, clock, opaque);
    }
    BPV1DecodeProfile lastProfile() const {
        BPV1DecodeProfile profile{};
        bpv1_decoder_last_profile(decoder_, &profile);
        return profile;
    }

    int readPacket(FILE *file, BPV1Packet *packet) {
        return bpv1_decoder_read_packet(decoder_, file, packet);
    }
    int decode(const BPV1Packet *packet, const BPV1Frame **frame) {
        return bpv1_decoder_decode(decoder_, packet, frame);
    }
    int decodeDirect(
        const BPV1Packet *packet, uint16_t rows_per_strip,
        BPV1Rgb565StripAcquire acquire, BPV1Rgb565StripSubmit submit,
        BPV1Rgb565StripFlush flush, void *opaque,
        const BPV1Frame **frame
    ) {
        return bpv1_decoder_decode_rgb565_strips(
            decoder_, packet, rows_per_strip, acquire, submit, flush,
            opaque, frame);
    }
    int decodeNextDirect(
        FILE *file, uint16_t rows_per_strip,
        BPV1Rgb565StripAcquire acquire, BPV1Rgb565StripSubmit submit,
        BPV1Rgb565StripFlush flush, void *opaque,
        const BPV1Frame **frame
    ) {
        return bpv1_decoder_decode_next_rgb565_strips(
            decoder_, file, rows_per_strip, acquire, submit, flush,
            opaque, frame);
    }

private:
    BPV1Header header_{};
    BPV1Decoder *decoder_ = nullptr;
};
