#include "hlv_esp32_decoder.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {

constexpr char kTag[] = "hlv-decoder";

struct StreamReadContext {
    FILE *file;
    uint32_t read_us;
};

int streamRead(void *opaque, uint8_t *destination, size_t bytes) {
    auto *context = static_cast<StreamReadContext *>(opaque);
    if (!context || !context->file || (!destination && bytes))
        return HLV1_ERR_ARGUMENT;
    const int64_t start = esp_timer_get_time();
    const size_t received =
        fread(destination, 1, bytes, context->file);
    context->read_us +=
        static_cast<uint32_t>(esp_timer_get_time() - start);
    if (received == bytes) return HLV1_OK;
    return received == 0 && feof(context->file)
               ? HLV1_EOF
               : HLV1_ERR_IO;
}

}  // namespace

int HlvEsp32Decoder::begin(const HLV1Header &header,
                           bool compact_y6_u5_v5) {
    end();
    decoder_ = compact_y6_u5_v5
                   ? hlv1_decoder_create_y6_u5_v5(&header)
                   : hlv1_decoder_create(&header);
    if (!decoder_) {
        ESP_LOGE(kTag, "Core decoder allocation failed");
        return HLV1_ERR_MEMORY;
    }
    compact_yuv_ = compact_y6_u5_v5;
    const size_t padded_width = (header.width + 15U) & ~15U;
    const size_t padded_height = (header.height + 15U) & ~15U;
    const size_t full_frame_bytes = padded_width * padded_height * 3U / 2U;
    const size_t packed_frame_bytes =
        padded_width * padded_height * 6U / 8U +
        2U * (padded_width / 2U) * (padded_height / 2U) * 5U / 8U;
    const size_t correction_frame_bytes =
        (padded_width / 8U) * (padded_height / 8U) +
        2U * (padded_width / 16U) * (padded_height / 16U);
    const size_t compact_work_bytes =
        padded_width * 16U + 2U * (padded_width / 2U) * 8U;
    const size_t selected_frame_bytes = compact_yuv_
                                            ? 2U * (packed_frame_bytes +
                                                    correction_frame_bytes) +
                                                  compact_work_bytes
                                            : 2U * full_frame_bytes;
    ESP_LOGI(kTag,
             "Core ready (%s): heap=%u largest=%u, DMA=%u largest-DMA=%u",
             compact_yuv_
                 ? "packed Y6/U5/V5 + Q4 corrections"
                 : "8-bit YUV 4:2:0",
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
    ESP_LOGI(kTag, "Frame storage: %u bytes (8-bit baseline %u, saved %u)",
             static_cast<unsigned>(selected_frame_bytes),
             static_cast<unsigned>(2U * full_frame_bytes),
             static_cast<unsigned>(2U * full_frame_bytes -
                                   selected_frame_bytes));

    for (size_t i = 0; i < kPacketBlockCount; ++i) {
        packet_blocks_[i] = static_cast<uint8_t *>(heap_caps_malloc(
            kPacketBlockBytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (packet_blocks_[i]) {
            ++dma_block_count_;
        } else {
            packet_blocks_[i] = static_cast<uint8_t *>(heap_caps_malloc(
                kPacketBlockBytes, MALLOC_CAP_8BIT));
        }
        if (!packet_blocks_[i]) {
            ESP_LOGE(kTag,
                     "Packet block %u/%u failed: heap=%u largest=%u",
                     static_cast<unsigned>(i + 1),
                     static_cast<unsigned>(kPacketBlockCount),
                     static_cast<unsigned>(
                         heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(
                         MALLOC_CAP_8BIT)));
            end();
            return HLV1_ERR_MEMORY;
        }
    }
    ESP_LOGI(kTag, "Streaming packet window: %u x %u = %u bytes",
             static_cast<unsigned>(kPacketBlockCount),
             static_cast<unsigned>(kPacketBlockBytes),
             static_cast<unsigned>(packetCapacity()));
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
    compact_yuv_ = false;
}

int HlvEsp32Decoder::readPacket(FILE *file, HLV1Packet *packet) {
    if (!ready()) return HLV1_ERR_ARGUMENT;
    return hlv1_packet_read(file, packet);
}

int HlvEsp32Decoder::decode(const HLV1Packet *packet,
                            const HLV1Frame **frame) {
    if (!ready()) return HLV1_ERR_ARGUMENT;
    return packet && packet->payload_blocks
               ? hlv1_decoder_decode_blocks(decoder_, packet, frame)
               : hlv1_decoder_decode(decoder_, packet, frame);
}

int HlvEsp32Decoder::decodeNext(FILE *file, const HLV1Frame **frame,
                                uint32_t *read_us,
                                HLV1StageProfile *profile) {
    if (!ready() || !file || !frame || !read_us)
        return HLV1_ERR_ARGUMENT;
    hlv1_decoder_stage_profile_reset(decoder_);
    StreamReadContext context{file, 0};
    HLV1Packet packet{};
    const int result = hlv1_decoder_decode_stream(
        decoder_, streamRead, &context, packet_blocks_,
        kPacketBlockCount, kPacketBlockBytes, &packet, frame);
    *read_us = context.read_us;
    if (profile) {
        const HLV1StageProfile *measured =
            hlv1_decoder_stage_profile(decoder_);
        *profile = measured ? *measured : HLV1StageProfile{};
    }
    return result;
}
