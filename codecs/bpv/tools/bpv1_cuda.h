#ifndef BPV1_CUDA_H
#define BPV1_CUDA_H

#include <stddef.h>
#include <stdint.h>

#define BPV1_CUDA_RECORD_BYTES 9
#define BPV1_CUDA_COLOR_CLASSES 3

typedef struct Bpv1CudaContext Bpv1CudaContext;

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BPV1_WITH_CUDA

int bpv1_cuda_runtime_available(char *error, size_t error_capacity);

Bpv1CudaContext *bpv1_cuda_create(
    int width,
    int height,
    int chroma_width,
    int candidate_palettes,
    size_t frame_bytes,
    char *error,
    size_t error_capacity
);

int bpv1_cuda_set_palette(
    Bpv1CudaContext *context,
    const uint8_t *palette,
    size_t palette_bytes,
    const uint32_t *palette_index,
    size_t palette_index_entries,
    char *error,
    size_t error_capacity
);

int bpv1_cuda_encode_frame(
    Bpv1CudaContext *context,
    const uint8_t *frame,
    size_t frame_bytes,
    uint8_t *candidate_blocks,
    size_t candidate_block_bytes,
    uint64_t *candidate_errors,
    size_t candidate_error_count,
    char *error,
    size_t error_capacity
);

void bpv1_cuda_destroy(Bpv1CudaContext *context);

#else

static int bpv1_cuda_runtime_available(
    char *error,
    size_t error_capacity
) {
    static const char message[] = "CUDA support is not compiled in";
    size_t index;
    if (error && error_capacity) {
        for (index = 0;
             index + 1 < error_capacity && message[index];
             ++index) {
            error[index] = message[index];
        }
        error[index] = '\0';
    }
    return 0;
}

static void bpv1_cuda_destroy(Bpv1CudaContext *context) {
    (void)context;
}

#endif

#ifdef __cplusplus
}
#endif

#endif
