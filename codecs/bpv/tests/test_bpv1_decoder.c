#define _CRT_SECURE_NO_WARNINGS

#include "bpv1.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BPV1_V5_VERSION 5
#define TEST_BPV1_V6_VERSION 6

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
        write_u8(file, BPV1_VIDEO_VERSION) ||
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

static int make_eviction_stream(FILE *file) {
    uint8_t palette[BPV1_MAX_PALETTE_BYTES] = {0};
    const uint8_t records[3][BPV1_RECORD_BYTES] = {
        {0, 0, 1, 2, 3, 0x00, 0x00, 0x00, 0x00},
        {0, 0, 1, 2, 3, 0x55, 0x55, 0x55, 0x55},
        {0, 0, 1, 2, 3, 0xaa, 0xaa, 0xaa, 0xaa}
    };
    const uint8_t dictionary_zero[2] = {0, 0};
    const uint8_t dictionary_one[2] = {1, 0};

    if (fwrite("BPV1", 1, 4, file) != 4 ||
        write_u8(file, BPV1_VIDEO_VERSION) ||
        write_u16(file, 4) || write_u16(file, 4) ||
        write_u32(file, 5) ||
        write_u16(file, 24) || write_u16(file, 1) ||
        write_u16(file, 5) ||
        write_u16(file, 2) || write_u16(file, 2) ||
        write_u8(file, 1) || write_u8(file, 0) ||
        fwrite(palette, 1, sizeof palette, file) != sizeof palette ||
        write_frame(file, 1, 4, records[0], sizeof records[0]) ||
        write_frame(file, 0, 4, records[1], sizeof records[1]) ||
        write_frame(file, 0, 4, records[2], sizeof records[2]) ||
        write_frame(file, 0, 2, dictionary_zero,
                    sizeof dictionary_zero) ||
        write_frame(file, 0, 2, dictionary_one,
                    sizeof dictionary_one)) {
        return -1;
    }
    return fseek(file, 0, SEEK_SET);
}

static int test_dictionary_eviction(void) {
    static const uint8_t expected_patterns[5] = {
        0x00, 0x55, 0xaa, 0x55, 0xaa
    };
    FILE *file = tmpfile();
    BPV1Header header;
    BPV1Decoder *decoder = NULL;
    int frame_index;
    int result = 1;
    if (!file || make_eviction_stream(file) ||
        bpv1_header_read(file, &header) != BPV1_OK ||
        !(decoder = bpv1_decoder_create(&header))) {
        fprintf(stderr, "BPV1 dictionary eviction setup failed\n");
        goto cleanup;
    }
    for (frame_index = 0; frame_index < 5; ++frame_index) {
        BPV1Packet packet;
        const BPV1Frame *frame = NULL;
        if (bpv1_decoder_read_packet(decoder, file, &packet) != BPV1_OK ||
            bpv1_decoder_decode(decoder, &packet, &frame) != BPV1_OK ||
            !frame ||
            frame->blocks[5] != expected_patterns[frame_index]) {
            fprintf(stderr,
                    "BPV1 dictionary eviction frame %d failed\n",
                    frame_index);
            goto cleanup;
        }
    }
    result = 0;

cleanup:
    bpv1_decoder_destroy(decoder);
    if (file) fclose(file);
    return result;
}

