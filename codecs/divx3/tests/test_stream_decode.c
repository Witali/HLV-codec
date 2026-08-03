#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compact_yuv420.h"
#include "divx3.h"
#include "divx3_avi.h"

typedef struct MemoryReader {
    const uint8_t *data;
    size_t size;
    size_t position;
    size_t maximum_chunk;
} MemoryReader;

static size_t read_memory(
    void *context, uint8_t *buffer, size_t capacity) {
    MemoryReader *reader = (MemoryReader *)(context);
    size_t remaining;
    size_t wanted;
    if (!reader || !buffer || !capacity ||
        reader->position >= reader->size) {
        return 0;
    }
    remaining = reader->size - reader->position;
    wanted = capacity < remaining ? capacity : remaining;
    if (wanted > reader->maximum_chunk)
        wanted = reader->maximum_chunk;
    memcpy(buffer, reader->data + reader->position, wanted);
    reader->position += wanted;
    return wanted;
}

static uint64_t checksum_byte(uint64_t checksum, uint8_t value) {
    return (checksum ^ value) * UINT64_C(1099511628211);
}

static uint64_t checksum_plane(
    uint64_t checksum, const uint8_t *plane, unsigned stride,
    unsigned width, unsigned height, const int8_t *correction,
    unsigned correction_stride, unsigned bits, uint8_t *scratch) {
    unsigned y;
    for (y = 0; y < height; ++y) {
        unsigned x;
        const uint8_t *row = plane + (size_t)y * stride;
        if (correction) {
            compact_yuv420_unpack_corrected_samples(
                row, 0, (int)y, bits, correction,
                (int)correction_stride, scratch, (int)width);
            row = scratch;
        }
        for (x = 0; x < width; ++x)
            checksum = checksum_byte(checksum, row[x]);
    }
    return checksum;
}

static uint64_t checksum_frame(
    const Divx3Frame *frame, uint8_t *scratch) {
    const unsigned chroma_width = (frame->width + 1U) / 2U;
    const unsigned chroma_height = (frame->height + 1U) / 2U;
    uint64_t checksum = UINT64_C(1469598103934665603);
    checksum = checksum_plane(
        checksum, frame->y, frame->y_stride, frame->width,
        frame->height, frame->correction_y,
        frame->correction_stride_y, COMPACT_YUV420_LUMA_BITS,
        scratch);
    checksum = checksum_plane(
        checksum, frame->cb, frame->c_stride, chroma_width,
        chroma_height, frame->correction_cb,
        frame->correction_stride_c, COMPACT_YUV420_CHROMA_BITS,
        scratch);
    return checksum_plane(
        checksum, frame->cr, frame->c_stride, chroma_width,
        chroma_height, frame->correction_cr,
        frame->correction_stride_c, COMPACT_YUV420_CHROMA_BITS,
        scratch);
}

int main(int argc, char **argv) {
    FILE *input;
    Divx3AviInfo info;
    Divx3Decoder *contiguous_decoder;
    Divx3Decoder *stream_decoder;
    uint8_t *packet;
    uint8_t *scratch;
    unsigned frames = 0;
    unsigned frame_limit = 0;
    size_t largest_packet = 0;
    int result;
    if (argc != 2 && argc != 3) {
        fprintf(stderr,
                "usage: test_stream_decode INPUT.avi [frames]\n");
        return 2;
    }
    if (argc == 3) {
        frame_limit = (unsigned)strtoul(argv[2], NULL, 10);
        if (!frame_limit) {
            fprintf(stderr, "frames must be positive\n");
            return 2;
        }
    }
    input = fopen(argv[1], "rb");
    if (!input) {
        fprintf(stderr, "cannot open input AVI\n");
        return 1;
    }
    result = divx3_avi_read_info(input, &info);
    if (result != DIVX3_AVI_OK) {
        fprintf(stderr, "AVI probe failed: %s\n",
                divx3_avi_strerror(result));
        fclose(input);
        return 1;
    }
    contiguous_decoder =
        divx3_decoder_create_y6_u5_v5(info.width, info.height);
    stream_decoder =
        divx3_decoder_create_y6_u5_v5(info.width, info.height);
    packet = (uint8_t *)malloc(info.max_video_packet_size);
    scratch = (uint8_t *)malloc(info.width);
    if (!contiguous_decoder || !stream_decoder || !packet || !scratch) {
        fprintf(stderr, "test allocation failed\n");
        free(scratch);
        free(packet);
        divx3_decoder_destroy(stream_decoder);
        divx3_decoder_destroy(contiguous_decoder);
        fclose(input);
        return 1;
    }
    for (;;) {
        Divx3Frame contiguous_frame = {0};
        Divx3Frame stream_frame = {0};
        MemoryReader reader;
        size_t packet_size = 0;
        uint64_t contiguous_checksum;
        uint64_t stream_checksum;
        int probed_intra = 0;
        result = divx3_avi_read_video_packet(
            input, &info, packet, info.max_video_packet_size,
            &packet_size);
        if (result == DIVX3_AVI_EOF) break;
        if (result != DIVX3_AVI_OK) {
            fprintf(stderr, "packet %u failed: %s\n", frames,
                    divx3_avi_strerror(result));
            return 1;
        }
        if (packet_size > largest_packet)
            largest_packet = packet_size;
        result = divx3_decoder_decode(
            contiguous_decoder, packet, packet_size,
            &contiguous_frame);
        if (result != DIVX3_OK) {
            fprintf(stderr, "contiguous frame %u failed: %s\n",
                    frames, divx3_strerror(result));
            return 1;
        }
        result = divx3_packet_probe_intra(
            packet, packet_size, &probed_intra);
        if (result != DIVX3_OK ||
            probed_intra != (contiguous_frame.intra != 0)) {
            fprintf(stderr, "frame %u picture probe mismatch\n", frames);
            return 1;
        }
        reader.data = packet;
        reader.size = packet_size;
        reader.position = 0;
        reader.maximum_chunk = 257;
        result = divx3_decoder_decode_stream(
            stream_decoder, packet_size, read_memory, &reader,
            &stream_frame);
        if (result != DIVX3_OK) {
            fprintf(stderr, "stream frame %u failed: %s\n",
                    frames, divx3_strerror(result));
            return 1;
        }
        contiguous_checksum =
            checksum_frame(&contiguous_frame, scratch);
        stream_checksum = checksum_frame(&stream_frame, scratch);
        if (reader.position > packet_size ||
            contiguous_checksum != stream_checksum ||
            contiguous_frame.frame_number != stream_frame.frame_number ||
            contiguous_frame.intra != stream_frame.intra) {
            fprintf(stderr,
                    "frame %u mismatch: contiguous=%016" PRIx64
                    " stream=%016" PRIx64 "\n",
                    frames, contiguous_checksum, stream_checksum);
            return 1;
        }
        ++frames;
        if (frame_limit && frames >= frame_limit) break;
    }
    if (!frames || largest_packet <= DIVX3_STREAM_BUFFER_BYTES) {
        fprintf(stderr,
                "test needs a packet larger than the %u-byte refill "
                "buffer; largest=%zu\n",
                DIVX3_STREAM_BUFFER_BYTES, largest_packet);
        return 1;
    }
    printf("frames=%u largest_packet=%zu refill=%u checksums=identical\n",
           frames, largest_packet, DIVX3_STREAM_BUFFER_BYTES);
    free(scratch);
    free(packet);
    divx3_decoder_destroy(stream_decoder);
    divx3_decoder_destroy(contiguous_decoder);
    fclose(input);
    return 0;
}
