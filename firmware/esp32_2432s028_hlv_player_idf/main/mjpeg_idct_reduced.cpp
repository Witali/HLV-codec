#include <cstdint>

#include "esp_attr.h"

extern "C" const uint8_t lips[];

namespace {

struct IdctVector {
    int16_t value[8];
};

inline __attribute__((always_inline)) int32_t shift8(int32_t value) {
    return value >> 8;
}

inline __attribute__((always_inline)) IdctVector transformColumn(
    const int16_t *coefficients,
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

    const int32_t even0 = x0 + x4;
    const int32_t even1 = x0 - x4;
    const int32_t even2 = x2 + x6;
    const int32_t even3 = shift8((x2 - x6) * 0x16a) - even2;
    const int32_t a = even0 + even2;
    const int32_t b = even0 - even2;
    const int32_t c = even3 + even1;
    const int32_t d = even1 - even3;

    const int32_t odd0 = x1 - x7;
    const int32_t odd1 = x1 + x7;
    const int32_t odd2 = x5 - x3;
    const int32_t odd3 = x3 + x5;
    const int32_t sum = odd3 + odd1;
    const int32_t p = shift8((odd0 + odd2) * 0x1d9);
    const int32_t q = p - shift8(odd2 * 0x29d) - sum;
    const int32_t r = shift8((odd1 - odd3) * 0x16a) - q;
    const int32_t s = p - shift8(odd0 * 0x115) - r;

    return IdctVector{{
        static_cast<int16_t>(a + sum),
        static_cast<int16_t>(c + q),
        static_cast<int16_t>(d + r),
        static_cast<int16_t>(b + s),
        static_cast<int16_t>(b - s),
        static_cast<int16_t>(d - r),
        static_cast<int16_t>(c - q),
        static_cast<int16_t>(a - sum),
    }};
}

inline __attribute__((always_inline)) uint16_t clipSample(int32_t value) {
    return lips[(static_cast<uint32_t>(value) >> 5) & 0x3ffU];
}

inline __attribute__((always_inline)) uint32_t samplePair(
    uint16_t first,
    uint16_t second) {
    return static_cast<uint32_t>(first) |
        (static_cast<uint32_t>(second) << 16);
}

inline __attribute__((always_inline)) void storeRow(
    uint16_t *destination,
    uint16_t value0,
    uint16_t value1,
    uint16_t value2,
    uint16_t value3,
    uint16_t value4,
    uint16_t value5,
    uint16_t value6,
    uint16_t value7) {
    auto *pairs = reinterpret_cast<uint32_t *>(destination);
    pairs[0] = samplePair(value0, value1);
    pairs[1] = samplePair(value2, value3);
    pairs[2] = samplePair(value4, value5);
    pairs[3] = samplePair(value6, value7);
}

}  // namespace

extern "C" void IRAM_ATTR mjpeg_idct_reduced_8x8(
    const int16_t *coefficients,
    const int16_t *quantization,
    uint16_t *destination,
    int stride,
    int activeColumns) {
    const IdctVector column0 =
        transformColumn(coefficients, quantization, 0);

    if (activeColumns == 1) {
        for (int row = 0; row < 8; ++row) {
            const uint16_t sample =
                clipSample(static_cast<int32_t>(column0.value[row]) + 0x1000);
            storeRow(
                destination, sample, sample, sample, sample,
                sample, sample, sample, sample);
            destination += stride;
        }
        return;
    }

    const IdctVector column1 =
        transformColumn(coefficients, quantization, 1);
    for (int row = 0; row < 8; ++row) {
        const int32_t x0 =
            static_cast<int32_t>(column0.value[row]) + 0x1000;
        const int32_t x1 = column1.value[row];
        const int32_t p = shift8(x1 * 0x1d9);
        const int32_t q = p - x1;
        const int32_t r = shift8(x1 * 0x16a) - q;
        const int32_t s = p - shift8(x1 * 0x115) - r;

        storeRow(
            destination,
            clipSample(x0 + x1),
            clipSample(x0 + q),
            clipSample(x0 + r),
            clipSample(x0 + s),
            clipSample(x0 - s),
            clipSample(x0 - r),
            clipSample(x0 - q),
            clipSample(x0 - x1));
        destination += stride;
    }
}