static int test_audio_packet(void) {
    uint8_t palette[BPV1_MAX_PALETTE_BYTES] = {0};
    const uint8_t raw[BPV1_RECORD_BYTES] = {
        0, 0, 1, 2, 3, 0, 0, 0, 0
    };
    uint8_t audio[100];
    FILE *file = tmpfile();
    BPV1Header header;
    BPV1Decoder *decoder = NULL;
    BPV1Packet packet;
    const BPV1Frame *frame = NULL;
    int result = 1;
    size_t index;
    for (index = 0; index < sizeof audio; ++index)
        audio[index] = (uint8_t)index;
    if (!file ||
        fwrite("BPV1", 1, 4, file) != 4 ||
        write_u8(file, BPV1_AUDIO_VERSION) ||
        write_u16(file, 4) || write_u16(file, 4) ||
        write_u32(file, 1) ||
        write_u16(file, 10) || write_u16(file, 1) ||
        write_u16(file, 1) ||
        write_u16(file, 4) || write_u16(file, 4) ||
        write_u8(file, 0) || write_u8(file, BPV1_AUDIO_PCM_U8) ||
        write_u16(file, 1000) || write_u8(file, 1) ||
        write_u8(file, 0) ||
        fwrite(palette, 1, sizeof palette, file) != sizeof palette ||
        write_u8(file, 1) ||
        write_u32(file, 1U + sizeof raw) ||
        write_u32(file, 1) ||
        write_u32(file, sizeof audio) ||
        write_u8(file, 4U << 5) ||
        fwrite(raw, 1, sizeof raw, file) != sizeof raw ||
        fwrite(audio, 1, sizeof audio, file) != sizeof audio ||
        fseek(file, 0, SEEK_SET) ||
        bpv1_header_read(file, &header) != BPV1_OK ||
        header.audio_codec != BPV1_AUDIO_PCM_U8 ||
        header.audio_sample_rate != 1000 ||
        header.audio_channels != 1 ||
        !(decoder = bpv1_decoder_create(&header)) ||
        bpv1_decoder_read_packet(decoder, file, &packet) != BPV1_OK ||
        packet.audio_size != sizeof audio ||
        !packet.audio_data ||
        memcmp(packet.audio_data, audio, sizeof audio) ||
        bpv1_decoder_decode(decoder, &packet, &frame) != BPV1_OK ||
        !frame) {
        fprintf(stderr, "BPV1 v3 audio packet test failed\n");
        goto cleanup;
    }
    result = 0;

cleanup:
    bpv1_decoder_destroy(decoder);
    if (file) fclose(file);
    return result;
}

static int write_active_frame(FILE *file, const uint8_t *palette,
                              const uint8_t *raw) {
    const uint32_t frame_bytes =
        BPV1_MAX_PALETTE_BYTES + 1U + 2U;
    return write_u8(file, 1) ||
           write_u32(file, frame_bytes) ||
           write_u32(file, 1) ||
           write_u32(file, 0) ||
           fwrite(palette, 1, BPV1_MAX_PALETTE_BYTES, file) !=
               BPV1_MAX_PALETTE_BYTES ||
           write_u8(file, 4U << 5) ||
           fwrite(raw, 1, 2, file) != 2;
}

static int test_active_palettes(void) {
    uint8_t red_palette[BPV1_MAX_PALETTE_BYTES] = {0};
    uint8_t green_palette[BPV1_MAX_PALETTE_BYTES] = {0};
    const uint8_t raw[2] = {0};
    FILE *file = tmpfile();
    BPV1Header header;
    BPV1Decoder *decoder = NULL;
    uint8_t expected[2][3] = {{255, 0, 0}, {0, 255, 0}};
    uint16_t palette565[BPV1_MAX_PALETTE_COLORS];
    int frame_index;
    int result = 1;
    red_palette[0] = 255;
    green_palette[1] = 255;
    if (!file ||
        fwrite("BPV1", 1, 4, file) != 4 ||
        write_u8(file, TEST_BPV1_V5_VERSION) ||
        write_u16(file, 4) || write_u16(file, 4) ||
        write_u32(file, 2) ||
        write_u16(file, 24) || write_u16(file, 1) ||
        write_u16(file, 1) ||
        write_u16(file, 4) || write_u16(file, 4) ||
        write_u8(file, 0) || write_u8(file, BPV1_AUDIO_NONE) ||
        write_u16(file, 0) || write_u8(file, 0) || write_u8(file, 0) ||
        write_active_frame(file, red_palette, raw) ||
        write_active_frame(file, green_palette, raw) ||
        fseek(file, 0, SEEK_SET) ||
        bpv1_header_read(file, &header) != BPV1_OK ||
        header.version != TEST_BPV1_V5_VERSION ||
        !(decoder = bpv1_decoder_create(&header)) ||
        bpv1_decoder_packet_capacity(decoder) !=
            BPV1_MAX_PALETTE_BYTES + 10U) {
        fprintf(stderr, "BPV1 v5 active-palette setup failed\n");
        goto cleanup;
    }
    for (frame_index = 0; frame_index < 2; ++frame_index) {
        BPV1Packet packet;
        const BPV1Frame *frame = NULL;
        uint8_t row[12];
        uint16_t row565[4];
        if (bpv1_decoder_read_packet(decoder, file, &packet) != BPV1_OK ||
            bpv1_decoder_decode(decoder, &packet, &frame) != BPV1_OK ||
            bpv1_frame_render_rgb24_row(
                &header, frame, 0, row, sizeof row) != BPV1_OK ||
            bpv1_palette_build_rgb565(
                &header, frame, palette565,
                BPV1_MAX_PALETTE_COLORS) != BPV1_OK ||
            bpv1_frame_render_rgb565_row_cached(
                &header, frame, 0, palette565,
                BPV1_MAX_PALETTE_COLORS, row565, 4) != BPV1_OK ||
            row[0] != expected[frame_index][0] ||
            row[1] != expected[frame_index][1] ||
            row[2] != expected[frame_index][2] ||
            row565[0] != (frame_index ? 0x07e0 : 0xf800)) {
            fprintf(stderr, "BPV1 v5 active palette frame %d failed\n",
                    frame_index);
            goto cleanup;
        }
    }
    result = 0;

cleanup:
    bpv1_decoder_destroy(decoder);
    if (file) fclose(file);
    return result;
}

