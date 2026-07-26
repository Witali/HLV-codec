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

#include "bpv1.h"

namespace {

constexpr char kTag[] = "bpv-qemu-bench";
constexpr uint16_t kWidth = 320;
constexpr uint16_t kHeight = 240;
constexpr uint16_t kRowsPerStrip = 16;
constexpr uint32_t kFrameLimit = 60;
constexpr uint32_t kTargetCpuHz = 240000000;

[[noreturn]] void finish(int code) {
    fflush(stdout);
    esp_rom_printf("BPV_BENCH_DONE,%d\n", code);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

uint64_t hashPixels(uint64_t hash, const uint16_t *pixels, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        hash ^= static_cast<uint8_t>(pixels[i]);
        hash *= UINT64_C(1099511628211);
        hash ^= static_cast<uint8_t>(pixels[i] >> 8);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

void prepareFrame(BPV1Header *header, BPV1Frame *frame,
                  uint8_t *blocks) {
    header->version = BPV1_VERSION;
    header->width = kWidth;
    header->height = kHeight;
    header->palette_count = BPV1_PALETTE_COUNT;
    for (size_t i = 0; i < BPV1_MAX_PALETTE_BYTES; ++i) {
        header->palette[i] =
            static_cast<uint8_t>((i * 73U + i / 11U * 19U) & 0xffU);
    }

    const uint16_t blocks_x =
        (kWidth + BPV1_BLOCK_SIZE - 1U) / BPV1_BLOCK_SIZE;
    const uint16_t blocks_y =
        (kHeight + BPV1_BLOCK_SIZE - 1U) / BPV1_BLOCK_SIZE;
    for (uint16_t by = 0; by < blocks_y; ++by) {
        for (uint16_t bx = 0; bx < blocks_x; ++bx) {
            const size_t block_index =
                static_cast<size_t>(by) * blocks_x + bx;
            uint8_t *record =
                blocks + block_index * BPV1_RECORD_BYTES;
            record[0] = static_cast<uint8_t>(
                (bx * 13U + by * 7U) % BPV1_PALETTE_COUNT);
            for (unsigned color = 0; color < 4; ++color) {
                record[1U + color] = static_cast<uint8_t>(
                    (bx * 3U + by * 5U + color * 7U) %
                    BPV1_COLORS_PER_PALETTE);
            }
            for (unsigned row = 0; row < BPV1_PATTERN_BYTES; ++row) {
                record[5U + row] = static_cast<uint8_t>(
                    (bx * 29U + by * 17U + row * 57U) & 0xffU);
            }
        }
    }

    frame->width = kWidth;
    frame->height = kHeight;
    frame->blocks_x = blocks_x;
    frame->blocks_y = blocks_y;
    frame->block_count =
        static_cast<uint32_t>(blocks_x) * blocks_y;
    frame->keyframe = 1;
    frame->blocks = blocks;
    frame->palette = header->palette;
}

}  // namespace

extern "C" void app_main(void) {
    const size_t block_count =
        static_cast<size_t>(kWidth / BPV1_BLOCK_SIZE) *
        (kHeight / BPV1_BLOCK_SIZE);
    uint8_t *blocks = static_cast<uint8_t *>(
        malloc(block_count * BPV1_RECORD_BYTES));
    uint16_t *strip = static_cast<uint16_t *>(
        malloc(static_cast<size_t>(kWidth) * kRowsPerStrip *
               sizeof(uint16_t)));
    uint16_t *palette_rgb565 = static_cast<uint16_t *>(
        malloc(BPV1_MAX_PALETTE_COLORS * sizeof(uint16_t)));
    BPV1Header *header =
        static_cast<BPV1Header *>(calloc(1, sizeof(BPV1Header)));
    if (!blocks || !strip || !palette_rgb565 || !header) finish(1);

    BPV1Frame frame{};
    prepareFrame(header, &frame, blocks);
    if (bpv1_palette_build_rgb565(
            header, &frame, palette_rgb565,
            BPV1_MAX_PALETTE_COLORS) != BPV1_OK) {
        finish(2);
    }
    ESP_LOGI(kTag, "Xtensa BPV RGB565 benchmark, %ux%u",
             kWidth, kHeight);

    uint64_t render_cycles = 0;
    uint64_t frame_hash = UINT64_C(1469598103934665603);
    uint32_t frame_cycles[kFrameLimit]{};
    for (uint32_t frame_index = 0;
         frame_index < kFrameLimit; ++frame_index) {
        const uint32_t start = esp_cpu_get_cycle_count();
        for (uint16_t y = 0; y < kHeight; y += kRowsPerStrip) {
            const uint16_t rows = static_cast<uint16_t>(
                kHeight - y < kRowsPerStrip
                    ? kHeight - y
                    : kRowsPerStrip);
            if (bpv1_frame_render_rgb565_rows_cached(
                    header, &frame, y, rows, palette_rgb565,
                    BPV1_MAX_PALETTE_COLORS, strip, kWidth,
                    static_cast<size_t>(kWidth) * rows) != BPV1_OK) {
                finish(3);
            }
        }
        const uint32_t elapsed = esp_cpu_get_cycle_count() - start;
        render_cycles += elapsed;
        frame_cycles[frame_index] = elapsed;
        for (uint16_t y = 0; y < kHeight; y += kRowsPerStrip) {
            const uint16_t rows = static_cast<uint16_t>(
                kHeight - y < kRowsPerStrip
                    ? kHeight - y
                    : kRowsPerStrip);
            if (bpv1_frame_render_rgb565_rows_cached(
                    header, &frame, y, rows, palette_rgb565,
                    BPV1_MAX_PALETTE_COLORS, strip, kWidth,
                    static_cast<size_t>(kWidth) * rows) != BPV1_OK) {
                finish(4);
            }
            frame_hash = hashPixels(
                frame_hash, strip,
                static_cast<size_t>(kWidth) * rows);
        }
    }

    for (uint32_t i = 1; i < kFrameLimit; ++i) {
        const uint32_t value = frame_cycles[i];
        uint32_t j = i;
        while (j && frame_cycles[j - 1] > value) {
            frame_cycles[j] = frame_cycles[j - 1];
            --j;
        }
        frame_cycles[j] = value;
    }
    const uint32_t average =
        static_cast<uint32_t>(render_cycles / kFrameLimit);
    const uint32_t p50 =
        frame_cycles[(kFrameLimit - 1U) / 2U];
    const uint32_t p95 =
        frame_cycles[((kFrameLimit * 95U + 99U) / 100U) - 1U];
    const uint32_t maximum = frame_cycles[kFrameLimit - 1U];
    const uint64_t fps_milli =
        static_cast<uint64_t>(kTargetCpuHz) * kFrameLimit * 1000U /
        render_cycles;
    esp_rom_printf(
        "#P,frames,avg,p50,p95,max,fps_milli,hash,heap,largest\n");
    esp_rom_printf(
        "P,%u,%u,%u,%u,%u,%u,%08x%08x,%u,%u\n",
        static_cast<unsigned>(kFrameLimit),
        static_cast<unsigned>(average), static_cast<unsigned>(p50),
        static_cast<unsigned>(p95), static_cast<unsigned>(maximum),
        static_cast<unsigned>(fps_milli),
        static_cast<unsigned>(frame_hash >> 32),
        static_cast<unsigned>(frame_hash),
        static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_8BIT)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    finish(0);
}
