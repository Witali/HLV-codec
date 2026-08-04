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

#include "pl_mpeg.h"

#define FRAME_LIMIT 90U
#define TARGET_CPU_HZ 240000000U

static const char *const k_tag = "mpeg1-qemu-bench";

extern const uint8_t k_video_start[]
    asm("_binary_qemu_mpeg1_benchmark_mpg_start");
extern const uint8_t k_video_end[]
    asm("_binary_qemu_mpeg1_benchmark_mpg_end");

static uint64_t hash_plane(uint64_t hash,
                           const plm_plane_t *plane,
                           unsigned bits,
                           unsigned width,
                           unsigned height,
                           uint8_t *scratch) {
    unsigned y;
    for (y = 0; y < height; ++y) {
        unsigned x;
        plm_plane_unpack_compact_samples(
            plane, 0, (int)y, bits, scratch, (int)width);
        for (x = 0; x < width; ++x) {
            hash ^= scratch[x];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

static uint64_t hash_frame(uint64_t hash,
                           const plm_frame_t *frame,
                           uint8_t *scratch) {
    unsigned chroma_width = (frame->width + 1U) / 2U;
    unsigned chroma_height = (frame->height + 1U) / 2U;
    hash = hash_plane(
        hash, &frame->y, 6, frame->width, frame->height, scratch);
    hash = hash_plane(
        hash, &frame->cb, 5, chroma_width, chroma_height, scratch);
    return hash_plane(
        hash, &frame->cr, 5, chroma_width, chroma_height, scratch);
}

static __attribute__((noreturn)) void finish(int code) {
    fflush(stdout);
    esp_rom_printf("MPEG1_BENCH_DONE,%d\n", code);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

void app_main(void) {
    size_t video_size = (size_t)(k_video_end - k_video_start);
    plm_t *mpeg;
    int width;
    int height;
    uint8_t *scratch;
    uint64_t decode_cycles = 0;
    uint64_t frame_hash = UINT64_C(1469598103934665603);
    uint32_t frame_cycles[FRAME_LIMIT] = {0};
    uint32_t frames = 0;

    ESP_LOGI(k_tag,
             "Xtensa compact decoder benchmark, MPEG=%u bytes",
             (unsigned)video_size);
    mpeg = plm_create_with_memory(
        (uint8_t *)k_video_start, video_size, false);
    if (mpeg == NULL) {
        finish(1);
    }
    plm_set_audio_enabled(mpeg, false);

    width = plm_get_width(mpeg);
    height = plm_get_height(mpeg);
    if (width <= 0 || width > 320 || height <= 0 || height > 240) {
        ESP_LOGE(k_tag, "Unexpected dimensions: %dx%d",
                 width, height);
        finish(2);
    }
    scratch = (uint8_t *)malloc((size_t)width);
    if (scratch == NULL) {
        finish(3);
    }

    while (frames < FRAME_LIMIT) {
        uint32_t start = esp_cpu_get_cycle_count();
        plm_frame_t *frame = plm_decode_video(mpeg);
        uint32_t elapsed = esp_cpu_get_cycle_count() - start;
        if (frame == NULL) {
            break;
        }
        if (frame->storage_mode != PLM_FRAME_STORAGE_Y6_U5_V5 ||
            frame->width != (unsigned)width ||
            frame->height != (unsigned)height) {
            finish(4);
        }
        decode_cycles += elapsed;
        frame_cycles[frames] = elapsed;
        frame_hash = hash_frame(frame_hash, frame, scratch);
        ++frames;
    }
    if (frames == 0U || decode_cycles == 0U) {
        finish(5);
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
        uint32_t average = (uint32_t)(decode_cycles / frames);
        uint32_t p50 = frame_cycles[(frames - 1U) / 2U];
        uint32_t p95 =
            frame_cycles[((frames * 95U + 99U) / 100U) - 1U];
        uint32_t maximum = frame_cycles[frames - 1U];
        uint64_t fps_milli =
            (uint64_t)TARGET_CPU_HZ * frames * 1000U /
            decode_cycles;
        esp_rom_printf(
            "#M,frames,avg,p50,p95,max,fps_milli,hash,heap,largest\n");
        esp_rom_printf(
            "M,%u,%u,%u,%u,%u,%u,%08x%08x,%u,%u\n",
            (unsigned)frames, (unsigned)average,
            (unsigned)p50, (unsigned)p95, (unsigned)maximum,
            (unsigned)fps_milli,
            (unsigned)(frame_hash >> 32), (unsigned)frame_hash,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(
                MALLOC_CAP_8BIT));
    }
    finish(0);
}
