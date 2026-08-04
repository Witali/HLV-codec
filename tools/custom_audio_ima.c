/* Replace HLV/BPV packet audio with mono IMA ADPCM without re-encoding video. */
#include "hlv1.h"
#include "ima_adpcm.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    kAudioRateMinimum = 8000,
    kAudioRateMaximum = 48000,
    kCopyBufferBytes = 64 * 1024,
};

typedef struct Sha256 {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t used;
} Sha256;

static const uint32_t kSha256Round[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

static uint32_t rotate_right(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32U - bits));
}

static void sha256_transform(Sha256 *sha, const uint8_t *block) {
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    size_t index;
    for (index = 0; index < 16; ++index) {
        const uint8_t *p = block + index * 4U;
        words[index] = ((uint32_t)p[0] << 24) |
                       ((uint32_t)p[1] << 16) |
                       ((uint32_t)p[2] << 8) | p[3];
    }
    for (index = 16; index < 64; ++index) {
        uint32_t s0 = rotate_right(words[index - 15], 7) ^
                      rotate_right(words[index - 15], 18) ^
                      (words[index - 15] >> 3);
        uint32_t s1 = rotate_right(words[index - 2], 17) ^
                      rotate_right(words[index - 2], 19) ^
                      (words[index - 2] >> 10);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
    a = sha->state[0]; b = sha->state[1]; c = sha->state[2];
    d = sha->state[3]; e = sha->state[4]; f = sha->state[5];
    g = sha->state[6]; h = sha->state[7];
    for (index = 0; index < 64; ++index) {
        uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                        rotate_right(e, 25);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + sum1 + choose + kSha256Round[index] + words[index];
        uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                        rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = sum0 + majority;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    sha->state[0] += a; sha->state[1] += b;
    sha->state[2] += c; sha->state[3] += d;
    sha->state[4] += e; sha->state[5] += f;
    sha->state[6] += g; sha->state[7] += h;
}

static void sha256_init(Sha256 *sha) {
    static const uint32_t initial[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    memset(sha, 0, sizeof *sha);
    memcpy(sha->state, initial, sizeof initial);
}

static void sha256_update(Sha256 *sha, const void *data_value, size_t size) {
    const uint8_t *data = (const uint8_t *)data_value;
    sha->bit_count += (uint64_t)size * 8U;
    while (size) {
        size_t span = sizeof sha->block - sha->used;
        if (span > size) span = size;
        memcpy(sha->block + sha->used, data, span);
        sha->used += span;
        data += span;
        size -= span;
        if (sha->used == sizeof sha->block) {
            sha256_transform(sha, sha->block);
            sha->used = 0;
        }
    }
}

static void sha256_final(Sha256 *sha, uint8_t digest[32]) {
    uint64_t bits = sha->bit_count;
    size_t index;
    sha->block[sha->used++] = 0x80;
    if (sha->used > 56) {
        memset(sha->block + sha->used, 0, sizeof sha->block - sha->used);
        sha256_transform(sha, sha->block);
        sha->used = 0;
    }
    memset(sha->block + sha->used, 0, 56U - sha->used);
    for (index = 0; index < 8; ++index)
        sha->block[63U - index] = (uint8_t)(bits >> (index * 8U));
    sha256_transform(sha, sha->block);
    for (index = 0; index < 8; ++index) {
        digest[index * 4U] = (uint8_t)(sha->state[index] >> 24);
        digest[index * 4U + 1U] = (uint8_t)(sha->state[index] >> 16);
        digest[index * 4U + 2U] = (uint8_t)(sha->state[index] >> 8);
        digest[index * 4U + 3U] = (uint8_t)sha->state[index];
    }
}

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static int read_exact(FILE *file, void *data, size_t size) {
    return fread(data, 1, size, file) == size ? 0 : -1;
}

static int write_exact(FILE *file, const void *data, size_t size) {
    return fwrite(data, 1, size, file) == size ? 0 : -1;
}

static void hash_u32(Sha256 *sha, uint32_t value) {
    uint8_t bytes[4];
    write_u32(bytes, value);
    sha256_update(sha, bytes, sizeof bytes);
}

static size_t next_audio_samples(uint64_t *phase, uint32_t rate,
                                 uint32_t fps_num, uint32_t fps_den) {
    *phase += (uint64_t)rate * fps_den;
    {
        size_t samples = (size_t)(*phase / fps_num);
        *phase %= fps_num;
        return samples;
    }
}

static int read_pcm_block(FILE *pcm, int16_t *samples, size_t count,
                          uint64_t *padded_samples) {
    uint8_t bytes[IMA_ADPCM_MAX_BLOCK_SAMPLES * 2U];
    size_t wanted;
    size_t got;
    size_t index;
    if (!pcm || count > IMA_ADPCM_MAX_BLOCK_SAMPLES) return -1;
    wanted = count * 2U;
    got = fread(bytes, 1, wanted, pcm);
    if (got < wanted) {
        if (ferror(pcm)) return -1;
        if (got & 1U) return -1;
        memset(bytes + got, 0, wanted - got);
        *padded_samples += (wanted - got) / 2U;
    }
    for (index = 0; index < count; ++index)
        samples[index] = (int16_t)read_u16(bytes + index * 2U);
    return 0;
}

static int encode_audio_block(FILE *pcm, uint64_t *phase, uint32_t rate,
                              uint32_t fps_num, uint32_t fps_den,
                              uint8_t *encoded, size_t *encoded_size,
                              uint64_t *padded_samples) {
    int16_t samples[IMA_ADPCM_MAX_BLOCK_SAMPLES];
    size_t count = next_audio_samples(phase, rate, fps_num, fps_den);
    size_t capacity = ima_adpcm_block_size(count);
    if (!count || count > IMA_ADPCM_MAX_BLOCK_SAMPLES || !capacity) {
        fprintf(stderr, "custom_audio_ima: invalid interval of %u samples\n",
                (unsigned)count);
        return -1;
    }
    if (read_pcm_block(pcm, samples, count, padded_samples)) {
        fprintf(stderr, "custom_audio_ima: PCM input read failed\n");
        return -1;
    }
    if (ima_adpcm_encode_block(samples, count, encoded, capacity,
                               encoded_size)) {
        fprintf(stderr, "custom_audio_ima: IMA encode failed for %u samples\n",
                (unsigned)count);
        return -1;
    }
    {
        int16_t decoded[IMA_ADPCM_MAX_BLOCK_SAMPLES];
        size_t decoded_count = 0;
        if (ima_adpcm_decode_block(encoded, *encoded_size, decoded,
                                   IMA_ADPCM_MAX_BLOCK_SAMPLES,
                                   &decoded_count) ||
            decoded_count != count) {
            fprintf(stderr,
                    "custom_audio_ima: IMA verification failed (%u/%u)\n",
                    (unsigned)decoded_count, (unsigned)count);
            return -1;
        }
    }
    return 0;
}

static int validate_ima_block(const uint8_t *encoded, size_t encoded_size) {
    int16_t decoded[IMA_ADPCM_MAX_BLOCK_SAMPLES];
    size_t decoded_count = 0;
    return !encoded ||
           ima_adpcm_decode_block(encoded, encoded_size, decoded,
                                  IMA_ADPCM_MAX_BLOCK_SAMPLES,
                                  &decoded_count) ||
           !decoded_count;
}

static void hash_hlv_header(Sha256 *sha, const HLV1Header *header) {
    static const char tag[] = "HLV-VIDEO\0";
    sha256_update(sha, tag, sizeof tag);
    hash_u32(sha, header->version);
    hash_u32(sha, header->width);
    hash_u32(sha, header->height);
    hash_u32(sha, header->fps_num);
    hash_u32(sha, header->fps_den);
    hash_u32(sha, header->frame_count);
    hash_u32(sha, header->gop);
    hash_u32(sha, header->quality);
    hash_u32(sha, header->search_radius);
}

static int process_hlv(FILE *input, FILE *output, FILE *pcm, uint32_t rate,
                       Sha256 *sha, uint32_t *frame_total,
                       uint64_t *padded_samples) {
    HLV1Header header;
    uint8_t input_audio_codec;
    uint64_t phase = 0;
    uint32_t frames = 0;
    int result = hlv1_header_read(input, &header);
    if (result != HLV1_OK) {
        fprintf(stderr, "custom_audio_ima: HLV header read failed: %d\n",
                result);
        return -1;
    }
    input_audio_codec = header.audio_codec;
    hash_hlv_header(sha, &header);
    if (output) {
        header.flags |= HLV1_FLAG_AUDIO;
        header.audio_codec = HLV1_AUDIO_IMA_ADPCM;
        header.audio_sample_rate = (uint16_t)rate;
        header.audio_channels = 1;
        if (hlv1_header_write(output, &header) != HLV1_OK) {
            fprintf(stderr, "custom_audio_ima: HLV header write failed\n");
            return -1;
        }
    }
    for (;;) {
        HLV1Packet packet;
        size_t video_size;
        result = hlv1_packet_read(input, &packet);
        if (result == HLV1_EOF) break;
        if (result != HLV1_OK) {
            fprintf(stderr,
                    "custom_audio_ima: HLV packet %u read failed: %d\n",
                    frames, result);
            return -1;
        }
        video_size = hlv1_packet_video_payload_size(&packet);
        if ((!video_size && packet.bit_length) ||
            video_size > packet.payload_size) {
            hlv1_packet_free(&packet);
            fprintf(stderr,
                    "custom_audio_ima: HLV packet %u has invalid video size\n",
                    frames);
            return -1;
        }
        hash_u32(sha, packet.frame_type);
        hash_u32(sha, packet.q_y);
        hash_u32(sha, packet.q_uv);
        hash_u32(sha, packet.q_shift);
        hash_u32(sha, packet.bit_length);
        hash_u32(sha, (uint32_t)video_size);
        sha256_update(sha, packet.payload, video_size);
        if (input_audio_codec == HLV1_AUDIO_IMA_ADPCM &&
            validate_ima_block(hlv1_packet_audio_data(&packet),
                               hlv1_packet_audio_size(&packet))) {
            hlv1_packet_free(&packet);
            fprintf(stderr,
                    "custom_audio_ima: HLV packet %u has invalid IMA audio\n",
                    frames);
            return -1;
        }
        if (output) {
            uint8_t encoded[IMA_ADPCM_BLOCK_HEADER_SIZE +
                            (IMA_ADPCM_MAX_BLOCK_SAMPLES + 1U) / 2U];
            size_t encoded_size = 0;
            if (encode_audio_block(pcm, &phase, rate, header.fps_num,
                                   header.fps_den, encoded, &encoded_size,
                                   padded_samples)) {
                hlv1_packet_free(&packet);
                fprintf(stderr,
                        "custom_audio_ima: HLV packet %u audio encode failed\n",
                        frames);
                return -1;
            }
            packet.payload_size = (uint32_t)video_size;
            if (hlv1_packet_append_audio(&packet, encoded, encoded_size) !=
                    HLV1_OK ||
                hlv1_packet_write(output, &packet) != HLV1_OK) {
                hlv1_packet_free(&packet);
                fprintf(stderr,
                        "custom_audio_ima: HLV packet %u write failed\n",
                        frames);
                return -1;
            }
        }
        hlv1_packet_free(&packet);
        ++frames;
    }
    if (header.frame_count && frames != header.frame_count) {
        fprintf(stderr,
                "custom_audio_ima: HLV frame count mismatch: %u/%u\n",
                frames, header.frame_count);
        return -1;
    }
    *frame_total = frames;
    return 0;
}

typedef struct BpvHeader {
    uint8_t fixed[25];
    uint8_t audio[4];
    uint8_t *palette;
    size_t palette_size;
    uint8_t version;
    uint32_t frame_count;
    uint16_t fps_num;
    uint16_t fps_den;
    uint8_t audio_codec;
} BpvHeader;

static void free_bpv_header(BpvHeader *header) {
    free(header->palette);
    memset(header, 0, sizeof *header);
}

static int read_bpv_header(FILE *file, BpvHeader *header) {
    memset(header, 0, sizeof *header);
    if (read_exact(file, header->fixed, sizeof header->fixed) ||
        memcmp(header->fixed, "BPV1", 4)) return -1;
    header->version = header->fixed[4];
    if (header->version < 3 || header->version > 7 ||
        read_exact(file, header->audio, sizeof header->audio)) return -1;
    header->frame_count = read_u32(header->fixed + 9);
    header->fps_num = read_u16(header->fixed + 13);
    header->fps_den = read_u16(header->fixed + 15);
    header->audio_codec = header->fixed[24];
    if (!header->frame_count || !header->fps_num || !header->fps_den ||
        header->audio[3] != 0) return -1;
    if (header->version < 4) {
        header->palette_size = 64U * 16U * 3U;
        header->palette = (uint8_t *)malloc(header->palette_size);
        if (!header->palette ||
            read_exact(file, header->palette, header->palette_size)) {
            free_bpv_header(header);
            return -1;
        }
    }
    return 0;
}

static void hash_bpv_header(Sha256 *sha, const BpvHeader *header) {
    uint8_t fixed[25];
    static const char tag[] = "BPV-VIDEO\0";
    memcpy(fixed, header->fixed, sizeof fixed);
    fixed[24] = 0;
    sha256_update(sha, tag, sizeof tag);
    sha256_update(sha, fixed, sizeof fixed);
    sha256_update(sha, header->palette, header->palette_size);
}

static int copy_video_bytes(FILE *input, FILE *output, uint32_t count,
                            Sha256 *sha) {
    uint8_t buffer[kCopyBufferBytes];
    uint32_t remaining = count;
    while (remaining) {
        size_t chunk = remaining < sizeof buffer ? remaining : sizeof buffer;
        if (read_exact(input, buffer, chunk)) return -1;
        sha256_update(sha, buffer, chunk);
        if (output && write_exact(output, buffer, chunk)) return -1;
        remaining -= (uint32_t)chunk;
    }
    return 0;
}

static int discard_bytes(FILE *input, uint32_t count) {
    uint8_t buffer[4096];
    uint32_t remaining = count;
    while (remaining) {
        size_t chunk = remaining < sizeof buffer ? remaining : sizeof buffer;
        if (read_exact(input, buffer, chunk)) return -1;
        remaining -= (uint32_t)chunk;
    }
    return 0;
}

static int process_bpv(FILE *input, FILE *output, FILE *pcm, uint32_t rate,
                       Sha256 *sha, uint32_t *frame_total,
                       uint64_t *padded_samples) {
    BpvHeader header;
    uint64_t phase = 0;
    uint32_t frame_index;
    int extra;
    if (read_bpv_header(input, &header)) return -1;
    hash_bpv_header(sha, &header);
    if (output) {
        uint8_t fixed[25];
        uint8_t audio[4];
        memcpy(fixed, header.fixed, sizeof fixed);
        memcpy(audio, header.audio, sizeof audio);
        fixed[24] = 2;
        write_u16(audio, (uint16_t)rate);
        audio[2] = 1;
        audio[3] = 0;
        if (write_exact(output, fixed, sizeof fixed) ||
            write_exact(output, audio, sizeof audio) ||
            write_exact(output, header.palette, header.palette_size)) {
            free_bpv_header(&header);
            return -1;
        }
    }
    for (frame_index = 0; frame_index < header.frame_count; ++frame_index) {
        uint8_t frame_header[13];
        uint8_t hash_header[13];
        uint32_t frame_bytes;
        uint32_t old_audio_bytes;
        if (read_exact(input, frame_header, sizeof frame_header)) {
            free_bpv_header(&header);
            return -1;
        }
        frame_bytes = read_u32(frame_header + 1);
        old_audio_bytes = read_u32(frame_header + 9);
        if (frame_header[0] > 1U || !frame_bytes) {
            free_bpv_header(&header);
            return -1;
        }
        memcpy(hash_header, frame_header, sizeof hash_header);
        write_u32(hash_header + 9, 0);
        sha256_update(sha, hash_header, sizeof hash_header);
        if (output) {
            uint8_t encoded[IMA_ADPCM_BLOCK_HEADER_SIZE +
                            (IMA_ADPCM_MAX_BLOCK_SAMPLES + 1U) / 2U];
            size_t encoded_size = 0;
            if (encode_audio_block(pcm, &phase, rate, header.fps_num,
                                   header.fps_den, encoded, &encoded_size,
                                   padded_samples) ||
                encoded_size > UINT32_MAX) {
                free_bpv_header(&header);
                return -1;
            }
            write_u32(frame_header + 9, (uint32_t)encoded_size);
            if (write_exact(output, frame_header, sizeof frame_header) ||
                copy_video_bytes(input, output, frame_bytes, sha) ||
                discard_bytes(input, old_audio_bytes) ||
                write_exact(output, encoded, encoded_size)) {
                free_bpv_header(&header);
                return -1;
            }
        } else {
            if (copy_video_bytes(input, NULL, frame_bytes, sha)) {
                free_bpv_header(&header);
                return -1;
            }
            if (header.audio_codec == 2) {
                uint8_t encoded[IMA_ADPCM_BLOCK_HEADER_SIZE +
                                (IMA_ADPCM_MAX_BLOCK_SAMPLES + 1U) / 2U];
                if (old_audio_bytes > sizeof encoded ||
                    read_exact(input, encoded, old_audio_bytes) ||
                    validate_ima_block(encoded, old_audio_bytes)) {
                    free_bpv_header(&header);
                    return -1;
                }
            } else if (discard_bytes(input, old_audio_bytes)) {
                free_bpv_header(&header);
                return -1;
            }
        }
    }
    extra = fgetc(input);
    if (extra != EOF || ferror(input)) {
        free_bpv_header(&header);
        return -1;
    }
    *frame_total = header.frame_count;
    free_bpv_header(&header);
    return 0;
}

static int process_file(const char *input_path, const char *output_path,
                        const char *pcm_path, uint32_t rate,
                        uint8_t digest[32], uint32_t *frames,
                        uint64_t *padded_samples) {
    FILE *input = NULL;
    FILE *output = NULL;
    FILE *pcm = NULL;
    uint8_t magic[4];
    Sha256 sha;
    int result = -1;
    input = fopen(input_path, "rb");
    if (!input || read_exact(input, magic, sizeof magic) ||
        fseek(input, 0, SEEK_SET)) goto cleanup;
    if (output_path) {
        output = fopen(output_path, "wb");
        pcm = fopen(pcm_path, "rb");
        if (!output || !pcm) goto cleanup;
    }
    sha256_init(&sha);
    if (!memcmp(magic, "HLV1", 4))
        result = process_hlv(input, output, pcm, rate, &sha, frames,
                             padded_samples);
    else if (!memcmp(magic, "BPV1", 4))
        result = process_bpv(input, output, pcm, rate, &sha, frames,
                             padded_samples);
    if (!result && output && (fflush(output) || ferror(output))) result = -1;
    if (!result) sha256_final(&sha, digest);
cleanup:
    if (pcm) fclose(pcm);
    if (output && fclose(output)) result = -1;
    if (input) fclose(input);
    return result;
}

static void print_digest(const uint8_t digest[32]) {
    size_t index;
    for (index = 0; index < 32; ++index) printf("%02x", digest[index]);
}

static int sha256_self_test(void) {
    static const uint8_t expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    uint8_t actual[32];
    Sha256 sha;
    sha256_init(&sha);
    sha256_update(&sha, "abc", 3);
    sha256_final(&sha, actual);
    return memcmp(actual, expected, sizeof expected) ? -1 : 0;
}

static void usage(const char *program) {
    fprintf(stderr,
            "Usage:\n"
            "  %s --video-sha256 INPUT.hlv|INPUT.bpv1\n"
            "  %s INPUT OUTPUT PCM_S16LE RATE\n",
            program, program);
}

int main(int argc, char **argv) {
    uint8_t digest[32];
    uint32_t frames = 0;
    uint64_t padded_samples = 0;
    uint32_t rate = 0;
    char *end = NULL;
    if (sha256_self_test()) {
        fprintf(stderr, "custom_audio_ima: SHA-256 self-test failed\n");
        return 1;
    }
    if (argc == 3 && !strcmp(argv[1], "--video-sha256")) {
        if (process_file(argv[2], NULL, NULL, 0, digest, &frames,
                         &padded_samples)) {
            fprintf(stderr, "custom_audio_ima: invalid input: %s\n", argv[2]);
            return 1;
        }
        print_digest(digest);
        printf("\n");
        return 0;
    }
    if (argc != 5) {
        usage(argv[0]);
        return 2;
    }
    errno = 0;
    rate = (uint32_t)strtoul(argv[4], &end, 10);
    if (errno || !end || *end || rate < kAudioRateMinimum ||
        rate > kAudioRateMaximum || !strcmp(argv[1], argv[2])) {
        usage(argv[0]);
        return 2;
    }
    if (process_file(argv[1], argv[2], argv[3], rate, digest, &frames,
                     &padded_samples)) {
        fprintf(stderr, "custom_audio_ima: rewrite failed\n");
        remove(argv[2]);
        return 1;
    }
    printf("frames=%u video_sha256=", frames);
    print_digest(digest);
    printf(" padded_samples=%llu\n", (unsigned long long)padded_samples);
    return 0;
}
