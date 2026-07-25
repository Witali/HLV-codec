#include "compact_yuv420.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message, int line) {
    fprintf(stderr, "compact_yuv420 test failed at line %d: %s\n",
            line, message);
    return 1;
}

#define CHECK(condition, message) \
    do { if (!(condition)) return fail((message), __LINE__); } while (0)

static int test_layout(void) {
    CHECK(compact_yuv420_packed_stride(320, 6) == 240,
          "QVGA luma stride");
    CHECK(compact_yuv420_packed_stride(160, 5) == 100,
          "QVGA chroma stride");
    CHECK(compact_yuv420_plane_correction_bytes(320, 240) == 1200,
          "QVGA luma correction bytes");
    CHECK(compact_yuv420_plane_correction_bytes(160, 120) == 300,
          "QVGA chroma correction bytes");
    CHECK(compact_yuv420_frame_storage_bytes(320, 240) == 83400,
          "QVGA compact frame bytes");
    return 0;
}

static int test_round_trip(unsigned bits) {
    uint8_t source[256];
    uint8_t reconstructed[256];
    uint8_t packed[256];
    uint8_t unpacked[256];
    int error_sum = 0;
    int i;
    for (i = 0; i < 256; ++i) source[i] = (uint8_t)i;
    compact_yuv420_pack_aligned_samples(
        packed, source, 256, bits, &error_sum, reconstructed);
    compact_yuv420_unpack_packed_samples(
        packed, 0, bits, unpacked, 256);
    CHECK(memcmp(reconstructed, unpacked, sizeof unpacked) == 0,
          "packed samples round trip");
    for (i = 0; i < 256; ++i) {
        unsigned code = compact_yuv420_quantize_code((uint8_t)i, bits);
        CHECK(unpacked[i] == (uint8_t)(code << (8U - bits)),
              "quantization matches packed output");
    }
    return 0;
}

static int test_correction(void) {
    int8_t correction[1];
    int q4;
    for (q4 = -64; q4 <= 48; ++q4) {
        int sum = 0;
        int y;
        int x;
        correction[0] = (int8_t)q4;
        for (y = 0; y < 8; ++y)
            for (x = 0; x < 8; ++x)
                sum += compact_yuv420_correction(
                    correction, 1, x, y);
        CHECK(sum == q4 * 4,
              "threshold map preserves the Q4 block average");
    }
    CHECK(compact_yuv420_error_q4(192) == 48,
          "positive correction limit");
    CHECK(compact_yuv420_error_q4(-256) == -64,
          "negative correction limit");
    return 0;
}

static int test_corrected_unpack(void) {
    uint8_t source[8] = {1, 7, 11, 63, 127, 191, 251, 255};
    uint8_t packed[6];
    uint8_t unpacked[8];
    int8_t correction[1];
    int error_sum = 0;
    int corrected_sum = 0;
    int source_sum = 0;
    int i;
    compact_yuv420_pack_aligned_samples(
        packed, source, 8, 6, &error_sum, NULL);
    correction[0] = compact_yuv420_error_q4(error_sum * 8);
    compact_yuv420_unpack_corrected_samples(
        packed, 0, 0, 6, correction, 1, unpacked, 8);
    for (i = 0; i < 8; ++i) {
        corrected_sum += unpacked[i];
        source_sum += source[i];
    }
    CHECK(corrected_sum >= source_sum - 8 &&
              corrected_sum <= source_sum + 8,
          "corrected row remains near the original average");
    return 0;
}

int main(void) {
    if (test_layout()) return 1;
    if (test_round_trip(6)) return 1;
    if (test_round_trip(5)) return 1;
    if (test_correction()) return 1;
    if (test_corrected_unpack()) return 1;
    puts("compact_yuv420 tests passed");
    return 0;
}