static int test_adaptive_raw_records(void) {
    uint8_t palette[BPV1_MAX_PALETTE_BYTES] = {0};
    const uint8_t modes[2] = {0x92, 0x40};
    const uint8_t payload[20] = {
        0x00, 0x50,
        0x41, 0x23, 0xaa, 0x55,
        0x82, 0x45, 0x60, 0x18, 0x18, 0x18, 0x18,
        0xc3, 0x78, 0x9a, 0x1b, 0x1b, 0x1b, 0x1b
    };
    const uint8_t expected[4][BPV1_RECORD_BYTES] = {
        {0, 5, 0, 0, 0, 0, 0, 0, 0},
        {1, 2, 3, 0, 0, 0x44, 0x44, 0x11, 0x11},
        {2, 4, 5, 6, 0, 0x18, 0x18, 0x18, 0x18},
        {3, 7, 8, 9, 10, 0x1b, 0x1b, 0x1b, 0x1b}
    };
    FILE *file = tmpfile();
    BPV1Header header;
    BPV1Decoder *decoder = NULL;
    BPV1Packet packet;
    const BPV1Frame *frame = NULL;
    int result = 1;
    if (!file ||
        fwrite("BPV1", 1, 4, file) != 4 ||
        write_u8(file, TEST_BPV1_V5_VERSION) ||
        write_u16(file, 16) || write_u16(file, 4) ||
        write_u32(file, 1) ||
        write_u16(file, 24) || write_u16(file, 1) ||
        write_u16(file, 1) ||
        write_u16(file, 8) || write_u16(file, 8) ||
        write_u8(file, 0) || write_u8(file, BPV1_AUDIO_NONE) ||
        write_u16(file, 0) || write_u8(file, 0) || write_u8(file, 0) ||
        write_u8(file, 1) ||
        write_u32(file, BPV1_MAX_PALETTE_BYTES +
                         sizeof modes + sizeof payload) ||
        write_u32(file, sizeof modes) ||
        write_u32(file, 0) ||
        fwrite(palette, 1, sizeof palette, file) != sizeof palette ||
        fwrite(modes, 1, sizeof modes, file) != sizeof modes ||
        fwrite(payload, 1, sizeof payload, file) != sizeof payload ||
        fseek(file, 0, SEEK_SET) ||
        bpv1_header_read(file, &header) != BPV1_OK ||
        !(decoder = bpv1_decoder_create(&header)) ||
        bpv1_decoder_packet_capacity(decoder) !=
            BPV1_MAX_PALETTE_BYTES + 2U + 4U * 9U ||
        bpv1_decoder_read_packet(decoder, file, &packet) != BPV1_OK ||
        bpv1_decoder_decode(decoder, &packet, &frame) != BPV1_OK ||
        !frame ||
        memcmp(frame->blocks, expected, sizeof expected)) {
        fprintf(stderr, "BPV1 v5 adaptive RAW test failed\n");
        goto cleanup;
    }
    result = 0;

cleanup:
    bpv1_decoder_destroy(decoder);
    if (file) fclose(file);
    return result;
}

