#include "HlvEsp32Decoder.hpp"

#include <Arduino.h>
#include <esp_heap_caps.h>

int HlvEsp32Decoder::begin(const HLV1Header &header) {
    end();
    decoder_ = hlv1_decoder_create(&header);
    if (!decoder_) {
        Serial.println("ESP32 core decoder allocation failed");
        return HLV1_ERR_MEMORY;
    }
    Serial.printf("Core decoder allocated: heap=%u, largest=%u, "
                  "DMA free=%u, DMA largest=%u\n",
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(
                      MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_free_size(
                      MALLOC_CAP_DMA)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(
                      MALLOC_CAP_DMA)));

    for (size_t i = 0; i < kPacketBlockCount; ++i) {
        packet_blocks_[i] = static_cast<uint8_t *>(heap_caps_malloc(
            kPacketBlockBytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (packet_blocks_[i]) ++dma_block_count_;
        else
            packet_blocks_[i] = static_cast<uint8_t *>(heap_caps_malloc(
                kPacketBlockBytes, MALLOC_CAP_8BIT));
        if (!packet_blocks_[i]) {
            Serial.printf("Packet pool allocation failed at block %u/%u: "
                          "heap=%u, largest=%u\n",
                          static_cast<unsigned>(i + 1),
                          static_cast<unsigned>(kPacketBlockCount),
                          static_cast<unsigned>(heap_caps_get_free_size(
                              MALLOC_CAP_8BIT)),
                          static_cast<unsigned>(
                              heap_caps_get_largest_free_block(
                                  MALLOC_CAP_8BIT)));
            end();
            return HLV1_ERR_MEMORY;
        }
    }
    return HLV1_OK;
}

void HlvEsp32Decoder::end() {
    for (size_t i = 0; i < kPacketBlockCount; ++i) {
        heap_caps_free(packet_blocks_[i]);
        packet_blocks_[i] = nullptr;
    }
    if (decoder_) {
        hlv1_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
    dma_block_count_ = 0;
}

int HlvEsp32Decoder::readPacket(FILE *file, HLV1Packet *packet) {
    if (!ready()) return HLV1_ERR_ARGUMENT;
    return hlv1_packet_read_blocks(file, packet, packet_blocks_,
                                   kPacketBlockCount, kPacketBlockBytes);
}

int HlvEsp32Decoder::decode(const HLV1Packet *packet,
                            const HLV1Frame **frame) {
    if (!ready()) return HLV1_ERR_ARGUMENT;
    return hlv1_decoder_decode_blocks(decoder_, packet, frame);
}
