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

#define FRAME_LIMIT 90U
#define TARGET_CPU_HZ 240000000U

#ifdef MPEG4_QEMU_BENCHMARK
static const char *const k_tag = "mpeg4-qemu-bench";
extern const uint8_t k_video_start[]
    asm("_binary_qemu_mpeg4_benchmark_avi_start");
extern const uint8_t k_video_end[]
    asm("_binary_qemu_mpeg4_benchmark_avi_end");
#define BENCH_DONE "MPEG4_BENCH_DONE"
#define BENCH_HEADER "#M"
#define BENCH_ROW "M"
#define BENCH_CONTAINER "AVI"
#define BENCH_WIDTH 320U
#define BENCH_HEIGHT 240U
#define BENCH_OUTPUT_BUFFERS 1U
#else
static const char *const k_tag = "h263-qemu-bench";
extern const uint8_t k_video_start[]
    asm("_binary_qemu_h263_benchmark_3gp_start");
extern const uint8_t k_video_end[]
    asm("_binary_qemu_h263_benchmark_3gp_end");
#define BENCH_DONE "H263_BENCH_DONE"
#define BENCH_HEADER "#H"
#define BENCH_ROW "H"
#define BENCH_CONTAINER "3GP"
#define BENCH_WIDTH 352U
#define BENCH_HEIGHT 288U
#define BENCH_OUTPUT_BUFFERS 1U
#endif

static uint64_t hash_plane(uint64_t hash,
                           const uint8_t *plane,
                           unsigned stride,
                           unsigned width,
                           unsigned height) {
    unsigned y;
    for (y = 0; y < height; ++y) {
        const uint8_t *row = plane + (size_t)y * stride;
        unsigned x;
        for (x = 0; x < width; ++x) {
            hash ^= row[x];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

static uint64_t hash_compact_plane(
    uint64_t hash, const CompactYuv420Plane *plane) {
    uint8_t unpacked[352];
    int y;
    if (plane->width > (int)(sizeof(unpacked))) return 0;
    for (y = 0; y < plane->height; ++y) {
        const uint8_t *row =
            plane->data + (size_t)y * plane->stride;
        compact_yuv420_unpack_corrected_samples(
            row, 0, y, plane->bits, plane->correction,
            plane->correction_stride, unpacked, plane->width);
        int x;
        for (x = 0; x < plane->width; ++x) {
            hash ^= unpacked[x];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

static uint64_t hash_frame(uint64_t hash,
                           const H2633gpFrame *frame) {
    unsigned chroma_width = (frame->width + 1U) / 2U;
    unsigned chroma_height = (frame->height + 1U) / 2U;
    if (frame->storage_mode == H263_FRAME_STORAGE_Y6_U5_V5) {
        hash = hash_compact_plane(hash, &frame->compact.y);
        hash = hash_compact_plane(hash, &frame->compact.u);
        return hash_compact_plane(hash, &frame->compact.v);
    }
    hash = hash_plane(hash, frame->y, frame->y_stride,
                      frame->width, frame->height);
    hash = hash_plane(hash, frame->u, frame->chroma_stride,
                      chroma_width, chroma_height);
    return hash_plane(hash, frame->v, frame->chroma_stride,
                      chroma_width, chroma_height);
}

static __attribute__((noreturn)) void finish(int code) {
    fflush(stdout);
    esp_rom_printf(BENCH_DONE ",%d\n", code);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_restart();
    __builtin_unreachable();
}

void app_main(void) {
    size_t video_size = (size_t)(k_video_end - k_video_start);
    FILE *file;
    H2633gpDecoder *decoder;
    H2633gpInfo info = {0};
    int result;
    uint64_t decode_cycles = 0;
    uint64_t frame_hash = UINT64_C(1469598103934665603);
    uint32_t frame_cycles[FRAME_LIMIT] = {0};
    uint32_t frames = 0;

    ESP_LOGI(k_tag, "Xtensa decoder benchmark, " BENCH_CONTAINER
             "=%u bytes",
             (unsigned)video_size);
    file = fmemopen((void *)k_video_start, video_size, "rb");
    if (file == NULL) {
        finish(1);
    }
    decoder = h263_3gp_decoder_create();
    if (decoder == NULL ||
        h263_3gp_decoder_set_output_buffer_count(
            decoder, BENCH_OUTPUT_BUFFERS) !=
            H263_3GP_OK) {
        finish(2);
    }
    result = h263_3gp_decoder_open(decoder, file, &info);
    if (result != H263_3GP_OK) {
        ESP_LOGE(k_tag, "Open failed: %s",
                 h263_3gp_strerror(result));
        finish(3);
    }
    if (info.width != BENCH_WIDTH || info.height != BENCH_HEIGHT
#ifdef MPEG4_QEMU_BENCHMARK
        || info.video_codec != H263_VIDEO_CODEC_MPEG4_SIMPLE
#endif
    ) {
        finish(4);
    }

    while (frames < FRAME_LIMIT) {
        H2633gpFrame frame = {0};
        uint32_t start = esp_cpu_get_cycle_count();
        uint32_t elapsed;
        result =
            h263_3gp_decoder_decode_next(decoder, file, &frame);
        elapsed = esp_cpu_get_cycle_count() - start;
        if (result == H263_3GP_EOF) {
            break;
        }
        if (result != H263_3GP_OK) {
            ESP_LOGE(k_tag, "Frame %" PRIu32 " failed: %s",
                     frames, h263_3gp_strerror(result));
            finish(5);
        }
        decode_cycles += elapsed;
        frame_cycles[frames] = elapsed;
        frame_hash = hash_frame(frame_hash, &frame);
        ++frames;
    }
    if (frames == 0U || decode_cycles == 0U) {
        finish(6);
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
            BENCH_HEADER ",frames,avg,p50,p95,max,fps_milli,hash,decoder,"
            "heap,largest\n");
        esp_rom_printf(
            BENCH_ROW ",%u,%u,%u,%u,%u,%u,%08x%08x,%u,%u,%u\n",
            (unsigned)frames, (unsigned)average,
            (unsigned)p50, (unsigned)p95, (unsigned)maximum,
            (unsigned)fps_milli,
            (unsigned)(frame_hash >> 32), (unsigned)frame_hash,
            (unsigned)h263_3gp_decoder_memory_bytes(decoder),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(
                MALLOC_CAP_8BIT));
    }
    finish(0);
}
