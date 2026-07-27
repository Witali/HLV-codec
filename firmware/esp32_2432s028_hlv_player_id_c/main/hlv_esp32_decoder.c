#include "hlv_esp32_decoder.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *const k_tag = "hlv-decoder";

int hlv_esp32_decoder_begin(hlv_esp32_decoder_t *decoder,
                            const HLV1Header *header,
                            bool compact_y7_u6_v6) {
    size_t padded_width;
    size_t padded_height;
    size_t full_frame_bytes;
    size_t packed_frame_bytes;
    size_t correction_frame_bytes;
    size_t compact_work_bytes;
    size_t compact_ring_bytes;
    size_t selected_frame_bytes;
    size_t ring_luma_rows;
    if (decoder == NULL || header == NULL) {
        return HLV1_ERR_ARGUMENT;
    }

    hlv_esp32_decoder_end(decoder);
    decoder->single_reference =
        compact_y7_u6_v6 &&
        header->search_radius <= HLV1_SINGLE_REFERENCE_MAX_RADIUS;
    decoder->decoder =
        decoder->single_reference
            ? hlv1_decoder_create_y7_u6_v6_single_reference(header)
            : (compact_y7_u6_v6
                   ? hlv1_decoder_create_y7_u6_v6(header)
                   : hlv1_decoder_create(header));
    if (decoder->decoder == NULL) {
        ESP_LOGE(k_tag, "Core decoder allocation failed");
        return HLV1_ERR_MEMORY;
    }
    decoder->compact_yuv = compact_y7_u6_v6;
    padded_width = (header->width + 15U) & ~15U;
    padded_height = (header->height + 15U) & ~15U;
    full_frame_bytes = padded_width * padded_height * 3U / 2U;
    packed_frame_bytes =
        padded_width * padded_height * 7U / 8U +
        2U * (padded_width / 2U) * (padded_height / 2U) * 6U / 8U;
    correction_frame_bytes =
        (padded_width / 8U) * (padded_height / 8U) +
        2U * (padded_width / 16U) * (padded_height / 16U);
    compact_work_bytes =
        padded_width * 16U + 2U * (padded_width / 2U) * 8U;
    ring_luma_rows =
        padded_height < HLV1_SINGLE_REFERENCE_LUMA_ROWS
            ? padded_height
            : HLV1_SINGLE_REFERENCE_LUMA_ROWS;
    compact_ring_bytes =
        padded_width * ring_luma_rows * 7U / 8U +
        2U * (padded_width / 2U) * (ring_luma_rows / 2U) * 6U / 8U +
        (padded_width / 8U) * (ring_luma_rows / 8U) +
        2U * (padded_width / 16U) * (ring_luma_rows / 16U);
    selected_frame_bytes =
        decoder->single_reference
            ? packed_frame_bytes + correction_frame_bytes +
                  compact_ring_bytes + compact_work_bytes
            : (decoder->compact_yuv
                   ? 2U * (packed_frame_bytes + correction_frame_bytes) +
                         compact_work_bytes
                   : 2U * full_frame_bytes);
    ESP_LOGI(k_tag,
             "Core ready (%s): heap=%u largest=%u, DMA=%u largest-DMA=%u",
             decoder->compact_yuv
                 ? "packed Y7/U6/V6 + per-plane Q4 corrections"
                 : "8-bit YUV 4:2:0",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    ESP_LOGI(k_tag, "Frame storage: %u bytes (8-bit baseline %u, saved %u)",
             (unsigned)selected_frame_bytes,
             (unsigned)(2U * full_frame_bytes),
             (unsigned)(2U * full_frame_bytes - selected_frame_bytes));

    decoder->stream_buffer = (uint8_t *)heap_caps_malloc(
        HLV_ESP32_STREAM_BUFFER_BYTES,
        MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    decoder->dma_buffer = decoder->stream_buffer != NULL;
    if (decoder->stream_buffer == NULL) {
        decoder->stream_buffer = (uint8_t *)heap_caps_malloc(
            HLV_ESP32_STREAM_BUFFER_BYTES, MALLOC_CAP_8BIT);
    }
    if (decoder->stream_buffer == NULL) {
        ESP_LOGE(k_tag, "Stream buffer failed: heap=%u largest=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(
                     MALLOC_CAP_8BIT));
        hlv_esp32_decoder_end(decoder);
        return HLV1_ERR_MEMORY;
    }
    return HLV1_OK;
}

void hlv_esp32_decoder_end(hlv_esp32_decoder_t *decoder) {
    if (decoder == NULL) {
        return;
    }
    heap_caps_free(decoder->stream_buffer);
    decoder->stream_buffer = NULL;
    if (decoder->decoder != NULL) {
        hlv1_decoder_destroy(decoder->decoder);
        decoder->decoder = NULL;
    }
    decoder->dma_buffer = false;
    decoder->compact_yuv = false;
    decoder->single_reference = false;
}

bool hlv_esp32_decoder_ready(const hlv_esp32_decoder_t *decoder) {
    return decoder != NULL && decoder->decoder != NULL;
}

size_t hlv_esp32_decoder_stream_buffer_bytes(
    const hlv_esp32_decoder_t *decoder) {
    (void)decoder;
    return HLV_ESP32_STREAM_BUFFER_BYTES;
}

bool hlv_esp32_decoder_dma_buffer(
    const hlv_esp32_decoder_t *decoder) {
    return decoder != NULL && decoder->dma_buffer;
}

bool hlv_esp32_decoder_compact_yuv(const hlv_esp32_decoder_t *decoder) {
    return decoder != NULL && decoder->compact_yuv;
}

bool hlv_esp32_decoder_single_reference(
    const hlv_esp32_decoder_t *decoder) {
    return decoder != NULL && decoder->single_reference;
}

void hlv_esp32_decoder_set_reference_row_guard(
    hlv_esp32_decoder_t *decoder,
    HLV1ReferenceRowGuard guard, void *opaque) {
    if (decoder == NULL || decoder->decoder == NULL) return;
    hlv1_decoder_set_reference_row_guard(
        decoder->decoder, guard, opaque);
}

int hlv_esp32_decoder_decode_next(hlv_esp32_decoder_t *decoder,
                                  FILE *file,
                                  const HLV1Frame **frame,
                                  HLV1Packet *packet_info,
                                  HLV1StageProfile *profile) {
    if (!hlv_esp32_decoder_ready(decoder)) {
        return HLV1_ERR_ARGUMENT;
    }
#if HLV1_ENABLE_STAGE_PROFILE
    hlv1_decoder_stage_profile_reset(decoder->decoder);
    const int result = hlv1_decoder_decode_file(
        decoder->decoder, file, decoder->stream_buffer,
        HLV_ESP32_STREAM_BUFFER_BYTES, packet_info, frame);
    if (profile) {
        const HLV1StageProfile *measured =
            hlv1_decoder_stage_profile(decoder->decoder);
        *profile = measured ? *measured : (HLV1StageProfile){0};
    }
    return result;
#else
    (void)profile;
    return hlv1_decoder_decode_file(
        decoder->decoder, file, decoder->stream_buffer,
        HLV_ESP32_STREAM_BUFFER_BYTES, packet_info, frame);
#endif
}
