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

namespace {

constexpr char kTag[] = "divx3-qemu-bench";
constexpr uint32_t kFrameLimit = 120;
constexpr uint32_t kTargetCpuHz = 240000000;

extern const uint8_t kVideoStart[]
    asm("_binary_qemu_divx3_benchmark_avi_start");
extern const uint8_t kVideoEnd[]
    asm("_binary_qemu_divx3_benchmark_avi_end");

uint64_t hashPlane(uint64_t hash, const uint8_t *plane, unsigned stride,
                   unsigned width, unsigned height,
                   const int8_t *correction,
                   unsigned correction_stride, unsigned bits,
                   uint8_t *scratch) {
    for (unsigned y = 0; y < height; ++y) {
        const uint8_t *row = plane + static_cast<size_t>(y) * stride;
        compact_yuv420_unpack_corrected_samples(
            row, 0, static_cast<int>(y), bits, correction,
            static_cast<int>(correction_stride), scratch,
            static_cast<int>(width));
        for (unsigned x = 0; x < width; ++x) {
            hash ^= scratch[x];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

uint64_t hashFrame(uint64_t hash, const Divx3Frame &frame,
                   uint8_t *scratch) {
    const unsigned chroma_width = (frame.width + 1U) / 2U;
    const unsigned chroma_height = (frame.height + 1U) / 2U;
    hash = hashPlane(
        hash, frame.y, frame.y_stride, frame.width, frame.height,
        frame.correction_y, frame.correction_stride_y,
        COMPACT_YUV420_LUMA_BITS, scratch);
    hash = hashPlane(
        hash, frame.cb, frame.c_stride, chroma_width, chroma_height,
        frame.correction_cb, frame.correction_stride_c,
        COMPACT_YUV420_CHROMA_BITS, scratch);
    return hashPlane(
        hash, frame.cr, frame.c_stride, chroma_width, chroma_height,
        frame.correction_cr, frame.correction_stride_c,
        COMPACT_YUV420_CHROMA_BITS, scratch);
}

[[noreturn]] void finish(int code) {
    fflush(stdout);
    esp_rom_printf("DIVX3_BENCH_DONE,%d\n", code);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

}  // namespace

extern "C" void app_main(void) {
    const size_t video_size =
        static_cast<size_t>(kVideoEnd - kVideoStart);
    ESP_LOGI(kTag, "Xtensa compact decoder benchmark, AVI=%u bytes",
             static_cast<unsigned>(video_size));
    FILE *file =
        fmemopen(const_cast<uint8_t *>(kVideoStart), video_size, "rb");
    if (!file) finish(1);

    Divx3AviInfo info{};
    int result = divx3_avi_read_info(file, &info);
    if (result != DIVX3_AVI_OK) {
        ESP_LOGE(kTag, "AVI probe failed: %s",
                 divx3_avi_strerror(result));
        fclose(file);
        finish(2);
    }

    Divx3Decoder *decoder =
        divx3_decoder_create_y6_u5_v5(info.width, info.height);
    uint8_t *packet =
        static_cast<uint8_t *>(malloc(info.max_video_packet_size));
    uint8_t *scratch = static_cast<uint8_t *>(malloc(info.width));
    if (!decoder || !packet || !scratch) {
        ESP_LOGE(kTag, "Allocation failed: decoder=%u packet=%u",
                 static_cast<unsigned>(
                     divx3_decoder_memory_bytes(decoder)),
                 static_cast<unsigned>(info.max_video_packet_size));
        finish(3);
    }

    uint64_t decode_cycles = 0;
    uint64_t intra_cycles = 0;
    uint64_t p_cycles = 0;
    // Match the portable decoder regression tool so the guest result can be
    // compared directly with the same embedded AVI on the host.
    uint64_t frame_hash = UINT64_C(1469598103934665603);
    uint32_t frame_cycles[kFrameLimit]{};
    uint32_t intra_frames = 0;
    uint32_t p_frames = 0;
    uint32_t intra_max = 0;
    uint32_t p_max = 0;
    uint32_t frames = 0;
    while (frames < kFrameLimit) {
        size_t packet_size = 0;
        result = divx3_avi_read_video_packet(
            file, &info, packet, info.max_video_packet_size,
            &packet_size);
        if (result == DIVX3_AVI_EOF) break;
        if (result != DIVX3_AVI_OK) finish(4);
        Divx3Frame frame{};
        const uint32_t start = esp_cpu_get_cycle_count();
        result = divx3_decoder_decode(
            decoder, packet, packet_size, &frame);
        const uint32_t elapsed = esp_cpu_get_cycle_count() - start;
        if (result != DIVX3_OK) {
            ESP_LOGE(kTag, "Frame %" PRIu32 " failed: %s", frames,
                     divx3_strerror(result));
            finish(5);
        }
        decode_cycles += elapsed;
        frame_cycles[frames] = elapsed;
        if (frame.intra) {
            intra_cycles += elapsed;
            ++intra_frames;
            if (elapsed > intra_max) intra_max = elapsed;
        } else {
            p_cycles += elapsed;
            ++p_frames;
            if (elapsed > p_max) p_max = elapsed;
        }
        frame_hash = hashFrame(frame_hash, frame, scratch);
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
    const uint32_t p50 = frame_cycles[(frames - 1U) / 2U];
    const uint32_t p95 =
        frame_cycles[((frames * 95U + 99U) / 100U) - 1U];
    const uint32_t maximum = frame_cycles[frames - 1U];
    const uint32_t average =
        static_cast<uint32_t>(decode_cycles / frames);
    const uint32_t intra_average =
        intra_frames
            ? static_cast<uint32_t>(intra_cycles / intra_frames)
            : 0;
    const uint32_t p_average =
        p_frames ? static_cast<uint32_t>(p_cycles / p_frames) : 0;
    const uint64_t fps_milli =
        static_cast<uint64_t>(kTargetCpuHz) * frames * 1000U /
        decode_cycles;
    esp_rom_printf(
        "#D,frames,avg,p50,p95,max,i_count,i_avg,i_max,"
        "p_count,p_avg,p_max,fps_milli,hash,decoder,heap,largest\n");
    esp_rom_printf(
        "D,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
        "%08x%08x,%u,%u,%u\n",
        static_cast<unsigned>(frames), static_cast<unsigned>(average),
        static_cast<unsigned>(p50), static_cast<unsigned>(p95),
        static_cast<unsigned>(maximum),
        static_cast<unsigned>(intra_frames),
        static_cast<unsigned>(intra_average),
        static_cast<unsigned>(intra_max), static_cast<unsigned>(p_frames),
        static_cast<unsigned>(p_average), static_cast<unsigned>(p_max),
        static_cast<unsigned>(fps_milli),
        static_cast<unsigned>(frame_hash >> 32),
        static_cast<unsigned>(frame_hash),
        static_cast<unsigned>(divx3_decoder_memory_bytes(decoder)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    finish(0);
}
