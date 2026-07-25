#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "divx3.h"
#include "divx3_avi.h"

static uint64_t checksum_byte(uint64_t checksum, uint8_t value) {
    return (checksum ^ value) * UINT64_C(1099511628211);
}

static int write_plane(FILE *output, const uint8_t *plane,
                       unsigned stride, unsigned width, unsigned height,
                       uint64_t *checksum) {
    unsigned y;
    for (y = 0; y < height; ++y) {
        unsigned x;
        const uint8_t *row = plane + (size_t)y * stride;
        if (fwrite(row, 1, width, output) != width) return 0;
        for (x = 0; x < width; ++x)
            *checksum = checksum_byte(*checksum, row[x]);
    }
    return 1;
}

int main(int argc, char **argv) {
    FILE *input;
    FILE *output;
    Divx3AviInfo info;
    Divx3Decoder *decoder;
    uint8_t *packet;
    unsigned frames = 0;
    uint64_t checksum = UINT64_C(1469598103934665603);
    int result;
    if (argc != 3) {
        fprintf(stderr, "usage: test_decode INPUT.avi OUTPUT.yuv\n");
        return 2;
    }
    input = fopen(argv[1], "rb");
    if (!input) {
        fprintf(stderr, "cannot open input AVI\n");
        return 1;
    }
    output = fopen(argv[2], "wb");
    if (!output) {
        fprintf(stderr, "cannot create output YUV\n");
        fclose(input);
        return 1;
    }
    result = divx3_avi_read_info(input, &info);
    if (result != DIVX3_AVI_OK) {
        fprintf(stderr, "AVI probe failed: %s\n",
                divx3_avi_strerror(result));
        fclose(output);
        fclose(input);
        return 1;
    }
    decoder = divx3_decoder_create(info.width, info.height);
    packet = (uint8_t *)malloc(info.max_video_packet_size);
    if (!decoder || !packet) {
        fprintf(stderr, "decoder allocation failed\n");
        free(packet);
        divx3_decoder_destroy(decoder);
        fclose(output);
        fclose(input);
        return 1;
    }
    for (;;) {
        Divx3Frame frame;
        size_t packet_size = 0;
        unsigned chroma_width = (info.width + 1U) / 2U;
        unsigned chroma_height = (info.height + 1U) / 2U;
        result = divx3_avi_read_video_packet(
            input, &info, packet, info.max_video_packet_size,
            &packet_size);
        if (result == DIVX3_AVI_EOF) break;
        if (result != DIVX3_AVI_OK) {
            fprintf(stderr, "packet %u failed: %s\n", frames,
                    divx3_avi_strerror(result));
            return 1;
        }
        result = divx3_decoder_decode(
            decoder, packet, packet_size, &frame);
        if (result != DIVX3_OK) {
            fprintf(stderr, "frame %u failed: %s\n", frames,
                    divx3_strerror(result));
            return 1;
        }
        if (!write_plane(output, frame.y, frame.y_stride,
                         info.width, info.height, &checksum) ||
            !write_plane(output, frame.cb, frame.c_stride,
                         chroma_width, chroma_height, &checksum) ||
            !write_plane(output, frame.cr, frame.c_stride,
                         chroma_width, chroma_height, &checksum)) {
            fprintf(stderr, "cannot write decoded frame %u\n", frames);
            return 1;
        }
        ++frames;
    }
    printf("width=%u height=%u fps=%u/%u frames=%u memory=%zu "
           "checksum=%016" PRIx64 "\n",
           info.width, info.height, info.fps_num, info.fps_den,
           frames, divx3_decoder_memory_bytes(decoder), checksum);
    free(packet);
    divx3_decoder_destroy(decoder);
    fclose(output);
    fclose(input);
    return frames ? 0 : 1;
}
