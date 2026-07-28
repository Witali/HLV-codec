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

#define BENCH_WIDTH 320U
#define BENCH_HEIGHT 240U
#define ROWS_PER_STRIP 16U
#define FRAME_LIMIT 60U
#define TARGET_CPU_HZ 240000000U

static const char *const k_tag = "bpv-qemu-bench";

static __attribute__((noreturn)) void finish(int code) {
    fflush(stdout);
    esp_rom_printf("BPV_BENCH_DONE,%d\n", code);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

static uint64_t hash_pixels(uint64_t hash,
                            const uint16_t *pixels,
                            size_t count) {
    size_t i;
    for (i = 0; i < count; ++i) {
        hash ^= (uint8_t)pixels[i];
        hash *= UINT64_C(1099511628211);
        hash ^= (uint8_t)(pixels[i] >> 8);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void prepare_frame(BPV1Header *header,
                          BPV1Frame *frame,
                          uint8_t *blocks) {
    uint16_t blocks_x =
        (BENCH_WIDTH + BPV1_BLOCK_SIZE - 1U) / BPV1_BLOCK_SIZE;
    uint16_t blocks_y =
        (BENCH_HEIGHT + BPV1_BLOCK_SIZE - 1U) / BPV1_BLOCK_SIZE;
    size_t i;
    uint16_t by;

    header->version = BPV1_VERSION;
    header->width = BENCH_WIDTH;
    header->height = BENCH_HEIGHT;
    header->palette_count = BPV1_PALETTE_COUNT;
    for (i = 0; i < BPV1_MAX_PALETTE_BYTES; ++i) {
        header->palette[i] =
            (uint8_t)((i * 73U + i / 11U * 19U) & 0xffU);
    }

    for (by = 0; by < blocks_y; ++by) {
        uint16_t bx;
        for (bx = 0; bx < blocks_x; ++bx) {
            size_t block_index = (size_t)by * blocks_x + bx;
            uint8_t *record =
                blocks + block_index * BPV1_RECORD_BYTES;
            unsigned color;
            unsigned row;
            record[0] = (uint8_t)(
                (bx * 13U + by * 7U) % BPV1_PALETTE_COUNT);
            for (color = 0; color < 4U; ++color) {
                record[1U + color] = (uint8_t)(
                    (bx * 3U + by * 5U + color * 7U) %
                    BPV1_COLORS_PER_PALETTE);
            }
            for (row = 0; row < BPV1_PATTERN_BYTES; ++row) {
                record[5U + row] = (uint8_t)(
                    (bx * 29U + by * 17U + row * 57U) & 0xffU);
            }
        }
    }

    frame->width = BENCH_WIDTH;
    frame->height = BENCH_HEIGHT;
    frame->blocks_x = blocks_x;
    frame->blocks_y = blocks_y;
    frame->block_count = (uint32_t)blocks_x * blocks_y;
    frame->keyframe = 1;
    frame->blocks = blocks;
    frame->palette = header->palette;
}

void app_main(void) {
    size_t block_count =
        (size_t)(BENCH_WIDTH / BPV1_BLOCK_SIZE) *
        (BENCH_HEIGHT / BPV1_BLOCK_SIZE);
    uint8_t *blocks =
        (uint8_t *)malloc(block_count * BPV1_RECORD_BYTES);
    uint16_t *strip = (uint16_t *)malloc(
        (size_t)BENCH_WIDTH * ROWS_PER_STRIP * sizeof(uint16_t));
    uint16_t *palette_rgb565 = (uint16_t *)malloc(
        BPV1_MAX_PALETTE_COLORS * sizeof(uint16_t));
    BPV1Header *header =
        (BPV1Header *)calloc(1, sizeof(BPV1Header));
    BPV1Frame frame = {0};
    uint64_t render_cycles = 0;
    uint64_t frame_hash = UINT64_C(1469598103934665603);
    uint32_t frame_cycles[FRAME_LIMIT] = {0};
    uint32_t frame_index;

    if (blocks == NULL || strip == NULL ||
        palette_rgb565 == NULL || header == NULL) {
        finish(1);
    }

    prepare_frame(header, &frame, blocks);
    if (bpv1_palette_build_rgb565(
            header, &frame, palette_rgb565,
            BPV1_MAX_PALETTE_COLORS) != BPV1_OK) {
        finish(2);
    }
    ESP_LOGI(k_tag, "Xtensa BPV RGB565 benchmark, %ux%u",
             BENCH_WIDTH, BENCH_HEIGHT);

    for (frame_index = 0; frame_index < FRAME_LIMIT;
         ++frame_index) {
        uint32_t start = esp_cpu_get_cycle_count();
        uint16_t y;
        uint32_t elapsed;
        for (y = 0; y < BENCH_HEIGHT; y += ROWS_PER_STRIP) {
            uint16_t rows = (uint16_t)(
                BENCH_HEIGHT - y < ROWS_PER_STRIP
                    ? BENCH_HEIGHT - y
                    : ROWS_PER_STRIP);
            if (bpv1_frame_render_rgb565_rows_cached(
                    header, &frame, y, rows, palette_rgb565,
                    BPV1_MAX_PALETTE_COLORS, strip, BENCH_WIDTH,
                    (size_t)BENCH_WIDTH * rows) != BPV1_OK) {
                finish(3);
            }
        }
        elapsed = esp_cpu_get_cycle_count() - start;
        render_cycles += elapsed;
        frame_cycles[frame_index] = elapsed;
        for (y = 0; y < BENCH_HEIGHT; y += ROWS_PER_STRIP) {
            uint16_t rows = (uint16_t)(
                BENCH_HEIGHT - y < ROWS_PER_STRIP
                    ? BENCH_HEIGHT - y
                    : ROWS_PER_STRIP);
            if (bpv1_frame_render_rgb565_rows_cached(
                    header, &frame, y, rows, palette_rgb565,
                    BPV1_MAX_PALETTE_COLORS, strip, BENCH_WIDTH,
                    (size_t)BENCH_WIDTH * rows) != BPV1_OK) {
                finish(4);
            }
            frame_hash = hash_pixels(
                frame_hash, strip, (size_t)BENCH_WIDTH * rows);
        }
    }

    {
        uint32_t i;
        for (i = 1; i < FRAME_LIMIT; ++i) {
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
        uint32_t average =
            (uint32_t)(render_cycles / FRAME_LIMIT);
        uint32_t p50 = frame_cycles[(FRAME_LIMIT - 1U) / 2U];
        uint32_t p95 =
            frame_cycles[((FRAME_LIMIT * 95U + 99U) / 100U) - 1U];
        uint32_t maximum = frame_cycles[FRAME_LIMIT - 1U];
        uint64_t fps_milli =
            (uint64_t)TARGET_CPU_HZ * FRAME_LIMIT * 1000U /
            render_cycles;
        esp_rom_printf(
            "#P,frames,avg,p50,p95,max,fps_milli,hash,heap,largest\n");
        esp_rom_printf(
            "P,%u,%u,%u,%u,%u,%u,%08x%08x,%u,%u\n",
            (unsigned)FRAME_LIMIT, (unsigned)average,
            (unsigned)p50, (unsigned)p95, (unsigned)maximum,
            (unsigned)fps_milli,
            (unsigned)(frame_hash >> 32), (unsigned)frame_hash,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(
                MALLOC_CAP_8BIT));
    }
    finish(0);
}
