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

#include "h263_3gp.h"

namespace {

constexpr uint32_t kFrameLimit = 90;
constexpr uint32_t kTargetCpuHz = 240000000;

#ifdef MPEG4_QEMU_BENCHMARK
constexpr char kTag[] = "mpeg4-qemu-bench";
constexpr uint16_t kBenchWidth = 320;
constexpr uint16_t kBenchHeight = 240;
constexpr uint8_t kOutputBuffers = 2;
constexpr char kContainer[] = "AVI";
extern const uint8_t kVideoStart[]
    asm("_binary_qemu_mpeg4_benchmark_avi_start");
extern const uint8_t kVideoEnd[]
    asm("_binary_qemu_mpeg4_benchmark_avi_end");
#else
constexpr char kTag[] = "h263-qemu-bench";
constexpr uint16_t kBenchWidth = 320;
constexpr uint16_t kBenchHeight = 240;
constexpr uint8_t kOutputBuffers = 1;
constexpr char kContainer[] = "3GP";
extern const uint8_t kVideoStart[]
    asm("_binary_qemu_h263_benchmark_3gp_start");
extern const uint8_t kVideoEnd[]
    asm("_binary_qemu_h263_benchmark_3gp_end");
#endif

uint64_t hashPlane(uint64_t hash, const uint8_t *plane,
                   unsigned stride, unsigned width, unsigned height) {
    for (unsigned y = 0; y < height; ++y) {
        const uint8_t *row = plane + static_cast<size_t>(y) * stride;
        for (unsigned x = 0; x < width; ++x) {
            hash ^= row[x];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

uint64_t hashFrame(uint64_t hash, const H2633gpFrame &frame) {
    const unsigned chroma_width = (frame.width + 1U) / 2U;
    const unsigned chroma_height = (frame.height + 1U) / 2U;
    hash = hashPlane(
        hash, frame.y, frame.y_stride, frame.width, frame.height);
    hash = hashPlane(
        hash, frame.u, frame.chroma_stride, chroma_width, chroma_height);
    return hashPlane(
        hash, frame.v, frame.chroma_stride, chroma_width, chroma_height);
}

[[noreturn]] void finish(int code) {
    fflush(stdout);
#ifdef MPEG4_QEMU_BENCHMARK
    esp_rom_printf("MPEG4_BENCH_DONE,%d\n", code);
#else
    esp_rom_printf("H263_BENCH_DONE,%d\n", code);
#endif
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

}  // namespace

extern "C" void app_main(void) {
    const size_t video_size =
        static_cast<size_t>(kVideoEnd - kVideoStart);
    ESP_LOGI(kTag, "Xtensa decoder benchmark, %s=%u bytes",
             kContainer,
             static_cast<unsigned>(video_size));
    FILE *file =
        fmemopen(const_cast<uint8_t *>(kVideoStart), video_size, "rb");
    if (!file) finish(1);

    H2633gpDecoder *decoder = h263_3gp_decoder_create();
    if (!decoder ||
        h263_3gp_decoder_set_output_buffer_count(
            decoder, kOutputBuffers) !=
            H263_3GP_OK) {
        finish(2);
    }
    H2633gpInfo info{};
    int result = h263_3gp_decoder_open(decoder, file, &info);
    if (result != H263_3GP_OK) {
        ESP_LOGE(kTag, "Open failed: %s", h263_3gp_strerror(result));
        finish(3);
    }
    if (info.width != kBenchWidth || info.height != kBenchHeight
#ifdef MPEG4_QEMU_BENCHMARK
        || info.video_codec != H263_VIDEO_CODEC_MPEG4_SIMPLE
#endif
    ) {
        finish(4);
    }

    uint64_t decode_cycles = 0;
    uint64_t frame_hash = UINT64_C(1469598103934665603);
    uint32_t frame_cycles[kFrameLimit]{};
    uint32_t frames = 0;
    while (frames < kFrameLimit) {
        H2633gpFrame frame{};
        const uint32_t start = esp_cpu_get_cycle_count();
        result = h263_3gp_decoder_decode_next(decoder, file, &frame);
        const uint32_t elapsed = esp_cpu_get_cycle_count() - start;
        if (result == H263_3GP_EOF) break;
        if (result != H263_3GP_OK) {
            ESP_LOGE(kTag, "Frame %" PRIu32 " failed: %s", frames,
                     h263_3gp_strerror(result));
            finish(5);
        }
        decode_cycles += elapsed;
        frame_cycles[frames] = elapsed;
        frame_hash = hashFrame(frame_hash, frame);
        ++frames;
    }
    if (!frames || !decode_cycles) finish(6);

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
#ifdef MPEG4_QEMU_BENCHMARK
    esp_rom_printf(
        "#M,frames,avg,p50,p95,max,fps_milli,hash,decoder,heap,largest\n");
    esp_rom_printf(
        "M,%u,%u,%u,%u,%u,%u,%08x%08x,%u,%u,%u\n",
#else
    esp_rom_printf(
        "#H,frames,avg,p50,p95,max,fps_milli,hash,decoder,heap,largest\n");
    esp_rom_printf(
        "H,%u,%u,%u,%u,%u,%u,%08x%08x,%u,%u,%u\n",
#endif
        static_cast<unsigned>(frames), static_cast<unsigned>(average),
        static_cast<unsigned>(p50), static_cast<unsigned>(p95),
        static_cast<unsigned>(maximum),
        static_cast<unsigned>(fps_milli),
        static_cast<unsigned>(frame_hash >> 32),
        static_cast<unsigned>(frame_hash),
        static_cast<unsigned>(h263_3gp_decoder_memory_bytes(decoder)),
        static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_8BIT)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    finish(0);
}
