#define BPV1_WITH_CUDA 1
#include "bpv1_cuda.h"

#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

enum {
    BLOCK_SIZE = 4,
    PIXELS_PER_BLOCK = 16,
    PALETTE_COUNT = 64,
    COLORS_PER_PALETTE = 16,
    LOCAL_COLORS = 4,
    PALETTE_INDEX_BIN_COUNT = 4096,
    DIRECT_RECORD_FLAG = 0x80
};

struct Bpv1CudaContext {
    int width;
    int height;
    int chroma_width;
    int blocks_x;
    int blocks_y;
    int block_count;
    int candidate_palettes;
    size_t frame_bytes;
    size_t result_count;
    uint8_t *frame;
    uint8_t *pixels;
    uint8_t *palette;
    uint32_t *palette_index;
    uint8_t *palette_candidates;
    uint8_t *candidate_blocks;
    uint64_t *candidate_errors;
    cudaStream_t stream;
};

static void set_error(
    char *error,
    size_t capacity,
    const char *operation,
    cudaError_t result
) {
    if (!error || !capacity) return;
    std::snprintf(
        error,
        capacity,
        "%s: %s",
        operation,
        cudaGetErrorString(result)
    );
}

static int check_cuda(
    cudaError_t result,
    const char *operation,
    char *error,
    size_t capacity
) {
    if (result == cudaSuccess) return 0;
    set_error(error, capacity, operation, result);
    return -1;
}

__device__ static uint8_t clamp_byte_device(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<uint8_t>(value);
}

__device__ static void yuv_to_rgb_device(
    uint8_t y,
    uint8_t u,
    uint8_t v,
    uint8_t *rgb
) {
    int c = static_cast<int>(y) - 16;
    int d = static_cast<int>(u) - 128;
    int e = static_cast<int>(v) - 128;
    if (c < 0) c = 0;
    rgb[0] = clamp_byte_device((298 * c + 409 * e + 128) >> 8);
    rgb[1] = clamp_byte_device((298 * c - 100 * d - 208 * e + 128) >> 8);
    rgb[2] = clamp_byte_device((298 * c + 516 * d + 128) >> 8);
}

__device__ static uint32_t color_distance_device(
    const uint8_t *left,
    const uint8_t *right
) {
    int red = static_cast<int>(left[0]) - right[0];
    int green = static_cast<int>(left[1]) - right[1];
    int blue = static_cast<int>(left[2]) - right[2];
    return static_cast<uint32_t>(
        red * red + green * green + blue * blue
    );
}