static int test_direct_raw_records(void) {
    uint8_t palette[BPV1_MAX_PALETTE_BYTES] = {0};
    uint8_t rgb24[8U * 3U];
    uint16_t rgb565[8];
    uint16_t cached_rgb565[8];
    uint16_t palette_rgb565[BPV1_MAX_PALETTE_COLORS];
    const uint8_t expected_rgb24[8U * 3U] = {
         0, 0,  0, 16, 0,  0, 32, 0,  0, 48, 0,  0,
         0, 0,  0,  0, 0, 16,  0, 0, 32,  0, 0, 48
    };
    const uint16_t expected_rgb565[8] = {
        0x0000, 0x1000, 0x2000, 0x3000,
        0x0000, 0x0002, 0x0004, 0x0006
    };
    const uint8_t mode_map = 0xb4;
    const uint8_t payload[18] = {
        1, 0x01, 0x23, 0x40, 0x12, 0x34, 0x01, 0x23, 0x40,
        2, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef
    };
    const uint8_t expected[2][BPV1_RECORD_BYTES] = {
        {BPV1_DIRECT_RECORD_FLAG | 1,
         0x01, 0x23, 0x40, 0x12, 0x34, 0x01, 0x23, 0x40},
        {BPV1_DIRECT_RECORD_FLAG | 2,
         0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}
    };
    FILE *file = tmpfile();
    BPV1Header header;
    BPV1Decoder *decoder = NULL;
    BPV1Packet packet;
    const BPV1Frame *frame = NULL;
    int result = 1;
    unsigned color;
    for (color = 0; color < 16U; ++color) {
        palette[((1U * 16U + color) * 3U)] =
            (uint8_t)(color << 4);
        palette[((2U * 16U + color) * 3U) + 2U] =
            (uint8_t)(color << 4);
    }
    if (!file ||
        fwrite("BPV1", 1, 4, file) != 4 ||
        write_u8(file, TEST_BPV1_V5_VERSION) ||
        write_u16(file, 8) || write_u16(file, 4) ||
        write_u32(file, 1) ||
        write_u16(file, 24) || write_u16(file, 1) ||
        write_u16(file, 1) ||
        write_u16(file, 8) || write_u16(file, 8) ||
        write_u8(file, 0) || write_u8(file, BPV1_AUDIO_NONE) ||
        write_u16(file, 0) || write_u8(file, 0) || write_u8(file, 0) ||
        write_u8(file, 1) ||
        write_u32(file, BPV1_MAX_PALETTE_BYTES +
                         1U + sizeof payload) ||
        write_u32(file, 1) ||
        write_u32(file, 0) ||
        fwrite(palette, 1, sizeof palette, file) != sizeof palette ||
        write_u8(file, mode_map) ||
        fwrite(payload, 1, sizeof payload, file) != sizeof payload ||
        fseek(file, 0, SEEK_SET) ||
        bpv1_header_read(file, &header) != BPV1_OK ||
        !(decoder = bpv1_decoder_create(&header)) ||
        bpv1_decoder_packet_capacity(decoder) !=
            BPV1_MAX_PALETTE_BYTES + 1U + 2U * 9U ||
        bpv1_decoder_read_packet(decoder, file, &packet) != BPV1_OK ||
        bpv1_decoder_decode(decoder, &packet, &frame) != BPV1_OK ||
        !frame ||
        memcmp(frame->blocks, expected, sizeof expected) ||
        bpv1_frame_render_rgb24_row(
            &header, frame, 0, rgb24, sizeof rgb24) != BPV1_OK ||
        memcmp(rgb24, expected_rgb24, sizeof expected_rgb24) ||
        bpv1_frame_render_rgb565_row(
            &header, frame, 0, rgb565, 8) != BPV1_OK ||
        memcmp(rgb565, expected_rgb565, sizeof expected_rgb565) ||
        bpv1_palette_build_rgb565(
            &header, frame, palette_rgb565,
            BPV1_MAX_PALETTE_COLORS) != BPV1_OK ||
        bpv1_frame_render_rgb565_row_cached(
            &header, frame, 0, palette_rgb565,
            BPV1_MAX_PALETTE_COLORS, cached_rgb565, 8) != BPV1_OK ||
        memcmp(cached_rgb565, expected_rgb565,
               sizeof expected_rgb565)) {
        fprintf(stderr, "BPV1 v5 direct RAW test failed\n");
        goto cleanup;
    }
    result = 0;

cleanup:
    bpv1_decoder_destroy(decoder);
    if (file) fclose(file);
    return result;
}

