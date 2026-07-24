#define _CRT_SECURE_NO_WARNINGS

#include "bpv1.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int write_u8(FILE *file, uint8_t value) {
    return fputc(value, file) == EOF ? -1 : 0;
}

static int write_u16(FILE *file, uint16_t value) {
    uint8_t bytes[2] = {
        (uint8_t)value,
        (uint8_t)(value >> 8)
    };
    return fwrite(bytes, 1, sizeof bytes, file) == sizeof bytes ? 0 : -1;
}

static int write_u32(FILE *file, uint32_t value) {
    uint8_t bytes[4] = {
        (uint8_t)value,
        (uint8_t)(value >> 8),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 24)
    };
    return fwrite(bytes, 1, sizeof bytes, file) == sizeof bytes ? 0 : -1;
}

static int write_frame(FILE *file, int keyframe, uint8_t mode,
                       const uint8_t *payload, size_t payload_bytes) {
    const uint8_t mode_map = (uint8_t)(mode << 5);
    return write_u8(file, (uint8_t)keyframe) ||
           write_u32(file, (uint32_t)(1U + payload_bytes)) ||
           write_u32(file, 1) ||
           write_u8(file, mode_map) ||
           (payload_bytes &&
            fwrite(payload, 1, payload_bytes, file) != payload_bytes);
}

static int make_stream(FILE *file) {
    uint8_t palette[BPV1_MAX_PALETTE_BYTES] = {0};
    const uint8_t raw[BPV1_RECORD_BYTES] = {
        0, 0, 1, 2, 3, 0, 0, 0, 0
    };
    const uint8_t motion[2] = {0, 0};
    const uint8_t dictionary[2] = {0, 0};
    const uint8_t pattern_dictionary[7] = {0, 0, 0, 0, 1, 2, 3};
    palette[0] = 255;
    palette[4] = 255;
    palette[8] = 255;

    if (fwrite("BPV1", 1, 4, file) != 4 ||
        write_u8(file, BPV1_VERSION) ||
        write_u16(file, 4) || write_u16(file, 4) ||
        write_u32(file, 5) ||
        write_u16(file, 24) || write_u16(file, 1) ||
        write_u16(file, 5) ||
        write_u16(file, 4) || write_u16(file, 4) ||
        write_u8(file, 1) || write_u8(file, 0) ||
        fwrite(palette, 1, sizeof palette, file) != sizeof palette ||
        write_frame(file, 1, 4, raw, sizeof raw) ||
        write_frame(file, 0, 0, NULL, 0) ||
        write_frame(file, 0, 1, motion, sizeof motion) ||
        write_frame(file, 0, 2, dictionary, sizeof dictionary) ||
        write_frame(file, 0, 3, pattern_dictionary,
                    sizeof pattern_dictionary)) {
        return -1;
    }
    return fseek(file, 0, SEEK_SET);
}

static int validate_file(const char *path) {
    FILE *file = fopen(path, "rb");
    BPV1Header header;
    BPV1Decoder *decoder = NULL;
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t frame_index;
    int result = 1;
    if (!file) {
        fprintf(stderr, "Cannot open %s\n", path);
        return 1;
    }
    if (bpv1_header_read(file, &header) != BPV1_OK ||
        !(decoder = bpv1_decoder_create(&header))) {
        fprintf(stderr, "Cannot initialize BPV1 decoder for %s\n", path);
        goto cleanup;
    }
    for (frame_index = 0; frame_index < header.frame_count; ++frame_index) {
        BPV1Packet packet;
        const BPV1Frame *frame = NULL;
        size_t index;
        int status = bpv1_decoder_read_packet(decoder, file, &packet);
        if (status == BPV1_OK)
            status = bpv1_decoder_decode(decoder, &packet, &frame);
        if (status != BPV1_OK || !frame) {
            fprintf(stderr, "Frame %u failed: %s\n", frame_index,
                    bpv1_strerror(status));
            goto cleanup;
        }
        for (index = 0;
             index < (size_t)frame->block_count * BPV1_RECORD_BYTES;
             ++index) {
            hash ^= frame->blocks[index];
            hash *= UINT64_C(1099511628211);
        }
    }
    {
        BPV1Packet packet;
        const int status = bpv1_decoder_read_packet(decoder, file, &packet);
        if (status != BPV1_EOF) {
            fprintf(stderr, "Trailing BPV1 data: %s\n",
                    bpv1_strerror(status));
            goto cleanup;
        }
    }
    printf("BPV1 C validation: %ux%u, %u frames, hash %016llx\n",
           header.width, header.height, header.frame_count,
           (unsigned long long)hash);
    result = 0;

cleanup:
    bpv1_decoder_destroy(decoder);
    fclose(file);
    return result;
}

int main(int argc, char **argv) {
    FILE *file = NULL;
    BPV1Header header;
    BPV1Decoder *decoder = NULL;
    int frame_index;
    int result = 1;
    if (argc == 2) return validate_file(argv[1]);
    if (argc != 1) {
        fprintf(stderr, "Usage: %s [input.bpv1]\n", argv[0]);
        return 2;
    }
    file = tmpfile();
    if (!file || make_stream(file)) {
        fprintf(stderr, "Could not create the BPV1 test stream\n");
        goto cleanup;
    }
    if (bpv1_header_read(file, &header) != BPV1_OK ||
        header.width != 4 || header.height != 4 ||
        header.frame_count != 5 || header.palette_count != 64) {
        fprintf(stderr, "BPV1 header test failed\n");
        goto cleanup;
    }
    decoder = bpv1_decoder_create(&header);
    if (!decoder || bpv1_decoder_packet_capacity(decoder) != 10U) {
        fprintf(stderr, "BPV1 decoder allocation test failed\n");
        goto cleanup;
    }
    for (frame_index = 0; frame_index < 5; ++frame_index) {
        BPV1Packet packet;
        const BPV1Frame *frame = NULL;
        uint8_t row[12];
        uint16_t row565[4];
        int x;
        if (bpv1_decoder_read_packet(decoder, file, &packet) != BPV1_OK ||
            bpv1_decoder_decode(decoder, &packet, &frame) != BPV1_OK ||
            !frame || frame->frame_index != (uint32_t)frame_index ||
            bpv1_frame_render_rgb24_row(
                &header, frame, 0, row, sizeof row) != BPV1_OK ||
            bpv1_frame_render_rgb565_row(
                &header, frame, 0, row565, 4) != BPV1_OK) {
            fprintf(stderr, "BPV1 frame %d failed\n", frame_index);
            goto cleanup;
        }
        for (x = 0; x < 4; ++x) {
            if (row[x * 3] != 255 || row[x * 3 + 1] != 0 ||
                row[x * 3 + 2] != 0 || row565[x] != 0xf800) {
                fprintf(stderr, "BPV1 rendered pixel mismatch\n");
                goto cleanup;
            }
        }
    }
    {
        BPV1Packet packet;
        if (bpv1_decoder_read_packet(decoder, file, &packet) != BPV1_EOF) {
            fprintf(stderr, "BPV1 EOF test failed\n");
            goto cleanup;
        }
    }
    result = 0;
    puts("BPV1 portable C decoder tests passed");

cleanup:
    bpv1_decoder_destroy(decoder);
    if (file) fclose(file);
    return result;
}
