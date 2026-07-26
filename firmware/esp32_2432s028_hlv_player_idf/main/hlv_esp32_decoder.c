#include "hlv_esp32_decoder.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *const k_tag = "hlv-decoder";

int hlv_esp32_decoder_begin(hlv_esp32_decoder_t *decoder,
                            const HLV1Header *header,
                            bool compact_y6_u5_v5) {
    size_t padded_width;
    size_t padded_height;
    size_t full_frame_bytes;
    size_t packed_frame_bytes;
    size_t correction_frame_bytes;
    size_t compact_work_bytes;
    size_t selected_frame_bytes;
    size_t i;

    if (decoder == NULL || header == NULL) {
        return HLV1_ERR_ARGUMENT;
    }

    hlv_esp32_decoder_end(decoder);
    decoder->decoder = compact_y6_u5_v5
                           ? hlv1_decoder_create_y6_u5_v5(header)
                           : hlv1_decoder_create(header);
    if (decoder->decoder == NULL) {
        ESP_LOGE(k_tag, "Core decoder allocation failed");
        return HLV1_ERR_MEMORY;
    }
    decoder->compact_yuv = compact_y6_u5_v5;
    padded_width = (header->width + 15U) & ~15U;
    padded_height = (header->height + 15U) & ~15U;
    full_frame_bytes = padded_width * padded_height * 3U / 2U;
    packed_frame_bytes =
        padded_width * padded_height * 6U / 8U +
        2U * (padded_width / 2U) * (padded_height / 2U) * 5U / 8U;
    correction_frame_bytes =
        (padded_width / 8U) * (padded_height / 8U) +
        2U * (padded_width / 16U) * (padded_height / 16U);
    compact_work_bytes =
        padded_width * 16U + 2U * (padded_width / 2U) * 8U;
    selected_frame_bytes =
        decoder->compact_yuv
            ? 2U * (packed_frame_bytes + correction_frame_bytes) +
                  compact_work_bytes
            : 2U * full_frame_bytes;
    ESP_LOGI(k_tag,
             "Core ready (%s): heap=%u largest=%u, DMA=%u largest-DMA=%u",
             decoder->compact_yuv
                 ? "packed Y6/U5/V5 + Q4 corrections"
                 : "8-bit YUV 4:2:0",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    ESP_LOGI(k_tag, "Frame storage: %u bytes (8-bit baseline %u, saved %u)",
             (unsigned)selected_frame_bytes,
             (unsigned)(2U * full_frame_bytes),
             (unsigned)(2U * full_frame_bytes - selected_frame_bytes));

    for (i = 0; i < HLV_ESP32_PACKET_BLOCK_COUNT; ++i) {
        decoder->packet_blocks[i] =
            (uint8_t *)heap_caps_malloc(HLV_ESP32_PACKET_BLOCK_BYTES,
                                        MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (decoder->packet_blocks[i] != NULL) {
            ++decoder->dma_block_count;
        } else {
            decoder->packet_blocks[i] =
                (uint8_t *)heap_caps_malloc(HLV_ESP32_PACKET_BLOCK_BYTES,
                                            MALLOC_CAP_8BIT);
        }
        if (decoder->packet_blocks[i] == NULL) {
            ESP_LOGE(k_tag,
                     "Packet block %u/%u failed: heap=%u largest=%u",
                     (unsigned)(i + 1U),
                     (unsigned)HLV_ESP32_PACKET_BLOCK_COUNT,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(
                         MALLOC_CAP_8BIT));
            hlv_esp32_decoder_end(decoder);
            return HLV1_ERR_MEMORY;
        }
    }
    return HLV1_OK;
}

void hlv_esp32_decoder_end(hlv_esp32_decoder_t *decoder) {
    size_t i;

    if (decoder == NULL) {
        return;
    }
    for (i = 0; i < HLV_ESP32_PACKET_BLOCK_COUNT; ++i) {
        heap_caps_free(decoder->packet_blocks[i]);
        decoder->packet_blocks[i] = NULL;
    }
    if (decoder->decoder != NULL) {
        hlv1_decoder_destroy(decoder->decoder);
        decoder->decoder = NULL;
    }
    decoder->dma_block_count = 0;
    decoder->compact_yuv = false;
}

bool hlv_esp32_decoder_ready(const hlv_esp32_decoder_t *decoder) {
    return decoder != NULL && decoder->decoder != NULL;
}

size_t hlv_esp32_decoder_packet_capacity(
    const hlv_esp32_decoder_t *decoder) {
    (void)decoder;
    return HLV_ESP32_PACKET_BLOCK_COUNT * HLV_ESP32_PACKET_BLOCK_BYTES;
}

size_t hlv_esp32_decoder_dma_block_count(
    const hlv_esp32_decoder_t *decoder) {
    return decoder != NULL ? decoder->dma_block_count : 0U;
}

bool hlv_esp32_decoder_compact_yuv(const hlv_esp32_decoder_t *decoder) {
    return decoder != NULL && decoder->compact_yuv;
}

int hlv_esp32_decoder_read_packet(hlv_esp32_decoder_t *decoder,
                                  FILE *file,
                                  HLV1Packet *packet) {
    if (!hlv_esp32_decoder_ready(decoder)) {
        return HLV1_ERR_ARGUMENT;
    }
    return hlv1_packet_read_blocks(file, packet, decoder->packet_blocks,
                                   HLV_ESP32_PACKET_BLOCK_COUNT,
                                   HLV_ESP32_PACKET_BLOCK_BYTES);
}

int hlv_esp32_decoder_decode(hlv_esp32_decoder_t *decoder,
                             const HLV1Packet *packet,
                             const HLV1Frame **frame) {
    if (!hlv_esp32_decoder_ready(decoder)) {
        return HLV1_ERR_ARGUMENT;
    }
    return hlv1_decoder_decode_blocks(decoder->decoder, packet, frame);
}