static int test_invalid_direct_raw(void) {
    uint8_t palette[BPV1_MAX_PALETTE_BYTES] = {0};
    const uint8_t payload[9] = {
        0, 0x01, 0x23, 0x01, 0x23, 0x01, 0x23, 0x01, 0x23
    };
    FILE *file = tmpfile();
    BPV1Header header;
    BPV1Decoder *decoder = NULL;
    BPV1Packet packet;
    const BPV1Frame *frame = NULL;
    int status = BPV1_OK;
    int result = 1;
    if (!file ||
        fwrite("BPV1", 1, 4, file) != 4 ||
        write_u8(file, TEST_BPV1_V5_VERSION) ||
        write_u16(file, 4) || write_u16(file, 4) ||
        write_u32(file, 1) ||
        write_u16(file, 24) || write_u16(file, 1) ||
        write_u16(file, 1) ||
        write_u16(file, 8) || write_u16(file, 8) ||
        write_u8(file, 0) || write_u8(file, BPV1_AUDIO_NONE) ||
        write_u16(file, 0) || write_u8(file, 0) || write_u8(file, 0) ||
        write_u8(file, 1) ||
        write_u32(file, BPV1_MAX_PALETTE_BYTES +
                         1U + sizeof payload) ||
        write_u32(file, 1) ||
        write_u32(file, 0) ||
        fwrite(palette, 1, sizeof palette, file) != sizeof palette ||
        write_u8(file, 5U << 5) ||
        fwrite(payload, 1, sizeof payload, file) != sizeof payload ||
        fseek(file, 0, SEEK_SET) ||
        bpv1_header_read(file, &header) != BPV1_OK ||
        !(decoder = bpv1_decoder_create(&header)) ||
        bpv1_decoder_read_packet(decoder, file, &packet) != BPV1_OK) {
        fprintf(stderr, "BPV1 invalid direct RAW setup failed\n");
        goto cleanup;
    }
    status = bpv1_decoder_decode(decoder, &packet, &frame);
    if (status != BPV1_ERR_DECODE) {
        fprintf(stderr, "BPV1 invalid direct RAW was accepted\n");
        goto cleanup;
    }
    result = 0;

cleanup:
    bpv1_decoder_destroy(decoder);
    if (file) fclose(file);
    return result;
}

