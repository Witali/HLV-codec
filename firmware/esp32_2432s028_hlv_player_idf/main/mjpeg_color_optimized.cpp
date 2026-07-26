#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_attr.h"

namespace {

using ColorFunction = void (*)(
    int16_t *, int16_t *, int16_t *, int16_t *, uint8_t *);

constexpr size_t kYuv420Rgb565LeIndex = 13;
constexpr int kOutputStride = 320;
constexpr int kBlockWidth = 16;
constexpr int kBlockHeight = 16;

extern "C" uint8_t lips[];
extern "C" void *mjpeg_original_color_array[16];

#ifdef MJPEG_COLOR_TABLES
constexpr uint32_t packContribution(int green_product, int color) {
    return (static_cast<uint32_t>(green_product) & 0xfffffU) |
           ((static_cast<uint32_t>(color) & 0xfffU) << 20);
}

constexpr std::array<uint32_t, 256> makeUContributions() {
    std::array<uint32_t, 256> result{};
    for (int value = 0; value < 256; ++value) {
        const int delta = value - 128;
        result[value] = packContribution(
            delta * 0x581, (delta * 0x1c5b) >> 12);
    }
    return result;
}

constexpr std::array<uint32_t, 256> makeVContributions() {
    std::array<uint32_t, 256> result{};
    for (int value = 0; value < 256; ++value) {
        const int delta = value - 128;
        result[value] = packContribution(
            delta * 0xb6d, (delta * 0x166f) >> 12);
    }
    return result;
}

constexpr auto kUContributions = makeUContributions();
constexpr auto kVContributions = makeVContributions();

inline __attribute__((always_inline)) int signExtend20(uint32_t value) {
    return static_cast<int32_t>(value << 12) >> 12;
}
#endif

inline __attribute__((always_inline)) uint16_t packRgb565(
    uint16_t y, int red, int green, int blue) {
    const uint8_t r = lips[(y + red) & 0x3ff];
    const uint8_t g = lips[(y - green) & 0x3ff];
    const uint8_t b = lips[(y + blue) & 0x3ff];
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(r & 0xf8) << 8) |
        (static_cast<uint16_t>(g & 0xfc) << 3) |
        (b >> 3));
}

template <int Group>
inline __attribute__((always_inline)) void convertGroup(
    const int16_t *y0, const int16_t *y1,
    const int16_t *u, const int16_t *v,
    uint16_t *out0, uint16_t *out1) {
#ifdef MJPEG_COLOR_TABLES
    const uint32_t u_contribution =
        kUContributions[static_cast<uint8_t>(u[Group])];
    const uint32_t v_contribution =
        kVContributions[static_cast<uint8_t>(v[Group])];
    const int red = static_cast<int32_t>(v_contribution) >> 20;
    const int green =
        (signExtend20(u_contribution) +
         signExtend20(v_contribution)) >> 12;
    const int blue = static_cast<int32_t>(u_contribution) >> 20;
#else
    const int u_delta = static_cast<int>(u[Group]) - 128;
    const int v_delta = static_cast<int>(v[Group]) - 128;
    const int red = (v_delta * 0x166f) >> 12;
    const int green =
        (u_delta * 0x581 + v_delta * 0xb6d) >> 12;
    const int blue = (u_delta * 0x1c5b) >> 12;
#endif
    constexpr int x = Group * 2;
    out0[x] = packRgb565(
        static_cast<uint16_t>(y0[x]), red, green, blue);
    out0[x + 1] = packRgb565(
        static_cast<uint16_t>(y0[x + 1]), red, green, blue);
    out1[x] = packRgb565(
        static_cast<uint16_t>(y1[x]), red, green, blue);
    out1[x + 1] = packRgb565(
        static_cast<uint16_t>(y1[x + 1]), red, green, blue);
}

void IRAM_ATTR fixedYuv420ToRgb565Le(
    int16_t *y, int16_t *u, int16_t *v,
    int16_t *w_h, uint8_t *rgb_out) {
    if (w_h[0] != kOutputStride ||
        w_h[2] != kBlockWidth ||
        w_h[3] != kBlockHeight) {
        reinterpret_cast<ColorFunction>(
            mjpeg_original_color_array[kYuv420Rgb565LeIndex])(
                y, u, v, w_h, rgb_out);
        return;
    }

    auto *output = reinterpret_cast<uint16_t *>(rgb_out);
    for (int row = 0; row < 8; ++row) {
        const int16_t *y0 = y + row * 32;
        const int16_t *y1 = y0 + 16;
        const int16_t *row_u = u + row * 8;
        const int16_t *row_v = v + row * 8;
        uint16_t *out0 = output + row * (kOutputStride * 2);
        uint16_t *out1 = out0 + kOutputStride;
        convertGroup<0>(y0, y1, row_u, row_v, out0, out1);
        convertGroup<1>(y0, y1, row_u, row_v, out0, out1);
        convertGroup<2>(y0, y1, row_u, row_v, out0, out1);
        convertGroup<3>(y0, y1, row_u, row_v, out0, out1);
        convertGroup<4>(y0, y1, row_u, row_v, out0, out1);
        convertGroup<5>(y0, y1, row_u, row_v, out0, out1);
        convertGroup<6>(y0, y1, row_u, row_v, out0, out1);
        convertGroup<7>(y0, y1, row_u, row_v, out0, out1);
    }
}

}  // namespace

extern "C" {
void *color_array[16] = {};
}

extern "C" void mjpeg_install_fixed_rgb565() {
    for (size_t index = 0; index < 16; ++index) {
        color_array[index] = mjpeg_original_color_array[index];
    }
    color_array[kYuv420Rgb565LeIndex] =
        reinterpret_cast<void *>(&fixedYuv420ToRgb565Le);
}
