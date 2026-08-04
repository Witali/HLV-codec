#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "pl_mpeg.h"

int main(int argc, char **argv) {
    plm_t *mpeg;
    unsigned frames = 0;
    uint64_t samples_decoded = 0;
    uint64_t detailed_low_bytes = 0;
    uint64_t hash = UINT64_C(1469598103934665603);
    int minimum = INT_MAX;
    int maximum = INT_MIN;

    if (argc != 2) {
        fprintf(stderr, "usage: %s input.mpg\n", argv[0]);
        return 2;
    }
    mpeg = plm_create_with_filename(argv[1]);
    if (!mpeg) {
        fprintf(stderr, "could not open MPEG program stream\n");
        return 3;
    }
    plm_set_video_enabled(mpeg, 0);
    if (plm_get_num_audio_streams(mpeg) < 1 ||
        plm_get_samplerate(mpeg) != 32000) {
        fprintf(stderr, "missing 32 kHz MP2 stream\n");
        plm_destroy(mpeg);
        return 4;
    }

    while (frames < 32U) {
        plm_samples_t *decoded = plm_decode_audio(mpeg);
        unsigned i;
        if (!decoded) break;
        if (decoded->count != PLM_AUDIO_SAMPLES_PER_FRAME) {
            fprintf(stderr, "unexpected MP2 frame sample count\n");
            plm_destroy(mpeg);
            return 5;
        }
        for (i = 0; i < decoded->count; ++i) {
            const int sample = decoded->mono_s16[i];
            const uint16_t bits = (uint16_t)decoded->mono_s16[i];
            if (sample < minimum) minimum = sample;
            if (sample > maximum) maximum = sample;
            if ((bits & UINT16_C(0x00ff)) != 0U) {
                ++detailed_low_bytes;
            }
            hash ^= bits & UINT16_C(0x00ff);
            hash *= UINT64_C(1099511628211);
            hash ^= bits >> 8;
            hash *= UINT64_C(1099511628211);
        }
        samples_decoded += decoded->count;
        ++frames;
    }
    plm_destroy(mpeg);

    if (frames == 0U || minimum >= 0 || maximum <= 0 ||
        maximum - minimum < 4096 || detailed_low_bytes < 100U) {
        fprintf(stderr,
                "PCM16 detail check failed: frames=%u min=%d max=%d low=%" PRIu64 "\n",
                frames, minimum, maximum, detailed_low_bytes);
        return 6;
    }
    printf("MP2_PCM16_OK,frames=%u,samples=%" PRIu64
           ",min=%d,max=%d,low=%" PRIu64 ",hash=%016" PRIx64 "\n",
           frames, samples_decoded, minimum, maximum,
           detailed_low_bytes, hash);
    return 0;
}