static int test_v6_four_modes(void) {
    uint8_t palette[BPV1_MAX_PALETTE_BYTES] = {0};
    const uint8_t first_modes = 0xff;
    const uint8_t first_payload[22] = {
        0x00, 0x50,
        0x41, 0x23, 0xaa, 0x55,
        0x82, 0x45, 0x67, 0x1b, 0x1b, 0x1b, 0x1b,
        0xc3, 0x01, 0x23, 0x40, 0x12, 0x34, 0x01, 0x23, 0x40
    };
    const uint8_t second_modes = 0x1b;
    const uint8_t second_payload[5] = {
        0xf0,
        0x00, 0x00,
        0x04, 0x90
    };
    const uint8_t first_expected[4][BPV1_RECORD_BYTES] = {
        {0, 5, 0, 0, 0, 0, 0, 0, 0},
        {1, 2, 3, 0, 0, 0x44, 0x44, 0x11, 0x11},
        {2, 4, 5, 6, 7, 0x1b, 0x1b, 0x1b, 0x1b},
        {BPV1_DIRECT_RECORD_FLAG | 3,
         0x01, 0x23, 0x40, 0x12, 0x34, 0x01, 0x23, 0x40}
    };
    const uint8_t second_expected[4][BPV1_RECORD_BYTES] = {
        {0, 5, 0, 0, 0, 0, 0, 0, 0},
        {0, 5, 0, 0, 0, 0, 0, 0, 0},
        {0, 5, 0, 0, 0, 0, 0, 0, 0},
        {4, 9, 0, 0, 0, 0, 0, 0, 0}
    };
    FILE *file = tmpfile();
    BPV1Header header;
    BPV1Decoder *decoder = NULL;
    int frame_index;
    int result = 1;
    if (!file ||
        fwrite("BPV1", 1, 4, file) != 4 ||
        write_u8(file, TEST_BPV1_V6_VERSION) ||
        write_u16(file, 16) || write_u16(file, 4) ||
        write_u32(file, 2) ||
        write_u16(file, 24) || write_u16(file, 1) ||
        write_u16(file, 2) ||
        write_u16(file, 8) || write_u16(file, 0) ||
        write_u8(file, 7) || write_u8(file, BPV1_AUDIO_NONE) ||
        write_u16(file, 0) || write_u8(file, 0) || write_u8(file, 0) ||
        write_u8(file, 1) ||
        write_u32(file, BPV1_MAX_PALETTE_BYTES + 1U +
                         sizeof first_payload) ||
        write_u32(file, 1) || write_u32(file, 0) ||
        fwrite(palette, 1, sizeof palette, file) != sizeof palette ||
        write_u8(file, first_modes) ||
        fwrite(first_payload, 1, sizeof first_payload, file) !=
            sizeof first_payload ||
        write_u8(file, 0) ||
        write_u32(file, 1U + sizeof second_payload) ||
        write_u32(file, 1) || write_u32(file, 0) ||
        write_u8(file, second_modes) ||
        fwrite(second_payload, 1, sizeof second_payload, file) !=
            sizeof second_payload ||
        fseek(file, 0, SEEK_SET) ||
        bpv1_header_read(file, &header) != BPV1_OK ||
        header.version != TEST_BPV1_V6_VERSION ||
        header.max_pattern_dictionary != 0 ||
        !(decoder = bpv1_decoder_create(&header)) ||
        bpv1_decoder_packet_capacity(decoder) !=
            BPV1_MAX_PALETTE_BYTES + 1U + 4U * 9U) {
        fprintf(stderr, "BPV1 v6 four-mode setup failed\n");
        goto cleanup;
    }
    for (frame_index = 0; frame_index < 2; ++frame_index) {
        BPV1Packet packet;
        const BPV1Frame *frame = NULL;
        const uint8_t *expected =
            frame_index ? &second_expected[0][0] : &first_expected[0][0];
        if (bpv1_decoder_read_packet(decoder, file, &packet) != BPV1_OK ||
            packet.info.mode_bytes != 1U ||
            bpv1_decoder_decode(decoder, &packet, &frame) != BPV1_OK ||
            !frame ||
            memcmp(frame->blocks, expected,
                   4U * BPV1_RECORD_BYTES)) {
            fprintf(stderr, "BPV1 v6 frame %d failed\n", frame_index);
            goto cleanup;
        }
    }
    result = 0;

cleanup:
    bpv1_decoder_destroy(decoder);
    if (file) fclose(file);
    return result;
}

