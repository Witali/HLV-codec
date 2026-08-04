#include "ima_adpcm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    uint8_t encoded[IMA_ADPCM_WAV_MAX_BLOCK_BYTES];
    int16_t samples[IMA_ADPCM_MAX_BLOCK_SAMPLES];
    uint8_t pcm[IMA_ADPCM_MAX_BLOCK_SAMPLES * 2U];
    size_t encoded_size;
    size_t sample_count = 0;
    size_t sample;
    FILE *input;
    FILE *output;
    if (argc != 3) return 2;
    input = fopen(argv[1], "rb");
    output = fopen(argv[2], "wb");
    if (!input || !output) return 1;
    encoded_size = fread(encoded, 1, sizeof encoded, input);
    if (ferror(input) || !feof(input) ||
        ima_adpcm_decode_wav_mono_block(
            encoded, encoded_size, samples, IMA_ADPCM_MAX_BLOCK_SAMPLES,
            &sample_count)) {
        return 1;
    }
    for (sample = 0; sample < sample_count; ++sample) {
        const uint16_t value = (uint16_t)samples[sample];
        pcm[sample * 2U] = (uint8_t)value;
        pcm[sample * 2U + 1U] = (uint8_t)(value >> 8);
    }
    if (fwrite(pcm, 2U, sample_count, output) != sample_count ||
        fclose(output) || fclose(input)) {
        return 1;
    }
    return 0;
}