__global__ static void prepare_blocks_kernel(
    const uint8_t *frame,
    int width,
    int height,
    int chroma_width,
    int blocks_x,
    int block_count,
    int candidate_count,
    const uint32_t *palette_index,
    uint8_t *pixels,
    uint8_t *palette_candidates
) {
    int block_index = blockIdx.x * blockDim.x + threadIdx.x;
    uint64_t best_scores[PALETTE_COUNT];
    int best_indices[PALETTE_COUNT];
    int block_x;
    int block_y;
    int local_y;
    int local_x;
    int target = 0;
    const size_t luma_bytes =
        static_cast<size_t>(width) * static_cast<size_t>(height);
    const int chroma_height = (height + 1) / 2;
    const size_t chroma_bytes =
        static_cast<size_t>(chroma_width) *
        static_cast<size_t>(chroma_height);
    const uint8_t *y_plane;
    const uint8_t *u_plane;
    const uint8_t *v_plane;
    uint8_t *block_pixels;
    int palette;
    int slot;
    if (block_index >= block_count) return;
    y_plane = frame;
    u_plane = frame + luma_bytes;
    v_plane = u_plane + chroma_bytes;
    block_x = block_index % blocks_x;
    block_y = block_index / blocks_x;
    block_pixels =
        pixels + static_cast<size_t>(block_index) *
                     PIXELS_PER_BLOCK * 3;
    for (local_y = 0; local_y < BLOCK_SIZE; ++local_y) {
        int y = block_y * BLOCK_SIZE + local_y;
        if (y >= height) y = height - 1;
        for (local_x = 0; local_x < BLOCK_SIZE; ++local_x) {
            int x = block_x * BLOCK_SIZE + local_x;
            size_t chroma;
            if (x >= width) x = width - 1;
            chroma =
                static_cast<size_t>(y / 2) *
                    static_cast<size_t>(chroma_width) +
                static_cast<size_t>(x / 2);
            yuv_to_rgb_device(
                y_plane[static_cast<size_t>(y) *
                            static_cast<size_t>(width) +
                        static_cast<size_t>(x)],
                u_plane[chroma],
                v_plane[chroma],
                block_pixels + target * 3
            );
            ++target;
        }
    }
    for (slot = 0; slot < candidate_count; ++slot) {
        best_scores[slot] = UINT64_MAX;
        best_indices[slot] = 0;
    }
    for (palette = 0; palette < PALETTE_COUNT; ++palette) {
        uint64_t score = 0;
        int pixel;
        for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
            const uint8_t *rgb = block_pixels + pixel * 3;
            const unsigned bin =
                (static_cast<unsigned>(rgb[0] >> 4) << 8) |
                (static_cast<unsigned>(rgb[1] >> 4) << 4) |
                static_cast<unsigned>(rgb[2] >> 4);
            score += palette_index[
                static_cast<size_t>(bin) * PALETTE_COUNT + palette
            ];
        }
        for (slot = 0; slot < candidate_count; ++slot) {
            if (score < best_scores[slot] ||
                (score == best_scores[slot] &&
                 palette < best_indices[slot])) {
                int move;
                for (move = candidate_count - 1;
                     move > slot;
                     --move) {
                    best_scores[move] = best_scores[move - 1];
                    best_indices[move] = best_indices[move - 1];
                }
                best_scores[slot] = score;
                best_indices[slot] = palette;
                break;
            }
        }
    }
    for (slot = 0; slot < candidate_count; ++slot) {
        palette_candidates[
            static_cast<size_t>(block_index) * candidate_count + slot
        ] = static_cast<uint8_t>(best_indices[slot]);
    }
}

__device__ static unsigned direct_color(
    const uint8_t *block,
    unsigned pixel
) {
    const uint8_t value = block[1 + (pixel >> 1)];
    return (pixel & 1U) ? value & 15U : value >> 4;
}

__device__ static void direct_color_store(
    uint8_t *block,
    unsigned pixel,
    unsigned color
) {
    uint8_t *target = block + 1 + (pixel >> 1);
    if (pixel & 1U)
        *target = static_cast<uint8_t>((*target & 0xf0U) | color);
    else
        *target = static_cast<uint8_t>((*target & 0x0fU) | (color << 4));
}

