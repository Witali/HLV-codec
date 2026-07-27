#include <stdint.h>

#include "esp_attr.h"

extern const uint8_t lips[];

typedef struct {
    int16_t value[8];
} idct_vector_t;

static inline __attribute__((always_inline)) int32_t shift8(
    int32_t value) {
    return value >> 8;
}

static inline __attribute__((always_inline)) idct_vector_t
transform_column(const int16_t *coefficients,
                 const int16_t *quantization,
                 int column) {
    int32_t x0 = coefficients[column] * quantization[column];
    int32_t x1 = coefficients[column + 8] * quantization[column + 8];
    int32_t x2 = coefficients[column + 16] * quantization[column + 16];
    int32_t x3 = coefficients[column + 24] * quantization[column + 24];
    int32_t x4 = coefficients[column + 32] * quantization[column + 32];
    int32_t x5 = coefficients[column + 40] * quantization[column + 40];
    int32_t x6 = coefficients[column + 48] * quantization[column + 48];
    int32_t x7 = coefficients[column + 56] * quantization[column + 56];
    int32_t even0 = x0 + x4;
    int32_t even1 = x0 - x4;
    int32_t even2 = x2 + x6;
    int32_t even3 = shift8((x2 - x6) * 0x16a) - even2;
    int32_t a = even0 + even2;
    int32_t b = even0 - even2;
    int32_t c = even3 + even1;
    int32_t d = even1 - even3;
    int32_t odd0 = x1 - x7;
    int32_t odd1 = x1 + x7;
    int32_t odd2 = x5 - x3;
    int32_t odd3 = x3 + x5;
    int32_t sum = odd3 + odd1;
    int32_t p = shift8((odd0 + odd2) * 0x1d9);
    int32_t q = p - shift8(odd2 * 0x29d) - sum;
    int32_t r = shift8((odd1 - odd3) * 0x16a) - q;
    int32_t s = p - shift8(odd0 * 0x115) - r;
    idct_vector_t result = {{
        (int16_t)(a + sum),
        (int16_t)(c + q),
        (int16_t)(d + r),
        (int16_t)(b + s),
        (int16_t)(b - s),
        (int16_t)(d - r),
        (int16_t)(c - q),
        (int16_t)(a - sum),
    }};
    return result;
}

static inline __attribute__((always_inline)) uint16_t clip_sample(
    int32_t value) {
    return lips[((uint32_t)value >> 5) & 0x3ffU];
}

static inline __attribute__((always_inline)) uint32_t sample_pair(
    uint16_t first,
    uint16_t second) {
    return (uint32_t)first | ((uint32_t)second << 16);
}

static inline __attribute__((always_inline)) void store_row(
    uint16_t *destination,
    uint16_t value0,
    uint16_t value1,
    uint16_t value2,
    uint16_t value3,
    uint16_t value4,
    uint16_t value5,
    uint16_t value6,
    uint16_t value7) {
    uint32_t *pairs = (uint32_t *)destination;
    pairs[0] = sample_pair(value0, value1);
    pairs[1] = sample_pair(value2, value3);
    pairs[2] = sample_pair(value4, value5);
    pairs[3] = sample_pair(value6, value7);
}

void IRAM_ATTR mjpeg_idct_reduced_8x8(
    const int16_t *coefficients,
    const int16_t *quantization,
    uint16_t *destination,
    int stride,
    int active_columns) {
    idct_vector_t column0 =
        transform_column(coefficients, quantization, 0);

    if (active_columns == 1) {
        int row;
        for (row = 0; row < 8; ++row) {
            uint16_t sample =
                clip_sample((int32_t)column0.value[row] + 0x1000);
            store_row(destination, sample, sample, sample, sample,
                      sample, sample, sample, sample);
            destination += stride;
        }
        return;
    }

    {
        idct_vector_t column1 =
            transform_column(coefficients, quantization, 1);
        int row;
        for (row = 0; row < 8; ++row) {
            int32_t x0 =
                (int32_t)column0.value[row] + 0x1000;
            int32_t x1 = column1.value[row];
            int32_t p = shift8(x1 * 0x1d9);
            int32_t q = p - x1;
            int32_t r = shift8(x1 * 0x16a) - q;
            int32_t s = p - shift8(x1 * 0x115) - r;

            store_row(
                destination,
                clip_sample(x0 + x1),
                clip_sample(x0 + q),
                clip_sample(x0 + r),
                clip_sample(x0 + s),
                clip_sample(x0 - s),
                clip_sample(x0 - r),
                clip_sample(x0 - q),
                clip_sample(x0 - x1));
            destination += stride;
        }
    }
}
