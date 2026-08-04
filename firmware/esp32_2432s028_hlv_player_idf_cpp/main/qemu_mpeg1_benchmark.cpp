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

namespace {

constexpr char kTag[] = "mpeg1-qemu-bench";
constexpr uint32_t kFrameLimit = 90;
constexpr uint32_t kTargetCpuHz = 240000000;

extern const uint8_t kVideoStart[]
    asm("_binary_qemu_mpeg1_benchmark_mpg_start");
extern const uint8_t kVideoEnd[]
    asm("_binary_qemu_mpeg1_benchmark_mpg_end");

uint64_t hashPlane(uint64_t hash, const plm_plane_t &plane,
                   unsigned bits, unsigned width, unsigned height,
                   uint8_t *scratch) {
    for (unsigned y = 0; y < height; ++y) {
        plm_plane_unpack_compact_samples(
            &plane, 0, static_cast<int>(y), bits, scratch,
            static_cast<int>(width));
        for (unsigned x = 0; x < width; ++x) {
            hash ^= scratch[x];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

uint64_t hashFrame(uint64_t hash, const plm_frame_t &frame,
                   uint8_t *scratch) {
    const unsigned chroma_width = (frame.width + 1U) / 2U;
    const unsigned chroma_height = (frame.height + 1U) / 2U;
    hash = hashPlane(
        hash, frame.y, 6, frame.width, frame.height, scratch);
    hash = hashPlane(
        hash, frame.cb, 5, chroma_width, chroma_height, scratch);
    return hashPlane(
        hash, frame.cr, 5, chroma_width, chroma_height, scratch);
}

[[noreturn]] void finish(int code) {
    fflush(stdout);
    esp_rom_printf("MPEG1_BENCH_DONE,%d\n", code);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

}  // namespace

extern "C" void app_main(void) {
    const size_t video_size =
        static_cast<size_t>(kVideoEnd - kVideoStart);
    ESP_LOGI(kTag, "Xtensa compact decoder benchmark, MPEG=%u bytes",
             static_cast<unsigned>(video_size));
    plm_t *mpeg = plm_create_with_memory(
        const_cast<uint8_t *>(kVideoStart), video_size, false);
    if (!mpeg) finish(1);
    plm_set_audio_enabled(mpeg, false);

    const int width = plm_get_width(mpeg);
    const int height = plm_get_height(mpeg);
    if (width <= 0 || width > 320 || height <= 0 || height > 240) {
        ESP_LOGE(kTag, "Unexpected dimensions: %dx%d", width, height);
        finish(2);
    }
    uint8_t *scratch = static_cast<uint8_t *>(malloc(width));
    if (!scratch) finish(3);

    uint64_t decode_cycles = 0;
    uint64_t frame_hash = UINT64_C(1469598103934665603);
    uint32_t frame_cycles[kFrameLimit]{};
    uint32_t frames = 0;
    while (frames < kFrameLimit) {
        const uint32_t start = esp_cpu_get_cycle_count();
        plm_frame_t *frame = plm_decode_video(mpeg);
        const uint32_t elapsed = esp_cpu_get_cycle_count() - start;
        if (!frame) break;
        if (frame->storage_mode != PLM_FRAME_STORAGE_Y6_U5_V5 ||
            frame->width != static_cast<unsigned>(width) ||
            frame->height != static_cast<unsigned>(height)) {
            finish(4);
        }
        decode_cycles += elapsed;
        frame_cycles[frames] = elapsed;
        frame_hash = hashFrame(frame_hash, *frame, scratch);
        ++frames;
    }
    if (!frames || !decode_cycles) finish(5);

    for (uint32_t i = 1; i < frames; ++i) {
        const uint32_t value = frame_cycles[i];
        uint32_t j = i;
        while (j && frame_cycles[j - 1] > value) {
            frame_cycles[j] = frame_cycles[j - 1];
            --j;
        }
        frame_cycles[j] = value;
    }
    const uint32_t average =
        static_cast<uint32_t>(decode_cycles / frames);
    const uint32_t p50 = frame_cycles[(frames - 1U) / 2U];
    const uint32_t p95 =
        frame_cycles[((frames * 95U + 99U) / 100U) - 1U];
    const uint32_t maximum = frame_cycles[frames - 1U];
    const uint64_t fps_milli =
        static_cast<uint64_t>(kTargetCpuHz) * frames * 1000U /
        decode_cycles;
    esp_rom_printf(
        "#M,frames,avg,p50,p95,max,fps_milli,hash,heap,largest\n");
    esp_rom_printf(
        "M,%u,%u,%u,%u,%u,%u,%08x%08x,%u,%u\n",
        static_cast<unsigned>(frames), static_cast<unsigned>(average),
        static_cast<unsigned>(p50), static_cast<unsigned>(p95),
        static_cast<unsigned>(maximum),
        static_cast<unsigned>(fps_milli),
        static_cast<unsigned>(frame_hash >> 32),
        static_cast<unsigned>(frame_hash),
        static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_8BIT)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
#if PLM_MPEG_DECODE_PROFILE
    plm_video_decode_profile_t profile{};
    plm_get_video_decode_profile(mpeg, &profile);
    esp_rom_printf(
        "MDP,%u,%u,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u\n",
        static_cast<unsigned>(profile.frames),
        static_cast<unsigned>(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ),
        static_cast<unsigned long long>(profile.total_cycles),
        static_cast<unsigned long long>(profile.coefficient_cycles),
        static_cast<unsigned long long>(profile.reconstruction_cycles),
        static_cast<unsigned long long>(profile.motion_cycles),
        static_cast<unsigned long long>(profile.compact_cycles),
        static_cast<unsigned>(profile.blocks),
        static_cast<unsigned>(profile.dc_only_blocks),
        static_cast<unsigned>(profile.general_idct_blocks),
        static_cast<unsigned>(profile.motion_macroblocks));
#endif
    finish(0);
}
