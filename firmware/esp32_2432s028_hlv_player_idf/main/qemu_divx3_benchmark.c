#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "compact_yuv420.h"
#include "divx3.h"
#include "divx3_avi.h"

#define FRAME_LIMIT 120U
#define TARGET_CPU_HZ 240000000U

static const char *const k_tag = "divx3-qemu-bench";

extern const uint8_t k_video_start[]
    asm("_binary_qemu_divx3_benchmark_avi_start");
extern const uint8_t k_video_end[]
    asm("_binary_qemu_divx3_benchmark_avi_end");

static uint64_t hash_plane(uint64_t hash,
                           const uint8_t *plane,
                           unsigned stride,
                           unsigned width,
                           unsigned height,
                           const int8_t *correction,
                           unsigned correction_stride,
                           unsigned bits,
                           uint8_t *scratch) {
    unsigned y;
    for (y = 0; y < height; ++y) {
        const uint8_t *row = plane + (size_t)y * stride;
        unsigned x;
        compact_yuv420_unpack_corrected_samples(
            row, 0, (int)y, bits, correction,
            (int)correction_stride, scratch, (int)width);
        for (x = 0; x < width; ++x) {
            hash ^= scratch[x];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

static uint64_t hash_frame(uint64_t hash,
                           const Divx3Frame *frame,
                           uint8_t *scratch) {
    unsigned chroma_width = (frame->width + 1U) / 2U;
    unsigned chroma_height = (frame->height + 1U) / 2U;
    hash = hash_plane(
        hash, frame->y, frame->y_stride, frame->width, frame->height,
        frame->correction_y, frame->correction_stride_y,
        COMPACT_YUV420_LUMA_BITS, scratch);
    hash = hash_plane(
        hash, frame->cb, frame->c_stride, chroma_width, chroma_height,
        frame->correction_cb, frame->correction_stride_c,
        COMPACT_YUV420_CHROMA_BITS, scratch);
    return hash_plane(
        hash, frame->cr, frame->c_stride, chroma_width, chroma_height,
        frame->correction_cr, frame->correction_stride_c,
        COMPACT_YUV420_CHROMA_BITS, scratch);
}

static __attribute__((noreturn)) void finish(int code) {
    fflush(stdout);
    esp_rom_printf("DIVX3_BENCH_DONE,%d\n", code);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

void app_main(void) {
    size_t video_size = (size_t)(k_video_end - k_video_start);
    FILE *file;
    Divx3AviInfo info = {0};
    Divx3Decoder *decoder;
    uint8_t *packet;
    uint8_t *scratch;
    int result;
    uint64_t decode_cycles = 0;
    uint64_t intra_cycles = 0;
    uint64_t p_cycles = 0;
    uint64_t frame_hash = UINT64_C(1469598103934665603);
    uint32_t frame_cycles[FRAME_LIMIT] = {0};
    uint32_t intra_frames = 0;
    uint32_t p_frames = 0;
    uint32_t intra_max = 0;
    uint32_t p_max = 0;
    uint32_t frames = 0;

    ESP_LOGI(k_tag,
             "Xtensa compact decoder benchmark, AVI=%u bytes",
             (unsigned)video_size);
    file = fmemopen((void *)k_video_start, video_size, "rb");
    if (file == NULL) {
        finish(1);
    }
    result = divx3_avi_read_info(file, &info);
    if (result != DIVX3_AVI_OK) {
        ESP_LOGE(k_tag, "AVI probe failed: %s",
                 divx3_avi_strerror(result));
        fclose(file);
        finish(2);
    }
    decoder = divx3_decoder_create_y6_u5_v5(info.width, info.height);
    packet = (uint8_t *)malloc(info.max_video_packet_size);
    scratch = (uint8_t *)malloc(info.width);
    if (decoder == NULL || packet == NULL || scratch == NULL) {
        ESP_LOGE(k_tag, "Allocation failed: decoder=%u packet=%u",
                 (unsigned)divx3_decoder_memory_bytes(decoder),
                 (unsigned)info.max_video_packet_size);
        finish(3);
    }

    while (frames < FRAME_LIMIT) {
        size_t packet_size = 0;
        Divx3Frame frame = {0};
        uint32_t start;
        uint32_t elapsed;
        result = divx3_avi_read_video_packet(
            file, &info, packet, info.max_video_packet_size,
            &packet_size);
        if (result == DIVX3_AVI_EOF) {
            break;
        }
        if (result != DIVX3_AVI_OK) {
            finish(4);
        }
        start = esp_cpu_get_cycle_count();
        result =
            divx3_decoder_decode(decoder, packet, packet_size, &frame);
        elapsed = esp_cpu_get_cycle_count() - start;
        if (result != DIVX3_OK) {
            ESP_LOGE(k_tag, "Frame %" PRIu32 " failed: %s",
                     frames, divx3_strerror(result));
            finish(5);
        }
        decode_cycles += elapsed;
        frame_cycles[frames] = elapsed;
        if (frame.intra) {
            intra_cycles += elapsed;
            ++intra_frames;
            if (elapsed > intra_max) {
                intra_max = elapsed;
            }
        } else {
            p_cycles += elapsed;
            ++p_frames;
            if (elapsed > p_max) {
                p_max = elapsed;
            }
        }
        frame_hash = hash_frame(frame_hash, &frame, scratch);
        ++frames;
    }
    if (frames == 0U || decode_cycles == 0U) {
        finish(6);
    }

    {
        uint32_t i;
        for (i = 1; i < frames; ++i) {
            uint32_t value = frame_cycles[i];
            uint32_t j = i;
            while (j != 0U && frame_cycles[j - 1U] > value) {
                frame_cycles[j] = frame_cycles[j - 1U];
                --j;
            }
            frame_cycles[j] = value;
        }
    }
    {
        uint32_t p50 = frame_cycles[(frames - 1U) / 2U];
        uint32_t p95 =
            frame_cycles[((frames * 95U + 99U) / 100U) - 1U];
        uint32_t maximum = frame_cycles[frames - 1U];
        uint32_t average = (uint32_t)(decode_cycles / frames);
        uint32_t intra_average =
            intra_frames != 0U
                ? (uint32_t)(intra_cycles / intra_frames)
                : 0U;
        uint32_t p_average =
            p_frames != 0U ? (uint32_t)(p_cycles / p_frames) : 0U;
        uint64_t fps_milli =
            (uint64_t)TARGET_CPU_HZ * frames * 1000U /
            decode_cycles;
        esp_rom_printf(
            "#D,frames,avg,p50,p95,max,i_count,i_avg,i_max,"
            "p_count,p_avg,p_max,fps_milli,hash,decoder,heap,largest\n");
        esp_rom_printf(
            "D,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
            "%08x%08x,%u,%u,%u\n",
            (unsigned)frames, (unsigned)average,
            (unsigned)p50, (unsigned)p95, (unsigned)maximum,
            (unsigned)intra_frames, (unsigned)intra_average,
            (unsigned)intra_max, (unsigned)p_frames,
            (unsigned)p_average, (unsigned)p_max,
            (unsigned)fps_milli,
            (unsigned)(frame_hash >> 32), (unsigned)frame_hash,
            (unsigned)divx3_decoder_memory_bytes(decoder),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(
                MALLOC_CAP_8BIT));
    }
    finish(0);
}
