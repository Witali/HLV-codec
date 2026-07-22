/*
 * Negative tests for malformed headers, truncated packets, invalid reference
 * order, range checks, and CRC rejection.  A decoder must fail cleanly without
 * committing a partially reconstructed frame as the next reference.
 */
#include "hlv1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static HLV1Header test_header(void) {
    HLV1Header h;
    memset(&h, 0, sizeof h);
    h.width = 16;
    h.height = 16;
    h.fps_num = 25;
    h.fps_den = 1;
    h.gop = 30;
    h.quality = 55;
    h.search_radius = 4;
    h.version = HLV1_VERSION;
    return h;
}

int main(void) {
    HLV1Header h = test_header(), got;

    FILE *f = tmpfile();
    CHECK(f != NULL);
    CHECK(hlv1_header_write(f, &h) == HLV1_OK);
    rewind(f);
    CHECK(hlv1_header_read(f, &got) == HLV1_OK);
    CHECK(got.width == h.width && got.height == h.height && got.version == h.version);
    fclose(f);

    h.flags = HLV1_FLAG_AUDIO;
    h.audio_codec = HLV1_AUDIO_PCM_U8;
    h.audio_sample_rate = 16000;
    h.audio_channels = 1;
    f = tmpfile();
    CHECK(f != NULL);
    CHECK(hlv1_header_write(f, &h) == HLV1_OK);
    rewind(f);
    CHECK(hlv1_header_read(f, &got) == HLV1_OK);
    CHECK(got.flags == h.flags && got.audio_codec == h.audio_codec &&
          got.audio_sample_rate == h.audio_sample_rate &&
          got.audio_channels == h.audio_channels);
    fclose(f);

    HLV1Header invalid_audio = test_header();
    invalid_audio.flags = HLV1_FLAG_AUDIO;
    invalid_audio.audio_codec = HLV1_AUDIO_PCM_U8;
    invalid_audio.audio_channels = 1;
    f = tmpfile();
    CHECK(f != NULL);
    CHECK(hlv1_header_write(f, &invalid_audio) == HLV1_ERR_ARGUMENT);
    fclose(f);

    const unsigned char video_bytes[] = {0x12, 0x34, 0x56, 0x78};
    const unsigned char audio[] = {0x00, 0x40, 0x80, 0xC0, 0xFF};
    HLV1Packet read_packet;
    HLV1Packet audio_packet;
    memset(&audio_packet, 0, sizeof audio_packet);
    audio_packet.frame_type = HLV1_FRAME_KEY;
    audio_packet.q_y = 8;
    audio_packet.q_uv = 10;
    audio_packet.bit_length = 29;
    audio_packet.payload_size = 4;
    audio_packet.payload = (unsigned char *)malloc(4);
    CHECK(audio_packet.payload != NULL);
    memcpy(audio_packet.payload, video_bytes, 4);
    CHECK(hlv1_packet_append_audio(&audio_packet, audio, sizeof audio) == HLV1_OK);
    CHECK(hlv1_packet_video_payload_size(&audio_packet) == 4);
    CHECK(hlv1_packet_audio_size(&audio_packet) == sizeof audio);
    CHECK(!memcmp(hlv1_packet_audio_data(&audio_packet), audio, sizeof audio));
    f = tmpfile();
    CHECK(f != NULL);
    CHECK(hlv1_packet_write(f, &audio_packet) == HLV1_OK);
    rewind(f);
    CHECK(hlv1_packet_read(f, &read_packet) == HLV1_OK);
    CHECK(hlv1_packet_video_payload_size(&read_packet) == 4);
    CHECK(hlv1_packet_audio_size(&read_packet) == sizeof audio);
    CHECK(!memcmp(hlv1_packet_audio_data(&read_packet), audio, sizeof audio));
    hlv1_packet_free(&read_packet);
    hlv1_packet_free(&audio_packet);
    fclose(f);

    f = tmpfile();
    CHECK(f != NULL);
    unsigned char bad_header[HLV1_HEADER_SIZE] = {0};
    CHECK(fwrite(bad_header, 1, sizeof bad_header, f) == sizeof bad_header);
    rewind(f);
    CHECK(hlv1_header_read(f, &got) == HLV1_ERR_FORMAT);
    fclose(f);

    const unsigned char payload[] = {0x12, 0x34, 0x56, 0x78};
    HLV1Packet p;
    memset(&p, 0, sizeof p);
    p.frame_type = HLV1_FRAME_KEY;
    p.q_y = 8;
    p.q_uv = 10;
    p.bit_length = 32;
    p.payload_size = sizeof payload;
    p.payload = (unsigned char *)payload;

    f = tmpfile();
    CHECK(f != NULL);
    CHECK(hlv1_packet_write(f, &p) == HLV1_OK);
    CHECK(fseek(f, HLV1_FRAME_HEADER_SIZE + 1, SEEK_SET) == 0);
    CHECK(fputc(0xFF, f) != EOF);
    fflush(f);
    rewind(f);
    CHECK(hlv1_packet_read(f, &read_packet) == HLV1_ERR_CRC);
    fclose(f);

    f = tmpfile();
    CHECK(f != NULL);
    CHECK(fwrite("FRM1", 1, 4, f) == 4);
    rewind(f);
    CHECK(hlv1_packet_read(f, &read_packet) == HLV1_ERR_IO);
    fclose(f);

    HLV1Decoder *d = hlv1_decoder_create(&h);
    CHECK(d != NULL);
    HLV1Packet first_p;
    memset(&first_p, 0, sizeof first_p);
    first_p.frame_type = HLV1_FRAME_P;
    first_p.q_y = 8;
    first_p.q_uv = 10;
    const HLV1Frame *frame = NULL;
    CHECK(hlv1_decoder_decode(d, &first_p, &frame) == HLV1_ERR_FORMAT);
    hlv1_decoder_destroy(d);

    d = hlv1_decoder_create(&h);
    CHECK(d != NULL);
    HLV1Packet empty_key;
    memset(&empty_key, 0, sizeof empty_key);
    empty_key.frame_type = HLV1_FRAME_KEY;
    empty_key.q_y = 8;
    empty_key.q_uv = 10;
    CHECK(hlv1_decoder_decode(d, &empty_key, &frame) == HLV1_ERR_BITSTREAM);
    hlv1_decoder_destroy(d);

    puts("HLV-1 malformed input and CRC tests: PASS");
    return 0;
}
