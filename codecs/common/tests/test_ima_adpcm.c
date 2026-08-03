#include "ima_adpcm.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "check failed at line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static uint32_t checksum_samples(const int16_t *samples, size_t count) {
    uint32_t hash = 2166136261U;
    size_t sample;
    for (sample = 0; sample < count; ++sample) {
        const uint16_t value = (uint16_t)samples[sample];
        hash = (hash ^ (uint8_t)value) * 16777619U;
        hash = (hash ^ (uint8_t)(value >> 8)) * 16777619U;
    }
    return hash;
}

static int test_silence(void) {
    int16_t input[33] = {0};
    int16_t output[33];
    uint8_t encoded[32];
    size_t encoded_size = 0;
    size_t output_count = 0;
    CHECK(ima_adpcm_block_size(33) == 22U);
    CHECK(!ima_adpcm_encode_block(input, 33, encoded, sizeof encoded,
                                  &encoded_size));
    CHECK(encoded_size == 22U);
    CHECK(!ima_adpcm_decode_block(encoded, encoded_size, output, 33,
                                  &output_count));
    CHECK(output_count == 33U);
    CHECK(!memcmp(input, output, sizeof input));
    return 0;
}

static int test_large_block_and_streaming_equivalence(void) {
    enum { sample_count = 1333, refill_bytes = 127 };
    int16_t input[sample_count];
    int16_t contiguous[sample_count];
    int16_t streamed[sample_count];
    uint8_t encoded[IMA_ADPCM_BLOCK_HEADER_SIZE + sample_count / 2];
    size_t encoded_size = 0;
    size_t decoded_count = 0;
    IMAADPCMState state;
    uint16_t header_count = 0;
    size_t encoded_offset;
    size_t output_offset = 1;
    int nibble_high = 0;
    uint8_t packed = 0;
    size_t sample;
    int64_t squared_error = 0;

    for (sample = 0; sample < sample_count; ++sample) {
        const double phase = 2.0 * 3.14159265358979323846 *
                             997.0 * (double)sample / 32000.0;
        input[sample] = (int16_t)(sin(phase) * 28000.0);
    }
    CHECK(!ima_adpcm_encode_block(input, sample_count, encoded,
                                  sizeof encoded, &encoded_size));
    CHECK(encoded_size > 512U);
    CHECK(!ima_adpcm_decode_block(encoded, encoded_size, contiguous,
                                  sample_count, &decoded_count));
    CHECK(decoded_count == sample_count);

    CHECK(!ima_adpcm_block_header_read(encoded, refill_bytes, &state,
                                        &header_count));
    CHECK(header_count == sample_count);
    streamed[0] = (int16_t)state.predictor;
    encoded_offset = IMA_ADPCM_BLOCK_HEADER_SIZE;
    while (output_offset < sample_count) {
        const size_t refill_end = encoded_offset + refill_bytes < encoded_size
            ? encoded_offset + refill_bytes : encoded_size;
        while (encoded_offset < refill_end && output_offset < sample_count) {
            uint8_t code;
            if (!nibble_high) {
                packed = encoded[encoded_offset];
                code = packed & 15U;
                nibble_high = 1;
            } else {
                code = packed >> 4;
                nibble_high = 0;
                ++encoded_offset;
            }
            streamed[output_offset++] =
                ima_adpcm_decode_nibble(&state, code);
        }
    }
    CHECK(checksum_samples(streamed, sample_count) ==
          checksum_samples(contiguous, sample_count));
    for (sample = 0; sample < sample_count; ++sample) {
        const int difference = (int)input[sample] - contiguous[sample];
        squared_error += (int64_t)difference * difference;
    }
    CHECK(squared_error / sample_count < 2000000);
    return 0;
}

static int test_malformed_blocks(void) {
    uint8_t encoded[16] = {0};
    int16_t output[8];
    IMAADPCMState state = {0, 0};
    size_t count = 0;
    CHECK(ima_adpcm_block_size(0) == 0);
    CHECK(ima_adpcm_block_size(IMA_ADPCM_MAX_BLOCK_SAMPLES + 1U) == 0);
    CHECK(ima_adpcm_block_header_write(encoded, sizeof encoded, &state, 8) == 0);
    CHECK(ima_adpcm_decode_block(encoded, 9, output, 8, &count) != 0);
    encoded[2] = 89;
    CHECK(ima_adpcm_decode_block(encoded, sizeof encoded, output, 8,
                                 &count) != 0);
    encoded[2] = 0;
    encoded[3] = 1;
    CHECK(ima_adpcm_decode_block(encoded, sizeof encoded, output, 8,
                                 &count) != 0);
    return 0;
}

int main(void) {
    CHECK(!test_silence());
    CHECK(!test_large_block_and_streaming_equivalence());
    CHECK(!test_malformed_blocks());
    puts("IMA ADPCM tests passed");
    return 0;
}