static int test_v7_pixel_motion_palette_remap(void) {
    uint8_t palette[BPV1_MAX_PALETTE_BYTES] = {0};
    const uint8_t first_modes = 0xf0;
    const uint8_t first_payload[4] = {0x00, 0x00, 0x01, 0x00};
    const uint8_t second_modes = 0x10;
    const uint8_t motion = 0xf0;
    const uint8_t expected[2][BPV1_RECORD_BYTES] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 1, 0, 0, 0x15, 0x15, 0x15, 0x15}
    };
    FILE *file = tmpfile();
    BPV1Header header;
    BPV1Decoder *decoder = NULL;
    BPV1Packet packet;
    const BPV1Frame *frame = NULL;
    unsigned color;
    int result = 1;
    for (color = 0; color < BPV1_COLORS_PER_PALETTE; ++color) {
        palette[color * 3U] =
            (uint8_t)(color == 0U ? 0U :
                      color == 1U ? 10U : 200U);
    }
    palette[BPV1_COLORS_PER_PALETTE * 3U] = 12;
    if (!file ||
        fwrite("BPV1", 1, 4, file) != 4 ||
        write_u8(file, BPV1_PIXEL_MOTION_VERSION) ||
        write_u16(file, 8) || write_u16(file, 4) ||
        write_u32(file, 2) ||
        write_u16(file, 24) || write_u16(file, 1) ||
        write_u16(file, 2) ||
        write_u16(file, 8) || write_u16(file, 0) ||
        write_u8(file, 2) || write_u8(file, BPV1_AUDIO_NONE) ||
        write_u16(file, 0) || write_u8(file, 0) || write_u8(file, 0) ||
        write_u8(file, 1) ||
        write_u32(file, BPV1_MAX_PALETTE_BYTES + 1U +
                         sizeof first_payload) ||
        write_u32(file, 1) || write_u32(file, 0) ||
        fwrite(palette, 1, sizeof palette, file) != sizeof palette ||
        write_u8(file, first_modes) ||
        fwrite(first_payload, 1, sizeof first_payload, file) !=
            sizeof first_payload ||
        write_u8(file, 0) ||
        write_u32(file, 2) ||
        write_u32(file, 1) || write_u32(file, 0) ||
        write_u8(file, second_modes) ||
        write_u8(file, motion) ||
        fseek(file, 0, SEEK_SET) ||
        bpv1_header_read(file, &header) != BPV1_OK ||
        header.version != BPV1_PIXEL_MOTION_VERSION ||
        !(decoder = bpv1_decoder_create(&header)) ||
        bpv1_decoder_read_packet(decoder, file, &packet) != BPV1_OK ||
        bpv1_decoder_decode(decoder, &packet, &frame) != BPV1_OK ||
        bpv1_decoder_read_packet(decoder, file, &packet) != BPV1_OK ||
        bpv1_decoder_decode(decoder, &packet, &frame) != BPV1_OK ||
        !frame ||
        memcmp(frame->blocks, expected, sizeof expected)) {
        fprintf(stderr, "BPV1 v7 pixel-motion palette remap failed\n");
        goto cleanup;
    }
    result = 0;

cleanup:
    bpv1_decoder_destroy(decoder);
    if (file) fclose(file);
    return result;
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
        uint16_t cached_row565[4];
        uint16_t rows565[16];
        uint16_t cached_rows565[16];
        uint16_t palette565[BPV1_MAX_PALETTE_COLORS];
        int x;
        if (bpv1_decoder_read_packet(decoder, file, &packet) != BPV1_OK ||
            bpv1_decoder_decode(decoder, &packet, &frame) != BPV1_OK ||
            !frame || frame->frame_index != (uint32_t)frame_index ||
            bpv1_frame_render_rgb24_row(
                &header, frame, 0, row, sizeof row) != BPV1_OK ||
            bpv1_frame_render_rgb565_row(
                &header, frame, 0, row565, 4) != BPV1_OK ||
            bpv1_frame_render_rgb565_rows(
                &header, frame, 0, 4, rows565, 4, 16) != BPV1_OK ||
            bpv1_palette_build_rgb565(
                &header, frame, palette565,
                BPV1_MAX_PALETTE_COLORS) != BPV1_OK ||
            bpv1_frame_render_rgb565_row_cached(
                &header, frame, 0, palette565,
                BPV1_MAX_PALETTE_COLORS, cached_row565, 4) != BPV1_OK ||
            bpv1_frame_render_rgb565_rows_cached(
                &header, frame, 0, 4, palette565,
                BPV1_MAX_PALETTE_COLORS, cached_rows565, 4, 16) !=
                BPV1_OK ||
            memcmp(row565, cached_row565, sizeof row565) ||
            memcmp(rows565, cached_rows565, sizeof rows565)) {
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
        for (x = 0; x < 16; ++x) {
            if (rows565[x] != 0xf800) {
                fprintf(stderr, "BPV1 multi-row rendered pixel mismatch\n");
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
    if (test_dictionary_eviction()) goto cleanup;
    if (test_audio_packet()) goto cleanup;
    if (test_active_palettes()) goto cleanup;
    if (test_adaptive_raw_records()) goto cleanup;
    if (test_direct_raw_records()) goto cleanup;
    if (test_invalid_direct_raw()) goto cleanup;
    if (test_v6_four_modes() ||
        test_v7_pixel_motion_palette_remap()) goto cleanup;
    result = 0;
    puts("BPV1 portable C decoder tests passed");

cleanup:
    bpv1_decoder_destroy(decoder);
    if (file) fclose(file);
    return result;
}
