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

static int test_wav_block_and_streaming_equivalence(void) {
    enum { block_bytes = 1024, refill_bytes = 127 };
    enum { sample_count = 1 + (block_bytes - 4) * 2 };
    int16_t input[sample_count];
    int16_t contiguous[sample_count];
    int16_t streamed[sample_count];
    uint8_t encoded[block_bytes] = {0};
    IMAADPCMState encode_state = {0, 0};
    IMAADPCMState stream_state;
    size_t decoded_count = 0;
    size_t sample;
    size_t encoded_offset = IMA_ADPCM_WAV_BLOCK_HEADER_SIZE;
    size_t output_offset = 1;

    for (sample = 0; sample < sample_count; ++sample) {
        input[sample] = (int16_t)(sin(
            2.0 * 3.14159265358979323846 * 440.0 *
            (double)sample / 32000.0) * 24000.0);
    }
    encode_state.predictor = input[0];
    encoded[0] = (uint8_t)encode_state.predictor;
    encoded[1] = (uint8_t)((uint16_t)(int16_t)encode_state.predictor >> 8);
    encoded[2] = (uint8_t)encode_state.step_index;
    encoded[3] = 0;
    for (sample = 1; sample < sample_count; ++sample) {
        const uint8_t code = ima_adpcm_encode_sample(&encode_state,
                                                     input[sample]);
        const size_t byte = IMA_ADPCM_WAV_BLOCK_HEADER_SIZE +
                            (sample - 1U) / 2U;
        if (sample & 1U) encoded[byte] = code;
        else encoded[byte] = (uint8_t)(encoded[byte] | (code << 4));
    }

    CHECK(ima_adpcm_wav_mono_sample_count(block_bytes) == sample_count);
    CHECK(block_bytes > 512U);
    CHECK(!ima_adpcm_decode_wav_mono_block(
        encoded, sizeof encoded, contiguous, sample_count, &decoded_count));
    CHECK(decoded_count == sample_count);

    CHECK(!ima_adpcm_wav_block_header_read(
        encoded, IMA_ADPCM_WAV_BLOCK_HEADER_SIZE, &stream_state));
    streamed[0] = (int16_t)stream_state.predictor;
    while (encoded_offset < sizeof encoded) {
        const size_t refill_end = encoded_offset + refill_bytes < sizeof encoded
            ? encoded_offset + refill_bytes : sizeof encoded;
        while (encoded_offset < refill_end) {
            const uint8_t packed = encoded[encoded_offset++];
            streamed[output_offset++] = ima_adpcm_decode_nibble(
                &stream_state, packed & 15U);
            streamed[output_offset++] = ima_adpcm_decode_nibble(
                &stream_state, packed >> 4);
        }
    }
    CHECK(output_offset == sample_count);
    CHECK(checksum_samples(streamed, sample_count) ==
          checksum_samples(contiguous, sample_count));
    return 0;
}

static int test_malformed_wav_blocks(void) {
    uint8_t encoded[8] = {0};
    int16_t output[9];
    size_t count = 0;
    CHECK(ima_adpcm_wav_mono_sample_count(3) == 0);
    CHECK(ima_adpcm_wav_mono_sample_count(
        IMA_ADPCM_WAV_MAX_BLOCK_BYTES + 1U) == 0);
    CHECK(!ima_adpcm_decode_wav_mono_block(
        encoded, sizeof encoded, output, 9, &count));
    encoded[2] = 89;
    CHECK(ima_adpcm_decode_wav_mono_block(
        encoded, sizeof encoded, output, 9, &count) != 0);
    encoded[2] = 0;
    encoded[3] = 1;
    CHECK(ima_adpcm_decode_wav_mono_block(
        encoded, sizeof encoded, output, 9, &count) != 0);
    return 0;
}

int main(void) {
    CHECK(!test_silence());
    CHECK(!test_large_block_and_streaming_equivalence());
    CHECK(!test_malformed_blocks());
    CHECK(!test_wav_block_and_streaming_equivalence());
    CHECK(!test_malformed_wav_blocks());
    puts("IMA ADPCM tests passed");
    return 0;
}
