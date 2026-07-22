/*
 * Negative tests for malformed headers, truncated packets, invalid reference
 * order, range checks, and CRC rejection.  A decoder must fail cleanly without
 * committing a partially reconstructed frame as the next reference.
 */
#include "hlv1.h"

#include <stdio.h>
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
    HLV1Packet read_packet;
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