__device__ static void canonicalize_device(uint8_t *block) {
    if (block[0] & DIRECT_RECORD_FLAG) {
        uint16_t mask = 0;
        unsigned pixel;
        unsigned count = 0;
        for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel)
            mask |= static_cast<uint16_t>(1U << direct_color(block, pixel));
        count = static_cast<unsigned>(__popc(static_cast<unsigned>(mask)));
        if (count <= LOCAL_COLORS) {
            uint8_t compact[BPV1_CUDA_RECORD_BYTES] = {0};
            uint8_t direct_to_local[COLORS_PER_PALETTE] = {0};
            unsigned color;
            compact[0] = static_cast<uint8_t>(block[0] & 0x3fU);
            count = 0;
            for (color = 0; color < COLORS_PER_PALETTE; ++color) {
                if (!(mask & (1U << color))) continue;
                direct_to_local[color] = static_cast<uint8_t>(count);
                compact[1 + count++] = static_cast<uint8_t>(color);
            }
            for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
                const unsigned local =
                    direct_to_local[direct_color(block, pixel)];
                compact[5 + (pixel >> 2)] |= static_cast<uint8_t>(
                    local << (6 - ((pixel & 3U) * 2))
                );
            }
            for (unsigned byte = 0;
                 byte < BPV1_CUDA_RECORD_BYTES;
                 ++byte) {
                block[byte] = compact[byte];
            }
        }
        return;
    }
    {
        uint8_t used[LOCAL_COLORS] = {0};
        uint8_t mapping[LOCAL_COLORS] = {0};
        uint8_t colors[LOCAL_COLORS] = {0};
        uint8_t pattern[4] = {0};
        unsigned count = 0;
        int pixel;
        int slot;
        for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
            const int shift = 6 - ((pixel & 3) * 2);
            used[(block[5 + (pixel >> 2)] >> shift) & 3U] = 1;
        }
        for (slot = 0; slot < LOCAL_COLORS; ++slot) {
            if (!used[slot]) continue;
            mapping[slot] = static_cast<uint8_t>(count);
            colors[count++] = block[1 + slot];
        }
        for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
            const int shift = 6 - ((pixel & 3) * 2);
            const unsigned source =
                (block[5 + (pixel >> 2)] >> shift) & 3U;
            pattern[pixel >> 2] |= static_cast<uint8_t>(
                mapping[source] << shift
            );
        }
        for (slot = 0; slot < LOCAL_COLORS; ++slot)
            block[1 + slot] = colors[slot];
        for (slot = 0; slot < 4; ++slot)
            block[5 + slot] = pattern[slot];
    }
}

__global__ static void quantize_candidates_kernel(
    const uint8_t *pixels,
    const uint8_t *palette,
    const uint8_t *palette_candidates,
    int candidate_palettes,
    size_t result_count,
    uint8_t *candidate_blocks,
    uint64_t *candidate_errors
) {
    size_t result_index =
        static_cast<size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    size_t grouped;
    int color_class;
    int palette_slot;
    size_t block_index;
    int color_limit;
    int palette_index;
    const uint8_t *block_pixels;
    uint32_t distances[PIXELS_PER_BLOCK][COLORS_PER_PALETTE];
    uint32_t current[PIXELS_PER_BLOCK];
    int selected[COLORS_PER_PALETTE];
    uint8_t block[BPV1_CUDA_RECORD_BYTES] = {0};
    uint64_t total_error = 0;
    int pixel;
    int color;
    int slot;
    if (result_index >= result_count) return;
    color_class = static_cast<int>(
        result_index % BPV1_CUDA_COLOR_CLASSES
    );
    grouped = result_index / BPV1_CUDA_COLOR_CLASSES;
    palette_slot = static_cast<int>(grouped % candidate_palettes);
    block_index = grouped / candidate_palettes;
    color_limit = color_class == 0 ? 4 :
                  color_class == 1 ? 8 : COLORS_PER_PALETTE;
    palette_index = palette_candidates[
        block_index * candidate_palettes + palette_slot
    ];
    block_pixels =
        pixels + block_index * PIXELS_PER_BLOCK * 3;
    for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
        current[pixel] = UINT32_MAX;
        for (color = 0; color < COLORS_PER_PALETTE; ++color) {
            distances[pixel][color] = color_distance_device(
                block_pixels + pixel * 3,
                palette +
                    (static_cast<size_t>(palette_index) *
                         COLORS_PER_PALETTE +
                     color) *
                        3
            );
        }
    }
    for (slot = 0; slot < color_limit; ++slot) {
        int best_color = 0;
        uint64_t best_error = UINT64_MAX;
        for (color = 0; color < COLORS_PER_PALETTE; ++color) {
            uint64_t error = 0;
            int used = 0;
            int previous;
            for (previous = 0; previous < slot; ++previous)
                if (selected[previous] == color) used = 1;
            if (used) continue;
            for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
                const uint32_t distance = distances[pixel][color];
                error += distance < current[pixel]
                    ? distance
                    : current[pixel];
            }
            if (error < best_error) {
                best_error = error;
                best_color = color;
            }
        }
        selected[slot] = best_color;
        for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel)
            if (distances[pixel][best_color] < current[pixel])
                current[pixel] = distances[pixel][best_color];
    }
    for (slot = 0; slot < color_limit - 1; ++slot) {
        int other;
        for (other = slot + 1; other < color_limit; ++other) {
            if (selected[other] < selected[slot]) {
                const int temporary = selected[slot];
                selected[slot] = selected[other];
                selected[other] = temporary;
            }
        }
    }
    block[0] = static_cast<uint8_t>(
        palette_index |
        (color_limit > LOCAL_COLORS ? DIRECT_RECORD_FLAG : 0)
    );
    if (color_limit == LOCAL_COLORS) {
        for (slot = 0; slot < LOCAL_COLORS; ++slot)
            block[1 + slot] = static_cast<uint8_t>(selected[slot]);
    }
    for (pixel = 0; pixel < PIXELS_PER_BLOCK; ++pixel) {
        int best_slot = 0;
        uint32_t best_distance = distances[pixel][selected[0]];
        for (slot = 1; slot < color_limit; ++slot) {
            const uint32_t distance = distances[pixel][selected[slot]];
            if (distance < best_distance) {
                best_distance = distance;
                best_slot = slot;
            }
        }
        if (color_limit > LOCAL_COLORS) {
            direct_color_store(
                block,
                static_cast<unsigned>(pixel),
                static_cast<unsigned>(selected[best_slot])
            );
        } else {
            block[5 + (pixel >> 2)] |= static_cast<uint8_t>(
                best_slot << (6 - ((pixel & 3) * 2))
            );
        }
        total_error += best_distance;
    }
    canonicalize_device(block);
    for (int byte = 0; byte < BPV1_CUDA_RECORD_BYTES; ++byte) {
        candidate_blocks[
            result_index * BPV1_CUDA_RECORD_BYTES + byte
        ] = block[byte];
    }
    candidate_errors[result_index] = total_error;
}

