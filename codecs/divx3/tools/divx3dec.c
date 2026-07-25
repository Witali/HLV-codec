#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "divx3.h"
#include "divx3_avi.h"

static int write_plane(FILE *output, const uint8_t *plane,
                       unsigned stride, unsigned width, unsigned height) {
    unsigned row;
    for (row = 0; row < height; ++row) {
        if (fwrite(plane + (size_t)row * stride, 1, width, output) != width)
            return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    FILE *input;
    FILE *output;
    Divx3AviInfo info;
    Divx3Decoder *decoder;
    uint8_t *packet;
    int result;
    if (argc != 3) {
        fprintf(stderr, "usage: divx3dec INPUT.avi OUTPUT.y4m|-\n");
        return 2;
    }
    input = fopen(argv[1], "rb");
    if (!input) {
        fprintf(stderr, "divx3dec: cannot open %s\n", argv[1]);
        return 1;
    }
    output = !strcmp(argv[2], "-") ? stdout : fopen(argv[2], "wb");
    if (!output) {
        fprintf(stderr, "divx3dec: cannot create %s\n", argv[2]);
        fclose(input);
        return 1;
    }
    result = divx3_avi_read_info(input, &info);
    if (result != DIVX3_AVI_OK) {
        fprintf(stderr, "divx3dec: %s\n", divx3_avi_strerror(result));
        if (output != stdout) fclose(output);
        fclose(input);
        return 1;
    }
    decoder = divx3_decoder_create(info.width, info.height);
    packet = (uint8_t *)malloc(info.max_video_packet_size);
    if (!decoder || !packet) {
        fprintf(stderr, "divx3dec: not enough memory\n");
        free(packet);
        divx3_decoder_destroy(decoder);
        if (output != stdout) fclose(output);
        fclose(input);
        return 1;
    }
    if (fprintf(output, "YUV4MPEG2 W%u H%u F%u:%u Ip A1:1 C420jpeg\n",
                info.width, info.height, info.fps_num, info.fps_den) < 0) {
        fprintf(stderr, "divx3dec: cannot write Y4M header\n");
        return 1;
    }
    for (;;) {
        Divx3Frame frame = {0};
        size_t packet_size;
        unsigned chroma_width = (info.width + 1U) / 2U;
        unsigned chroma_height = (info.height + 1U) / 2U;
        result = divx3_avi_read_video_packet(
            input, &info, packet, info.max_video_packet_size,
            &packet_size);
        if (result == DIVX3_AVI_EOF) break;
        if (result != DIVX3_AVI_OK) {
            fprintf(stderr, "divx3dec: %s\n",
                    divx3_avi_strerror(result));
            return 1;
        }
        result = divx3_decoder_decode(
            decoder, packet, packet_size, &frame);
        if (result != DIVX3_OK) {
            fprintf(stderr, "divx3dec: frame %u: %s\n",
                    frame.frame_number, divx3_strerror(result));
            return 1;
        }
        if (fwrite("FRAME\n", 1, 6, output) != 6 ||
            !write_plane(output, frame.y, frame.y_stride,
                         info.width, info.height) ||
            !write_plane(output, frame.cb, frame.c_stride,
                         chroma_width, chroma_height) ||
            !write_plane(output, frame.cr, frame.c_stride,
                         chroma_width, chroma_height)) {
            fprintf(stderr, "divx3dec: output write failed\n");
            return 1;
        }
    }
    free(packet);
    divx3_decoder_destroy(decoder);
    if (output != stdout && fclose(output)) {
        fprintf(stderr, "divx3dec: output close failed\n");
        fclose(input);
        return 1;
    }
    fclose(input);
    return 0;
}
