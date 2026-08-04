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

#include "mjpeg_avi_decoder.h"

#ifndef MJPEG_QEMU_FRAME_LIMIT
#define MJPEG_QEMU_FRAME_LIMIT 12
#endif

#define TARGET_CPU_HZ 240000000U
#define STRIP_PIXELS (320U * 16U)

static const char *const k_tag = "mjpeg-qemu-bench";

extern const uint8_t k_video_start[]
    asm("_binary_qemu_mjpeg_benchmark_avi_start");
extern const uint8_t k_video_end[]
    asm("_binary_qemu_mjpeg_benchmark_avi_end");

typedef struct {
    uint16_t *buffers[2];
    unsigned next_buffer;
    uint64_t hash;
    uint64_t callback_cycles;
} output_context_t;

static uint16_t *acquire_strip(void *opaque,
                               uint16_t y,
                               uint16_t rows) {
    output_context_t *context = (output_context_t *)opaque;
    (void)y;
    if (context == NULL || rows == 0U || rows > 16U) {
        return NULL;
    }
    return context->buffers[context->next_buffer++ & 1U];
}

static bool submit_strip(void *opaque,
                         const uint16_t *rgb565,
                         uint16_t y,
                         uint16_t rows) {
    output_context_t *context = (output_context_t *)opaque;
    uint32_t start;
    size_t pixels;
    size_t i;
    (void)y;
    if (context == NULL || rgb565 == NULL ||
        rows == 0U || rows > 16U) {
        return false;
    }
    start = esp_cpu_get_cycle_count();
    pixels = (size_t)320U * rows;
    for (i = 0; i < pixels; ++i) {
        context->hash ^= (uint8_t)rgb565[i];
        context->hash *= UINT64_C(1099511628211);
        context->hash ^= (uint8_t)(rgb565[i] >> 8);
        context->hash *= UINT64_C(1099511628211);
    }
    context->callback_cycles +=
        esp_cpu_get_cycle_count() - start;
    return true;
}

static __attribute__((noreturn)) void finish(int code) {
    fflush(stdout);
    esp_rom_printf("MJPEG_BENCH_DONE,%d\n", code);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

void app_main(void) {
    size_t video_size = (size_t)(k_video_end - k_video_start);
    FILE *file =
        fmemopen((void *)k_video_start, video_size, "rb");
    mjpeg_avi_decoder_t decoder = {0};
    mjpeg_avi_info_t info = {0};
    output_context_t output = {0};
    int begin_result;
    uint64_t total_cycles = 0;
    uint64_t total_header_cycles = 0;
    uint64_t total_geometry_cycles = 0;
    uint64_t total_process_cycles = 0;
    uint64_t total_callback_cycles = 0;
    uint64_t total_refill_bytes = 0;
    uint32_t total_refills = 0;
    uint32_t maximum_packet = 0;
    uint32_t frame_cycles[MJPEG_QEMU_FRAME_LIMIT] = {0};
    uint32_t frames = 0;

    if (file == NULL) {
        finish(1);
    }
    begin_result =
        mjpeg_avi_decoder_begin(&decoder, file, &info, false);
    if (begin_result != MJPEG_AVI_OK ||
        info.width != 320U || info.height != 240U) {
        finish(2);
    }
    ESP_LOGI(k_tag, "AVI audio tag=%u rate=%u align=%u samples/block=%u",
             (unsigned)info.audio_format_tag,
             (unsigned)info.audio_sample_rate,
             (unsigned)info.audio_block_align,
             (unsigned)info.audio_samples_per_block);
    output.hash = UINT64_C(1469598103934665603);
    output.buffers[0] = (uint16_t *)heap_caps_aligned_alloc(
        16, STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_8BIT);
    output.buffers[1] = (uint16_t *)heap_caps_aligned_alloc(
        16, STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_8BIT);
    if (output.buffers[0] == NULL || output.buffers[1] == NULL) {
        finish(3);
    }

    while (frames < MJPEG_QEMU_FRAME_LIMIT) {
        mjpeg_avi_packet_t packet = {0};
        uint64_t callback_before;
        uint32_t start;
        int decode_result;
        uint32_t elapsed;
        const mjpeg_avi_decode_cycles_t *phases;
        if (mjpeg_avi_decoder_read_packet(
                &decoder, file, &packet) != MJPEG_AVI_OK) {
            finish(6);
        }
        if (packet.jpeg_size > maximum_packet) {
            maximum_packet = (uint32_t)packet.jpeg_size;
        }
        callback_before = output.callback_cycles;
        start = esp_cpu_get_cycle_count();
        decode_result = mjpeg_avi_decoder_decode_direct(
            &decoder, &packet, acquire_strip, submit_strip, &output);
        if (decode_result != MJPEG_AVI_OK) {
            finish(8);
        }
        elapsed = esp_cpu_get_cycle_count() - start;
        frame_cycles[frames++] = elapsed;
        total_cycles += elapsed;
        phases = mjpeg_avi_decoder_last_decode_cycles(&decoder);
        total_header_cycles += phases->parse_header;
        total_geometry_cycles += phases->geometry;
        total_process_cycles += phases->process;
        total_refills += decoder.entropy_stream.refill_count;
        total_refill_bytes += decoder.entropy_stream.refill_bytes;
        total_callback_cycles +=
            output.callback_cycles - callback_before;
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
        uint32_t average = (uint32_t)(total_cycles / frames);
        uint32_t p50 = frame_cycles[(frames - 1U) / 2U];
        uint32_t p95 =
            frame_cycles[((frames * 95U + 99U) / 100U) - 1U];
        uint32_t maximum = frame_cycles[frames - 1U];
        uint32_t header_average =
            (uint32_t)(total_header_cycles / frames);
        uint32_t geometry_average =
            (uint32_t)(total_geometry_cycles / frames);
        uint32_t process_average =
            (uint32_t)(total_process_cycles / frames);
        uint32_t callback_average =
            (uint32_t)(total_callback_cycles / frames);
        uint32_t decoder_average =
            average > callback_average
                ? average - callback_average
                : 0U;
        uint64_t fps_milli =
            (uint64_t)TARGET_CPU_HZ * frames * 1000U /
            total_cycles;
        esp_rom_printf(
            "#J,frames,avg,p50,p95,max,fps_milli,hash,heap,largest,"
            "decoder_avg,header_avg,geometry_avg,process_avg,"
            "callback_avg,input_buffer,max_packet,refills,refill_bytes\n");
        esp_rom_printf(
            "J,%u,%u,%u,%u,%u,%u,%08x%08x,%u,%u,%u,%u,%u,%u,%u,"
            "%u,%u,%u,%u\n",
            (unsigned)frames, (unsigned)average, (unsigned)p50,
            (unsigned)p95, (unsigned)maximum, (unsigned)fps_milli,
            (unsigned)(output.hash >> 32), (unsigned)output.hash,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(
                MALLOC_CAP_8BIT),
            (unsigned)decoder_average, (unsigned)header_average,
            (unsigned)geometry_average, (unsigned)process_average,
            (unsigned)callback_average,
            (unsigned)mjpeg_avi_decoder_input_buffer_bytes(&decoder),
            (unsigned)maximum_packet, (unsigned)total_refills,
            (unsigned)total_refill_bytes);
    }
    ESP_LOGI(k_tag, "esp_new_jpeg block benchmark complete");
    finish(0);
}