extern "C" int bpv1_cuda_runtime_available(
    char *error,
    size_t error_capacity
) {
    int count = 0;
    cudaError_t result = cudaGetDeviceCount(&count);
    if (result != cudaSuccess) {
        set_error(error, error_capacity, "cudaGetDeviceCount", result);
        return 0;
    }
    if (count <= 0) {
        if (error && error_capacity)
            std::snprintf(error, error_capacity, "no CUDA device found");
        return 0;
    }
    if (error && error_capacity) error[0] = '\0';
    return 1;
}

extern "C" Bpv1CudaContext *bpv1_cuda_create(
    int width,
    int height,
    int chroma_width,
    int candidate_palettes,
    size_t frame_bytes,
    char *error,
    size_t error_capacity
) {
    Bpv1CudaContext *context = nullptr;
    size_t pixel_bytes;
    size_t palette_candidate_bytes;
    size_t candidate_block_bytes;
    size_t candidate_error_bytes;
    if (width <= 0 || height <= 0 || chroma_width <= 0 ||
        candidate_palettes <= 0 ||
        candidate_palettes > PALETTE_COUNT) {
        if (error && error_capacity)
            std::snprintf(error, error_capacity, "invalid CUDA dimensions");
        return nullptr;
    }
    context = static_cast<Bpv1CudaContext *>(
        std::calloc(1, sizeof(*context))
    );
    if (!context) {
        if (error && error_capacity)
            std::snprintf(error, error_capacity, "out of host memory");
        return nullptr;
    }
    context->width = width;
    context->height = height;
    context->chroma_width = chroma_width;
    context->blocks_x = (width + BLOCK_SIZE - 1) / BLOCK_SIZE;
    context->blocks_y = (height + BLOCK_SIZE - 1) / BLOCK_SIZE;
    context->block_count = context->blocks_x * context->blocks_y;
    context->candidate_palettes = candidate_palettes;
    context->frame_bytes = frame_bytes;
    context->result_count =
        static_cast<size_t>(context->block_count) *
        static_cast<size_t>(candidate_palettes) *
        BPV1_CUDA_COLOR_CLASSES;
    pixel_bytes =
        static_cast<size_t>(context->block_count) *
        PIXELS_PER_BLOCK * 3;
    palette_candidate_bytes =
        static_cast<size_t>(context->block_count) *
        static_cast<size_t>(candidate_palettes);
    candidate_block_bytes =
        context->result_count * BPV1_CUDA_RECORD_BYTES;
    candidate_error_bytes =
        context->result_count * sizeof(uint64_t);
    if (check_cuda(
            cudaStreamCreateWithFlags(
                &context->stream,
                cudaStreamNonBlocking
            ),
            "cudaStreamCreate",
            error,
            error_capacity
        ) ||
        check_cuda(
            cudaMalloc(
                reinterpret_cast<void **>(&context->frame),
                frame_bytes
            ),
            "cudaMalloc(frame)",
            error,
            error_capacity
        ) ||
        check_cuda(
            cudaMalloc(
                reinterpret_cast<void **>(&context->pixels),
                pixel_bytes
            ),
            "cudaMalloc(pixels)",
            error,
            error_capacity
        ) ||
        check_cuda(
            cudaMalloc(
                reinterpret_cast<void **>(&context->palette),
                PALETTE_COUNT * COLORS_PER_PALETTE * 3
            ),
            "cudaMalloc(palette)",
            error,
            error_capacity
        ) ||
        check_cuda(
            cudaMalloc(
                reinterpret_cast<void **>(&context->palette_index),
                static_cast<size_t>(PALETTE_INDEX_BIN_COUNT) *
                    PALETTE_COUNT * sizeof(uint32_t)
            ),
            "cudaMalloc(palette index)",
            error,
            error_capacity
        ) ||
        check_cuda(
            cudaMalloc(
                reinterpret_cast<void **>(&context->palette_candidates),
                palette_candidate_bytes
            ),
            "cudaMalloc(palette candidates)",
            error,
            error_capacity
        ) ||
        check_cuda(
            cudaMalloc(
                reinterpret_cast<void **>(&context->candidate_blocks),
                candidate_block_bytes
            ),
            "cudaMalloc(candidate blocks)",
            error,
            error_capacity
        ) ||
        check_cuda(
            cudaMalloc(
                reinterpret_cast<void **>(&context->candidate_errors),
                candidate_error_bytes
            ),
            "cudaMalloc(candidate errors)",
            error,
            error_capacity
        )) {
        bpv1_cuda_destroy(context);
        return nullptr;
    }
    if (error && error_capacity) error[0] = '\0';
    return context;
}

