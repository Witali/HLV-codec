#include "hlv_esp32_decoder.hpp"

#include <algorithm>

#include "esp_heap_caps.h"
#include "esp_log.h"

namespace {

constexpr char kTag[] = "hlv-decoder";

}  // namespace

int HlvEsp32Decoder::begin(const HLV1Header &header,
                           bool compact_y7_u6_v6) {
    end();
    const size_t padded_width = (header.width + 15U) & ~15U;
    const size_t padded_height = (header.height + 15U) & ~15U;
    single_reference_ =
        compact_y7_u6_v6 &&
        header.search_radius <= HLV1_SINGLE_REFERENCE_MAX_RADIUS &&
        padded_width * padded_height > 320U * 192U;
    decoder_ =
        single_reference_
            ? hlv1_decoder_create_y7_u6_v6_single_reference(&header)
            : (compact_y7_u6_v6
                   ? hlv1_decoder_create_y7_u6_v6(&header)
                   : hlv1_decoder_create(&header));
    if (!decoder_ && compact_y7_u6_v6 && !single_reference_ &&
        header.search_radius <= HLV1_SINGLE_REFERENCE_MAX_RADIUS) {
        ESP_LOGW(kTag,
                 "Dual reference did not fit; retrying single reference");
        single_reference_ = true;
        decoder_ =
            hlv1_decoder_create_y7_u6_v6_single_reference(&header);
    }
    if (!decoder_) {
        ESP_LOGE(kTag, "Core decoder allocation failed");
        return HLV1_ERR_MEMORY;
    }
    compact_yuv_ = compact_y7_u6_v6;
    const size_t full_frame_bytes = padded_width * padded_height * 3U / 2U;
    const size_t packed_frame_bytes =
        padded_width * padded_height * 7U / 8U +
        2U * (padded_width / 2U) * (padded_height / 2U) * 6U / 8U;
    const size_t correction_frame_bytes =
        (padded_width / 8U) * (padded_height / 8U) +
        2U * (padded_width / 16U) * (padded_height / 16U);
    const size_t compact_work_bytes =
        padded_width * 16U + 2U * (padded_width / 2U) * 8U;
    const size_t ring_luma_rows =
        std::min(padded_height,
                 static_cast<size_t>(HLV1_SINGLE_REFERENCE_LUMA_ROWS));
    const size_t compact_ring_bytes =
        padded_width * ring_luma_rows * 7U / 8U +
        2U * (padded_width / 2U) * (ring_luma_rows / 2U) * 6U / 8U +
        (padded_width / 8U) * (ring_luma_rows / 8U) +
        2U * (padded_width / 16U) * (ring_luma_rows / 16U);
    const size_t selected_frame_bytes =
        single_reference_
            ? packed_frame_bytes + correction_frame_bytes +
                  compact_ring_bytes + compact_work_bytes
            : (compact_yuv_
                   ? 2U * (packed_frame_bytes + correction_frame_bytes) +
                         compact_work_bytes
                   : 2U * full_frame_bytes);
    ESP_LOGI(kTag,
             "Core ready (%s): heap=%u largest=%u, DMA=%u largest-DMA=%u",
             compact_yuv_
                 ? "packed Y7/U6/V6 + per-plane Q4 corrections"
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
    if (compact_yuv_) {
        ESP_LOGI(kTag, "Reference strategy: %s",
                 single_reference_ ? "single + rolling rows"
                                   : "dual + pointer swap");
    }

    stream_buffer_ = static_cast<uint8_t *>(heap_caps_malloc(
        kStreamBufferBytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    dma_buffer_ = stream_buffer_ != nullptr;
    if (!stream_buffer_) {
        stream_buffer_ = static_cast<uint8_t *>(heap_caps_malloc(
            kStreamBufferBytes, MALLOC_CAP_8BIT));
    }
    if (!stream_buffer_) {
        ESP_LOGE(kTag, "Stream buffer failed: heap=%u largest=%u",
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(
                     MALLOC_CAP_8BIT)));
        end();
        return HLV1_ERR_MEMORY;
    }
    return HLV1_OK;
}

void HlvEsp32Decoder::end() {
    heap_caps_free(stream_buffer_);
    stream_buffer_ = nullptr;
    if (decoder_) {
        hlv1_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
    dma_buffer_ = false;
    compact_yuv_ = false;
    single_reference_ = false;
}

void HlvEsp32Decoder::setReferenceRowGuard(
    HLV1ReferenceRowGuard guard, void *opaque) {
    if (decoder_)
        hlv1_decoder_set_reference_row_guard(decoder_, guard, opaque);
}

int HlvEsp32Decoder::decodeNext(FILE *file, const HLV1Frame **frame,
                                HLV1Packet *packet_info,
                                HLV1StageProfile *profile) {
    if (!ready()) return HLV1_ERR_ARGUMENT;
#if HLV1_ENABLE_STAGE_PROFILE
    hlv1_decoder_stage_profile_reset(decoder_);
    const int result = hlv1_decoder_decode_file(
        decoder_, file, stream_buffer_, kStreamBufferBytes,
        packet_info, frame);
    if (profile) {
        const HLV1StageProfile *measured =
            hlv1_decoder_stage_profile(decoder_);
        *profile = measured ? *measured : HLV1StageProfile{};
    }
    return result;
#else
    (void)profile;
    return hlv1_decoder_decode_file(
        decoder_, file, stream_buffer_, kStreamBufferBytes,
        packet_info, frame);
#endif
}

int HlvEsp32Decoder::decodeNextCatchup(
    FILE *file, const HLV1Frame **frame, HLV1Packet *packet_info,
    HLV1StageProfile *profile, bool skip_predictive, bool *skipped) {
    if (!skipped) return HLV1_ERR_ARGUMENT;
    *skipped = false;
    if (!skip_predictive)
        return decodeNext(file, frame, packet_info, profile);
    if (!ready() || !file || !frame) return HLV1_ERR_ARGUMENT;

    const long packet_offset = ftell(file);
    if (packet_offset < 0) return HLV1_ERR_IO;
    uint8_t header[HLV1_FRAME_HEADER_SIZE];
    const size_t header_size = fread(header, 1, sizeof header, file);
    if (!header_size && feof(file)) return HLV1_EOF;
    if (header_size != sizeof header) return HLV1_ERR_IO;
    HLV1Packet packet{};
    uint32_t expected_crc = 0;
    const int result =
        hlv1_packet_header_parse(header, &packet, &expected_crc);
    if (result != HLV1_OK) return result;
    (void)expected_crc;
    if (packet_info) *packet_info = packet;
    if (packet.frame_type != HLV1_FRAME_KEY) {
        if (fseek(file, static_cast<long>(packet.payload_size), SEEK_CUR) != 0)
            return HLV1_ERR_IO;
        *frame = nullptr;
        *skipped = true;
        if (profile) *profile = HLV1StageProfile{};
        return HLV1_OK;
    }
    if (fseek(file, packet_offset, SEEK_SET) != 0) return HLV1_ERR_IO;
    return decodeNext(file, frame, packet_info, profile);
}
