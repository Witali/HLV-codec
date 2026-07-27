#include <inttypes.h>
#include <stdio.h>

#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hlv_esp32_decoder.hpp"

namespace {

constexpr char kTag[] = "hlv-qemu-bench";
constexpr uint32_t kFrameLimit = 120;
constexpr uint32_t kTargetCpuHz = 240000000;

extern const uint8_t kVideoStart[] asm("_binary_qemu_benchmark_hlv_start");
extern const uint8_t kVideoEnd[] asm("_binary_qemu_benchmark_hlv_end");

uint64_t hashFrame(uint64_t hash, const HLV1Frame &frame) {
    const size_t y_bytes =
        static_cast<size_t>(frame.stride_y) * frame.padded_height;
    const size_t chroma_height = static_cast<size_t>(frame.padded_height) / 2U;
    const size_t u_bytes = static_cast<size_t>(frame.stride_u) * chroma_height;
    const size_t v_bytes = static_cast<size_t>(frame.stride_v) * chroma_height;
    const uint8_t *planes[] = {frame.y, frame.u, frame.v};
    const size_t sizes[] = {y_bytes, u_bytes, v_bytes};
    for (size_t plane = 0; plane < 3; ++plane) {
        for (size_t i = 0; i < sizes[plane]; ++i) {
            hash ^= planes[plane][i];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

[[noreturn]] void finish(int code) {
    fflush(stdout);
    esp_rom_printf("HLV_BENCH_DONE,%d\n", code);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

}  // namespace

extern "C" void app_main(void) {
    size_t video_size = static_cast<size_t>(kVideoEnd - kVideoStart);
    ESP_LOGI(kTag, "Xtensa decoder benchmark, embedded clip=%u bytes",
             static_cast<unsigned>(video_size));
    FILE *file = fmemopen(const_cast<uint8_t *>(kVideoStart), video_size, "rb");
    if (!file) {
        ESP_LOGE(kTag, "Cannot open benchmark video");
        finish(1);
    }

    HLV1Header header{};
    int result = hlv1_header_read(file, &header);
    if (result < 0) {
        ESP_LOGE(kTag, "Header read failed: %s", hlv1_strerror(result));
        fclose(file);
        finish(2);
    }

    HlvEsp32Decoder decoder;
    result = decoder.begin(header, true);
    if (result < 0) {
        fclose(file);
        finish(3);
    }

    uint64_t decode_cycles = 0;
    uint64_t key_cycles = 0;
    uint64_t p_cycles = 0;
    uint64_t frame_hash = UINT64_C(14695981039346656037);
    uint32_t frame_cycles[kFrameLimit]{};
    uint32_t key_frames = 0;
    uint32_t p_frames = 0;
    uint32_t key_max = 0;
    uint32_t p_max = 0;
    uint32_t frames = 0;
    while (frames < kFrameLimit) {
        HLV1Packet packet{};
        const HLV1Frame *frame = nullptr;
        uint32_t start = esp_cpu_get_cycle_count();
        result = decoder.decodeNext(file, &frame, &packet);
        uint32_t elapsed = esp_cpu_get_cycle_count() - start;
        if (result == HLV1_EOF) break;
        const uint8_t frame_type = packet.frame_type;
        if (result < 0 || !frame) {
            ESP_LOGE(kTag, "Frame %" PRIu32 " decode failed: %s", frames,
                     hlv1_strerror(result));
            fclose(file);
            finish(5);
        }
        decode_cycles += elapsed;
        frame_cycles[frames] = elapsed;
        if (frame_type == HLV1_FRAME_KEY) {
            key_cycles += elapsed;
            ++key_frames;
            if (elapsed > key_max) key_max = elapsed;
        } else {
            p_cycles += elapsed;
            ++p_frames;
            if (elapsed > p_max) p_max = elapsed;
        }
        frame_hash = hashFrame(frame_hash, *frame);
        ++frames;
    }

    fclose(file);
    decoder.end();
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
    const uint32_t p50 = frame_cycles[(frames - 1U) / 2U];
    const uint32_t p95_index =
        ((frames * 95U + 99U) / 100U) - 1U;
    const uint32_t p95 = frame_cycles[p95_index];
    const uint32_t maximum = frame_cycles[frames - 1U];
    const uint32_t cycles_per_frame =
        static_cast<uint32_t>(decode_cycles / frames);
    const uint32_t key_average =
        key_frames ? static_cast<uint32_t>(key_cycles / key_frames) : 0;
    const uint32_t p_average =
        p_frames ? static_cast<uint32_t>(p_cycles / p_frames) : 0;
    const uint64_t fps_milli =
        static_cast<uint64_t>(kTargetCpuHz) * frames * 1000U / decode_cycles;
    esp_rom_printf(
        "#B,frames,avg,p50,p95,max,key_count,key_avg,key_max,"
        "p_count,p_avg,p_max,fps_milli,hash,heap,largest\n");
    esp_rom_printf(
        "B,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
        "%08x%08x,%u,%u\n",
        static_cast<unsigned>(frames),
        static_cast<unsigned>(cycles_per_frame),
        static_cast<unsigned>(p50), static_cast<unsigned>(p95),
        static_cast<unsigned>(maximum), static_cast<unsigned>(key_frames),
        static_cast<unsigned>(key_average), static_cast<unsigned>(key_max),
        static_cast<unsigned>(p_frames), static_cast<unsigned>(p_average),
        static_cast<unsigned>(p_max), static_cast<unsigned>(fps_milli),
        static_cast<unsigned>(frame_hash >> 32),
        static_cast<unsigned>(frame_hash),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    finish(0);
}