extern "C" int bpv1_cuda_set_palette(
    Bpv1CudaContext *context,
    const uint8_t *palette,
    size_t palette_bytes,
    const uint32_t *palette_index,
    size_t palette_index_entries,
    char *error,
    size_t error_capacity
) {
    const size_t expected_palette =
        PALETTE_COUNT * COLORS_PER_PALETTE * 3;
    const size_t expected_index =
        static_cast<size_t>(PALETTE_INDEX_BIN_COUNT) * PALETTE_COUNT;
    if (!context || !palette || !palette_index ||
        palette_bytes != expected_palette ||
        palette_index_entries != expected_index) {
        if (error && error_capacity)
            std::snprintf(error, error_capacity, "invalid palette data");
        return -1;
    }
    if (check_cuda(
            cudaMemcpyAsync(
                context->palette,
                palette,
                palette_bytes,
                cudaMemcpyHostToDevice,
                context->stream
            ),
            "cudaMemcpy(palette)",
            error,
            error_capacity
        ) ||
        check_cuda(
            cudaMemcpyAsync(
                context->palette_index,
                palette_index,
                palette_index_entries * sizeof(uint32_t),
                cudaMemcpyHostToDevice,
                context->stream
            ),
            "cudaMemcpy(palette index)",
            error,
            error_capacity
        ) ||
        check_cuda(
            cudaStreamSynchronize(context->stream),
            "cudaStreamSynchronize(palette)",
            error,
            error_capacity
        )) {
        return -1;
    }
    if (error && error_capacity) error[0] = '\0';
    return 0;
}

