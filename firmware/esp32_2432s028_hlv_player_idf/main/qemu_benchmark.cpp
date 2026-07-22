#include <inttypes.h>
#include <stdio.h>

#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
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
    ESP_LOGI(kTag, "HLV_QEMU_BENCH_DONE code=%d", code);
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
    uint64_t frame_hash = UINT64_C(14695981039346656037);
    uint32_t frames = 0;
    while (frames < kFrameLimit) {
        HLV1Packet packet{};
        result = decoder.readPacket(file, &packet);
        if (result == HLV1_EOF) break;
        if (result < 0) {
            ESP_LOGE(kTag, "Packet %" PRIu32 " read failed: %s", frames,
                     hlv1_strerror(result));
            hlv1_packet_free(&packet);
            fclose(file);
            finish(4);
        }

        const HLV1Frame *frame = nullptr;
        uint32_t start = esp_cpu_get_cycle_count();
        result = decoder.decode(&packet, &frame);
        uint32_t elapsed = esp_cpu_get_cycle_count() - start;
        hlv1_packet_free(&packet);
        if (result < 0 || !frame) {
            ESP_LOGE(kTag, "Frame %" PRIu32 " decode failed: %s", frames,
                     hlv1_strerror(result));
            fclose(file);
            finish(5);
        }
        decode_cycles += elapsed;
        frame_hash = hashFrame(frame_hash, *frame);
        ++frames;
    }

    fclose(file);
    decoder.end();
    if (!frames || !decode_cycles) finish(6);

    uint64_t cycles_per_frame = decode_cycles / frames;
    uint64_t fps_milli =
        static_cast<uint64_t>(kTargetCpuHz) * frames * 1000U / decode_cycles;
    ESP_LOGI(kTag,
             "frames=%u, decode_cycles=0x%08x%08x, cycles/frame=%u, "
             "estimated_fps=%u.%03u",
             static_cast<unsigned>(frames),
             static_cast<unsigned>(decode_cycles >> 32),
             static_cast<unsigned>(decode_cycles),
             static_cast<unsigned>(cycles_per_frame),
             static_cast<unsigned>(fps_milli / 1000U),
             static_cast<unsigned>(fps_milli % 1000U));
    ESP_LOGI(kTag, "frame_hash=%08x%08x, heap=%u, largest=%u",
             static_cast<unsigned>(frame_hash >> 32),
             static_cast<unsigned>(frame_hash),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    finish(0);
}
