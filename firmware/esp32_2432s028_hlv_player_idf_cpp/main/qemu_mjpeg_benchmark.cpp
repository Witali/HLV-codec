#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mjpeg_avi_decoder.hpp"

namespace {

#ifndef MJPEG_QEMU_FRAME_LIMIT
#define MJPEG_QEMU_FRAME_LIMIT 12
#endif

#ifndef MJPEG_QEMU_CALLBACK_INPUT
#define MJPEG_QEMU_CALLBACK_INPUT 0
#endif

constexpr char kTag[] = "mjpeg-qemu-bench";
constexpr uint32_t kFrameLimit = MJPEG_QEMU_FRAME_LIMIT;
constexpr uint32_t kTargetCpuHz = 240000000;
constexpr size_t kStripPixels = 320U * 16U;

extern const uint8_t kVideoStart[]
    asm("_binary_qemu_mjpeg_benchmark_avi_start");
extern const uint8_t kVideoEnd[]
    asm("_binary_qemu_mjpeg_benchmark_avi_end");

struct OutputContext {
    uint16_t *buffers[2]{};
    unsigned next_buffer = 0;
    uint64_t hash = UINT64_C(1469598103934665603);
    uint64_t callback_cycles = 0;
};

#if MJPEG_QEMU_CALLBACK_INPUT
struct InputContext {
    FILE *file = nullptr;
    size_t remaining = 0;
    long next_packet_offset = -1;
};

size_t readInput(void *opaque, uint8_t *destination,
                 size_t capacity) {
    auto *context = static_cast<InputContext *>(opaque);
    if (!context || !context->file || !destination || !capacity) {
        return 0;
    }
    const size_t requested = capacity < context->remaining
                                 ? capacity
                                 : context->remaining;
    const size_t bytes =
        fread(destination, 1, requested, context->file);
    context->remaining -= bytes;
    if (!context->remaining &&
        fseek(context->file, context->next_packet_offset, SEEK_SET)) {
        return 0;
    }
    return bytes;
}
#endif

uint16_t *acquireStrip(void *opaque, uint16_t, uint16_t rows) {
    auto *context = static_cast<OutputContext *>(opaque);
    if (!context || !rows || rows > 16U) return nullptr;
    return context->buffers[context->next_buffer++ & 1U];
}

bool submitStrip(void *opaque, const uint16_t *rgb565, uint16_t,
                 uint16_t rows) {
    auto *context = static_cast<OutputContext *>(opaque);
    if (!context || !rgb565 || !rows || rows > 16U) return false;
    const uint32_t start = esp_cpu_get_cycle_count();
    const size_t pixels = static_cast<size_t>(320U) * rows;
    for (size_t i = 0; i < pixels; ++i) {
        context->hash ^= static_cast<uint8_t>(rgb565[i]);
        context->hash *= UINT64_C(1099511628211);
        context->hash ^= static_cast<uint8_t>(rgb565[i] >> 8);
        context->hash *= UINT64_C(1099511628211);
    }
    context->callback_cycles +=
        esp_cpu_get_cycle_count() - start;
    return true;
}

[[noreturn]] void finish(int code) {
    fflush(stdout);
    esp_rom_printf("MJPEG_BENCH_DONE,%d\n", code);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

}  // namespace

extern "C" void app_main(void) {
    const size_t video_size =
        static_cast<size_t>(kVideoEnd - kVideoStart);
    FILE *file =
        fmemopen(const_cast<uint8_t *>(kVideoStart), video_size, "rb");
    if (!file) finish(1);

    MjpegAviDecoder decoder;
    MjpegAviInfo info{};
    const int begin_result = decoder.begin(file, &info, false);
    if (begin_result != MJPEG_AVI_OK ||
        info.width != 320 || info.height != 240) {
        finish(2);
    }
    OutputContext output{};
    output.buffers[0] = static_cast<uint16_t *>(
        heap_caps_aligned_alloc(
            16, kStripPixels * sizeof(uint16_t), MALLOC_CAP_8BIT));
    output.buffers[1] = static_cast<uint16_t *>(
        heap_caps_aligned_alloc(
            16, kStripPixels * sizeof(uint16_t), MALLOC_CAP_8BIT));
    if (!output.buffers[0] || !output.buffers[1]) finish(3);

    uint64_t total_cycles = 0;
    uint64_t total_header_cycles = 0;
    uint64_t total_geometry_cycles = 0;
    uint64_t total_process_cycles = 0;
    uint64_t total_callback_cycles = 0;
    uint32_t frame_cycles[kFrameLimit]{};
    uint32_t frames = 0;
    while (frames < kFrameLimit) {
        MjpegAviPacket packet{};
#if MJPEG_QEMU_CALLBACK_INPUT
        InputContext input{};
#endif
        if (decoder.readPacket(file, &packet) != MJPEG_AVI_OK) finish(6);
#if MJPEG_QEMU_CALLBACK_INPUT
        input.file = file;
        input.remaining = packet.jpeg_size;
        input.next_packet_offset = packet.next_offset;
        packet.input_read = readInput;
        packet.input_context = &input;
        packet.file = nullptr;
#endif
        const uint64_t callback_before = output.callback_cycles;
        const uint32_t start = esp_cpu_get_cycle_count();
        const int decode_result = decoder.decodeDirect(
            packet, acquireStrip, submitStrip, &output);
        if (decode_result != MJPEG_AVI_OK)
            finish(8);
        const uint32_t elapsed = esp_cpu_get_cycle_count() - start;
        frame_cycles[frames++] = elapsed;
        total_cycles += elapsed;
        const MjpegAviDecodeCycles &phases =
            decoder.lastDecodeCycles();
        total_header_cycles += phases.parse_header;
        total_geometry_cycles += phases.geometry;
        total_process_cycles += phases.process;
        total_callback_cycles +=
            output.callback_cycles - callback_before;
    }

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
        static_cast<uint32_t>(total_cycles / frames);
    const uint32_t p50 = frame_cycles[(frames - 1U) / 2U];
    const uint32_t p95 =
        frame_cycles[((frames * 95U + 99U) / 100U) - 1U];
    const uint32_t maximum = frame_cycles[frames - 1U];
    const uint32_t header_average =
        static_cast<uint32_t>(total_header_cycles / frames);
    const uint32_t geometry_average =
        static_cast<uint32_t>(total_geometry_cycles / frames);
    const uint32_t process_average =
        static_cast<uint32_t>(total_process_cycles / frames);
    const uint32_t callback_average =
        static_cast<uint32_t>(total_callback_cycles / frames);
    const uint32_t decoder_average =
        average > callback_average ? average - callback_average : 0;
    const uint64_t fps_milli =
        static_cast<uint64_t>(kTargetCpuHz) * frames * 1000U /
        total_cycles;
    esp_rom_printf(
        "#J,frames,avg,p50,p95,max,fps_milli,hash,heap,largest,"
        "decoder_avg,header_avg,geometry_avg,process_avg,callback_avg\n");
    esp_rom_printf(
        "J,%u,%u,%u,%u,%u,%u,%08x%08x,%u,%u,%u,%u,%u,%u,%u\n",
        static_cast<unsigned>(frames),
        static_cast<unsigned>(average), static_cast<unsigned>(p50),
        static_cast<unsigned>(p95), static_cast<unsigned>(maximum),
        static_cast<unsigned>(fps_milli),
        static_cast<unsigned>(output.hash >> 32),
        static_cast<unsigned>(output.hash),
        static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_8BIT)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
        static_cast<unsigned>(decoder_average),
        static_cast<unsigned>(header_average),
        static_cast<unsigned>(geometry_average),
        static_cast<unsigned>(process_average),
        static_cast<unsigned>(callback_average));
    ESP_LOGI(kTag, "esp_new_jpeg block benchmark complete");
    finish(0);
}
