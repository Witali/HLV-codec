#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>

#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hlv_esp32_decoder.h"

#define FRAME_LIMIT 120U
#define TARGET_CPU_HZ 240000000U

static const char *const k_tag = "hlv-qemu-bench";

extern const uint8_t k_video_start[]
    asm("_binary_qemu_benchmark_hlv_start");
extern const uint8_t k_video_end[]
    asm("_binary_qemu_benchmark_hlv_end");

static uint64_t hash_frame(uint64_t hash, const HLV1Frame *frame) {
    size_t y_bytes =
        (size_t)frame->stride_y * frame->padded_height;
    size_t chroma_height = (size_t)frame->padded_height / 2U;
    size_t u_bytes = (size_t)frame->stride_u * chroma_height;
    size_t v_bytes = (size_t)frame->stride_v * chroma_height;
    size_t correction_y_bytes =
        (size_t)frame->correction_stride_y *
        ((size_t)frame->padded_height / 8U);
    size_t correction_chroma_height =
        (size_t)frame->padded_height / 16U;
    size_t correction_u_bytes =
        (size_t)frame->correction_stride_u *
        correction_chroma_height;
    size_t correction_v_bytes =
        (size_t)frame->correction_stride_v *
        correction_chroma_height;
    const uint8_t *planes[] = {
        frame->y,
        frame->u,
        frame->v,
        (const uint8_t *)frame->correction_y,
        (const uint8_t *)frame->correction_u,
        (const uint8_t *)frame->correction_v,
    };
    const size_t sizes[] = {
        y_bytes,
        u_bytes,
        v_bytes,
        correction_y_bytes,
        correction_u_bytes,
        correction_v_bytes,
    };
    size_t plane;

    for (plane = 0; plane < 6U; ++plane) {
        size_t i;
        for (i = 0; i < sizes[plane]; ++i) {
            hash ^= planes[plane][i];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

static __attribute__((noreturn)) void finish(int code) {
    fflush(stdout);
    esp_rom_printf("HLV_BENCH_DONE,%d\n", code);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

void app_main(void) {
    size_t video_size = (size_t)(k_video_end - k_video_start);
    FILE *file;
    HLV1Header header = {0};
    hlv_esp32_decoder_t decoder = {0};
    int result;
    uint64_t decode_cycles = 0;
    uint64_t key_cycles = 0;
    uint64_t p_cycles = 0;
    uint64_t frame_hash = UINT64_C(14695981039346656037);
    uint32_t frame_cycles[FRAME_LIMIT] = {0};
    uint32_t key_frames = 0;
    uint32_t p_frames = 0;
    uint32_t key_max = 0;
    uint32_t p_max = 0;
    uint32_t frames = 0;

    ESP_LOGI(k_tag,
             "Xtensa decoder benchmark, embedded clip=%u bytes",
             (unsigned)video_size);
    file = fmemopen((void *)k_video_start, video_size, "rb");
    if (file == NULL) {
        ESP_LOGE(k_tag, "Cannot open benchmark video");
        finish(1);
    }

    result = hlv1_header_read(file, &header);
    if (result < 0) {
        ESP_LOGE(k_tag, "Header read failed: %s",
                 hlv1_strerror(result));
        fclose(file);
        finish(2);
    }

    result = hlv_esp32_decoder_begin(&decoder, &header, true);
    if (result < 0) {
        fclose(file);
        finish(3);
    }

    while (frames < FRAME_LIMIT) {
        HLV1Packet packet = {0};
        const HLV1Frame *frame = NULL;
        uint32_t start;
        uint32_t elapsed;
        uint8_t frame_type;

        start = esp_cpu_get_cycle_count();
        result = hlv_esp32_decoder_decode_next(
            &decoder, file, &frame, &packet);
        elapsed = esp_cpu_get_cycle_count() - start;
        if (result == HLV1_EOF) {
            break;
        }
        frame_type = packet.frame_type;
        if (result < 0 || frame == NULL) {
            ESP_LOGE(k_tag,
                     "Frame %" PRIu32 " decode failed: %s",
                     frames, hlv1_strerror(result));
            fclose(file);
            finish(5);
        }
        decode_cycles += elapsed;
        frame_cycles[frames] = elapsed;
        if (frame_type == HLV1_FRAME_KEY) {
            key_cycles += elapsed;
            ++key_frames;
            if (elapsed > key_max) {
                key_max = elapsed;
            }
        } else {
            p_cycles += elapsed;
            ++p_frames;
            if (elapsed > p_max) {
                p_max = elapsed;
            }
        }
        frame_hash = hash_frame(frame_hash, frame);
        ++frames;
    }

    fclose(file);
    hlv_esp32_decoder_end(&decoder);
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
        uint32_t p95_index =
            ((frames * 95U + 99U) / 100U) - 1U;
        uint32_t p95 = frame_cycles[p95_index];
        uint32_t maximum = frame_cycles[frames - 1U];
        uint32_t cycles_per_frame =
            (uint32_t)(decode_cycles / frames);
        uint32_t key_average =
            key_frames != 0U
                ? (uint32_t)(key_cycles / key_frames)
                : 0U;
        uint32_t p_average =
            p_frames != 0U
                ? (uint32_t)(p_cycles / p_frames)
                : 0U;
        uint64_t fps_milli =
            (uint64_t)TARGET_CPU_HZ * frames * 1000U /
            decode_cycles;
        esp_rom_printf(
            "#B,frames,avg,p50,p95,max,key_count,key_avg,key_max,"
            "p_count,p_avg,p_max,fps_milli,hash,heap,largest\n");
        esp_rom_printf(
            "B,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
            "%08x%08x,%u,%u\n",
            (unsigned)frames, (unsigned)cycles_per_frame,
            (unsigned)p50, (unsigned)p95, (unsigned)maximum,
            (unsigned)key_frames, (unsigned)key_average,
            (unsigned)key_max, (unsigned)p_frames,
            (unsigned)p_average, (unsigned)p_max,
            (unsigned)fps_milli,
            (unsigned)(frame_hash >> 32), (unsigned)frame_hash,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(
                MALLOC_CAP_8BIT));
    }
    finish(0);
}