extern "C" int bpv1_cuda_encode_frame(
    Bpv1CudaContext *context,
    const uint8_t *frame,
    size_t frame_bytes,
    uint8_t *candidate_blocks,
    size_t candidate_block_bytes,
    uint64_t *candidate_errors,
    size_t candidate_error_count,
    char *error,
    size_t error_capacity
) {
    const int threads = 128;
    int block_grid;
    int result_grid;
    size_t expected_block_bytes;
    if (!context || !frame || !candidate_blocks || !candidate_errors) {
        if (error && error_capacity)
            std::snprintf(error, error_capacity, "null CUDA argument");
        return -1;
    }
    expected_block_bytes =
        context->result_count * BPV1_CUDA_RECORD_BYTES;
    if (frame_bytes != context->frame_bytes ||
        candidate_block_bytes != expected_block_bytes ||
        candidate_error_count != context->result_count) {
        if (error && error_capacity)
            std::snprintf(error, error_capacity, "invalid CUDA buffer size");
        return -1;
    }
    if (check_cuda(
            cudaMemcpyAsync(
                context->frame,
                frame,
                frame_bytes,
                cudaMemcpyHostToDevice,
                context->stream
            ),
            "cudaMemcpy(frame)",
            error,
            error_capacity
        )) {
        return -1;
    }
    block_grid = (context->block_count + threads - 1) / threads;
    prepare_blocks_kernel<<<block_grid, threads, 0, context->stream>>>(
        context->frame,
        context->width,
        context->height,
        context->chroma_width,
        context->blocks_x,
        context->block_count,
        context->candidate_palettes,
        context->palette_index,
        context->pixels,
        context->palette_candidates
    );
    if (check_cuda(
            cudaPeekAtLastError(),
            "prepare_blocks_kernel",
            error,
            error_capacity
        )) {
        return -1;
    }
    result_grid = static_cast<int>(
        (context->result_count + threads - 1) / threads
    );
    quantize_candidates_kernel<<<
        result_grid,
        threads,
        0,
        context->stream
    >>>(
        context->pixels,
        context->palette,
        context->palette_candidates,
        context->candidate_palettes,
        context->result_count,
        context->candidate_blocks,
        context->candidate_errors
    );
    if (check_cuda(
            cudaPeekAtLastError(),
            "quantize_candidates_kernel",
            error,
            error_capacity
        ) ||
        check_cuda(
            cudaMemcpyAsync(
                candidate_blocks,
                context->candidate_blocks,
                expected_block_bytes,
                cudaMemcpyDeviceToHost,
                context->stream
            ),
            "cudaMemcpy(candidate blocks)",
            error,
            error_capacity
        ) ||
        check_cuda(
            cudaMemcpyAsync(
                candidate_errors,
                context->candidate_errors,
                context->result_count * sizeof(uint64_t),
                cudaMemcpyDeviceToHost,
                context->stream
            ),
            "cudaMemcpy(candidate errors)",
            error,
            error_capacity
        ) ||
        check_cuda(
            cudaStreamSynchronize(context->stream),
            "cudaStreamSynchronize(frame)",
            error,
            error_capacity
        )) {
        return -1;
    }
    if (error && error_capacity) error[0] = '\0';
    return 0;
}

extern "C" void bpv1_cuda_destroy(Bpv1CudaContext *context) {
    if (!context) return;
    cudaFree(context->candidate_errors);
    cudaFree(context->candidate_blocks);
    cudaFree(context->palette_candidates);
    cudaFree(context->palette_index);
    cudaFree(context->palette);
    cudaFree(context->pixels);
    cudaFree(context->frame);
    if (context->stream) cudaStreamDestroy(context->stream);
    std::free(context);
}
