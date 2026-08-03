#include "ima_adpcm.h"

#include <limits.h>

static const int ima_index_delta[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const int ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static int clamp_predictor(int value) {
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return value;
}

static int clamp_index(int value) {
    if (value < 0) return 0;
    if (value > 88) return 88;
    return value;
}

size_t ima_adpcm_block_size(size_t sample_count) {
    if (!sample_count || sample_count > IMA_ADPCM_MAX_BLOCK_SAMPLES)
        return 0;
    return IMA_ADPCM_BLOCK_HEADER_SIZE + sample_count / 2U;
}

int ima_adpcm_block_header_read(const uint8_t *header, size_t header_size,
                                IMAADPCMState *state,
                                uint16_t *sample_count) {
    uint16_t count;
    if (!header || header_size < IMA_ADPCM_BLOCK_HEADER_SIZE ||
        !state || !sample_count || header[3] != 0 || header[2] > 88U) {
        return -1;
    }
    count = (uint16_t)((uint16_t)header[4] |
                       ((uint16_t)header[5] << 8));
    if (!count || count > IMA_ADPCM_MAX_BLOCK_SAMPLES) return -1;
    state->predictor = (int16_t)((uint16_t)header[0] |
                                 ((uint16_t)header[1] << 8));
    state->step_index = header[2];
    *sample_count = count;
    return 0;
}

int ima_adpcm_block_header_write(uint8_t *header, size_t header_size,
                                 const IMAADPCMState *state,
                                 uint16_t sample_count) {
    uint16_t predictor;
    if (!header || header_size < IMA_ADPCM_BLOCK_HEADER_SIZE || !state ||
        state->predictor < INT16_MIN || state->predictor > INT16_MAX ||
        state->step_index < 0 || state->step_index > 88 || !sample_count ||
        sample_count > IMA_ADPCM_MAX_BLOCK_SAMPLES) {
        return -1;
    }
    predictor = (uint16_t)(int16_t)state->predictor;
    header[0] = (uint8_t)predictor;
    header[1] = (uint8_t)(predictor >> 8);
    header[2] = (uint8_t)state->step_index;
    header[3] = 0;
    header[4] = (uint8_t)sample_count;
    header[5] = (uint8_t)(sample_count >> 8);
    return 0;
}

uint8_t ima_adpcm_encode_sample(IMAADPCMState *state, int16_t sample) {
    int delta;
    int difference;
    int code = 0;
    int step;
    int reconstructed;
    if (!state) return 0;
    state->step_index = clamp_index(state->step_index);
    step = ima_step[state->step_index];
    delta = (int)sample - state->predictor;
    if (delta < 0) {
        code = 8;
        difference = -delta;
    } else {
        difference = delta;
    }
    reconstructed = step >> 3;
    if (difference >= step) {
        code |= 4;
        difference -= step;
        reconstructed += step;
    }
    step >>= 1;
    if (difference >= step) {
        code |= 2;
        difference -= step;
        reconstructed += step;
    }
    step >>= 1;
    if (difference >= step) {
        code |= 1;
        reconstructed += step;
    }
    if (code & 8) reconstructed = -reconstructed;
    state->predictor = clamp_predictor(state->predictor + reconstructed);
    state->step_index = clamp_index(
        state->step_index + ima_index_delta[code]);
    return (uint8_t)code;
}

int16_t ima_adpcm_decode_nibble(IMAADPCMState *state, uint8_t code) {
    int difference;
    int step;
    if (!state) return 0;
    code &= 15U;
    state->step_index = clamp_index(state->step_index);
    step = ima_step[state->step_index];
    difference = step >> 3;
    if (code & 4U) difference += step;
    if (code & 2U) difference += step >> 1;
    if (code & 1U) difference += step >> 2;
    if (code & 8U) difference = -difference;
    state->predictor = clamp_predictor(state->predictor + difference);
    state->step_index = clamp_index(
        state->step_index + ima_index_delta[code]);
    return (int16_t)state->predictor;
}

static int initial_step_index(const int16_t *samples, size_t sample_count) {
    unsigned difference;
    unsigned target;
    int index = 0;
    if (sample_count < 2U) return 0;
    difference = samples[1] >= samples[0]
        ? (unsigned)((int)samples[1] - samples[0])
        : (unsigned)((int)samples[0] - samples[1]);
    target = (difference * 4U + 3U) / 7U;
    while (index < 88 && (unsigned)ima_step[index] < target) ++index;
    if (index && target - (unsigned)ima_step[index - 1] <
                     (unsigned)ima_step[index] - target) {
        --index;
    }
    return index;
}

int ima_adpcm_encode_block(const int16_t *samples, size_t sample_count,
                           uint8_t *encoded, size_t encoded_capacity,
                           size_t *encoded_size) {
    IMAADPCMState state;
    size_t required = ima_adpcm_block_size(sample_count);
    size_t sample;
    if (!samples || !encoded || !encoded_size || !required ||
        encoded_capacity < required) {
        return -1;
    }
    state.predictor = samples[0];
    state.step_index = initial_step_index(samples, sample_count);
    if (ima_adpcm_block_header_write(encoded, encoded_capacity, &state,
                                      (uint16_t)sample_count)) {
        return -1;
    }
    for (sample = 1U; sample < sample_count; ++sample) {
        const uint8_t code = ima_adpcm_encode_sample(&state, samples[sample]);
        const size_t byte = IMA_ADPCM_BLOCK_HEADER_SIZE + (sample - 1U) / 2U;
        if (sample & 1U) encoded[byte] = code;
        else encoded[byte] = (uint8_t)(encoded[byte] | (code << 4));
    }
    *encoded_size = required;
    return 0;
}

int ima_adpcm_decode_block(const uint8_t *encoded, size_t encoded_size,
                           int16_t *samples, size_t sample_capacity,
                           size_t *sample_count) {
    IMAADPCMState state;
    uint16_t count;
    size_t required;
    size_t sample;
    if (!encoded || !samples || !sample_count ||
        ima_adpcm_block_header_read(encoded, encoded_size, &state, &count)) {
        return -1;
    }
    required = ima_adpcm_block_size(count);
    if (encoded_size != required || sample_capacity < count) return -1;
    samples[0] = (int16_t)state.predictor;
    for (sample = 1U; sample < count; ++sample) {
        const uint8_t packed = encoded[
            IMA_ADPCM_BLOCK_HEADER_SIZE + (sample - 1U) / 2U];
        const uint8_t code = (sample & 1U) ? (packed & 15U) : (packed >> 4);
        samples[sample] = ima_adpcm_decode_nibble(&state, code);
    }
    *sample_count = count;
    return 0;
}
