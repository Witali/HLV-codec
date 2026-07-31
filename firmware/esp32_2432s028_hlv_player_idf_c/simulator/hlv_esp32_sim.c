/* Host-side performance and correctness harness for the ESP32 compact
 * decoder. It deliberately uses the segmented packet API and the same
 * 16 x 7680-byte validation view alongside the firmware's refill API,
 * without copying packet data while the timed decoder loop is running. */
#include "hlv1.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

enum {
    PACKET_BLOCK_COUNT = 16,
    PACKET_BLOCK_BYTES = 7680
};

typedef struct PacketList {
    HLV1Packet *packets;
    size_t count;
    size_t capacity;
} PacketList;

static double now_seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    return (double)clock() / CLOCKS_PER_SEC;
#endif
}

static uint64_t hash_bytes(uint64_t hash, const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_frame(uint64_t hash, const HLV1Frame *frame) {
    if (frame->storage_mode == HLV1_FRAME_STORAGE_Y7_U6_V6) {
        for (int y = 0; y < frame->padded_height; ++y)
            for (int x = 0; x < frame->padded_width; ++x) {
                uint8_t sample = hlv1_frame_y_sample(frame, x, y);
                hash = hash_bytes(hash, &sample, 1);
            }
        for (int y = 0; y < frame->padded_height / 2; ++y)
            for (int x = 0; x < frame->padded_width / 2; ++x) {
                uint8_t sample = hlv1_frame_u_sample(frame, x, y);
                hash = hash_bytes(hash, &sample, 1);
            }
        for (int y = 0; y < frame->padded_height / 2; ++y)
            for (int x = 0; x < frame->padded_width / 2; ++x) {
                uint8_t sample = hlv1_frame_v_sample(frame, x, y);
                hash = hash_bytes(hash, &sample, 1);
            }
        return hash;
    }
    size_t y_size = (size_t)frame->stride_y * frame->padded_height;
    size_t c_height = (size_t)frame->padded_height / 2U;
    hash = hash_bytes(hash, frame->y, y_size);
    hash = hash_bytes(hash, frame->u, (size_t)frame->stride_u * c_height);
    return hash_bytes(hash, frame->v, (size_t)frame->stride_v * c_height);
}

static void packet_list_free(PacketList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; ++i)
        hlv1_packet_free(&list->packets[i]);
    free(list->packets);
    memset(list, 0, sizeof *list);
}

static int packet_list_push(PacketList *list, HLV1Packet *packet) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2U : 256U;
        HLV1Packet *packets = (HLV1Packet *)realloc(
            list->packets, capacity * sizeof *packets);
        if (!packets) return HLV1_ERR_MEMORY;
        list->packets = packets;
        list->capacity = capacity;
    }
    list->packets[list->count++] = *packet;
    memset(packet, 0, sizeof *packet);
    return HLV1_OK;
}

static int segmented_view(const HLV1Packet *source, HLV1Packet *view,
                          uint8_t *blocks[PACKET_BLOCK_COUNT]) {
    if (!source || !view || !source->payload) return HLV1_ERR_ARGUMENT;
    size_t count = ((size_t)source->payload_size + PACKET_BLOCK_BYTES - 1U) /
                   PACKET_BLOCK_BYTES;
    if (count > PACKET_BLOCK_COUNT) return HLV1_ERR_MEMORY;
    for (size_t i = 0; i < count; ++i)
        blocks[i] = source->payload + i * PACKET_BLOCK_BYTES;
    *view = *source;
    view->payload = NULL;
    view->payload_blocks = blocks;
    view->payload_block_count = count;
    view->payload_block_size = PACKET_BLOCK_BYTES;
    return HLV1_OK;
}

static int decode_pass(const HLV1Header *header, const PacketList *list,
                       int compact, int compute_hash, uint64_t *hash,
                       uint64_t *guard, HLV1Stats *stats) {
    HLV1Decoder *decoder =
        compact == 2
            ? hlv1_decoder_create_y7_u6_v6_single_reference(header)
            : (compact
                   ? hlv1_decoder_create_y7_u6_v6(header)
                   : hlv1_decoder_create(header));
    if (!decoder) return HLV1_ERR_MEMORY;
    uint64_t local_hash = UINT64_C(14695981039346656037);
    const HLV1Frame *frame = NULL;
    int result = HLV1_OK;
    for (size_t i = 0; i < list->count; ++i) {
        HLV1Packet view;
        uint8_t *blocks[PACKET_BLOCK_COUNT] = {0};
        result = segmented_view(&list->packets[i], &view, blocks);
        if (result < 0) break;
        result = hlv1_decoder_decode_blocks(decoder, &view, &frame);
        if (result < 0) break;
        if (compute_hash) local_hash = hash_frame(local_hash, frame);
    }
    if (stats) *stats = *hlv1_decoder_stats(decoder);
    if (frame && guard) *guard += frame->y[0];
    if (hash) *hash = local_hash;
    hlv1_decoder_destroy(decoder);
    return result;
}

static int decode_file_pass(const char *path, int single_reference,
                            uint64_t *hash) {
    FILE *file = fopen(path, "rb");
    if (!file) return HLV1_ERR_IO;
    HLV1Header header;
    int result = hlv1_header_read(file, &header);
    HLV1Decoder *decoder =
        result < 0
            ? NULL
            : (single_reference
                   ? hlv1_decoder_create_y7_u6_v6_single_reference(&header)
                   : hlv1_decoder_create_y7_u6_v6(&header));
    if (result >= 0 && !decoder) result = HLV1_ERR_MEMORY;
    uint8_t buffer[257];
    uint64_t local_hash = UINT64_C(14695981039346656037);
    while (result >= 0) {
        const HLV1Frame *frame = NULL;
        result = hlv1_decoder_decode_file(
            decoder, file, buffer, sizeof buffer, NULL, &frame);
        if (result == HLV1_EOF) {
            result = HLV1_OK;
            break;
        }
        if (result >= 0) local_hash = hash_frame(local_hash, frame);
    }
    if (hash) *hash = local_hash;
    hlv1_decoder_destroy(decoder);
    fclose(file);
    return result;
}

static int verify_compact_expanded(const HLV1Header *header,
                                   const PacketList *list) {
    HLV1Decoder *compact = hlv1_decoder_create_y7_u6_v6(header);
    HLV1Decoder *expanded = hlv1_decoder_create(header);
    if (!compact || !expanded) {
        hlv1_decoder_destroy(compact);
        hlv1_decoder_destroy(expanded);
        return HLV1_ERR_MEMORY;
    }
    int result = HLV1_OK;
    for (size_t i = 0; i < list->count; ++i) {
        HLV1Packet view;
        uint8_t *blocks[PACKET_BLOCK_COUNT] = {0};
        result = segmented_view(&list->packets[i], &view, blocks);
        const HLV1Frame *packed_frame = NULL;
        const HLV1Frame *expanded_frame = NULL;
        if (result >= 0)
            result = hlv1_decoder_decode_blocks(
                compact, &view, &packed_frame);
        if (result >= 0)
            result = hlv1_decoder_decode(
                expanded, &list->packets[i], &expanded_frame);
        if (result < 0) break;
        for (int y = 0; y < expanded_frame->padded_height; ++y)
            for (int x = 0; x < expanded_frame->padded_width; ++x)
                if (hlv1_frame_y_sample(packed_frame, x, y) !=
                    expanded_frame->y[y * expanded_frame->stride_y + x]) {
                    fprintf(stderr,
                            "First mismatch: frame %zu Y(%d,%d): %u != %u\n",
                            i, x, y,
                            hlv1_frame_y_sample(packed_frame, x, y),
                            expanded_frame->y[
                                y * expanded_frame->stride_y + x]);
                    result = HLV1_ERR_BITSTREAM;
                    goto done;
                }
        for (int y = 0; y < expanded_frame->padded_height / 2; ++y)
            for (int x = 0; x < expanded_frame->padded_width / 2; ++x) {
                uint8_t packed_u = hlv1_frame_u_sample(packed_frame, x, y);
                uint8_t expanded_u =
                    expanded_frame->u[y * expanded_frame->stride_u + x];
                uint8_t packed_v = hlv1_frame_v_sample(packed_frame, x, y);
                uint8_t expanded_v =
                    expanded_frame->v[y * expanded_frame->stride_v + x];
                if (packed_u != expanded_u || packed_v != expanded_v) {
                    fprintf(stderr,
                            "First mismatch: frame %zu UV(%d,%d): "
                            "%u/%u != %u/%u\n",
                            i, x, y, packed_u, packed_v,
                            expanded_u, expanded_v);
                    result = HLV1_ERR_BITSTREAM;
                    goto done;
                }
            }
    }
done:
    hlv1_decoder_destroy(compact);
    hlv1_decoder_destroy(expanded);
    return result;
}

static size_t compact_frame_working_bytes(const HLV1Header *header) {
    size_t width = ((size_t)header->width + 15U) & ~(size_t)15U;
    size_t height = ((size_t)header->height + 15U) & ~(size_t)15U;
    size_t packed_frame = width * HLV1_V14_LUMA_BITS / 8U * height +
                          2U * (width / 2U * HLV1_V14_CHROMA_BITS / 8U) *
                              (height / 2U);
    size_t corrections =
        width / 8U * (height / 8U) +
        2U * (width / 16U) * (height / 16U);
    size_t working_rows = width * 16U + 2U * (width / 2U) * 8U;
    return 2U * (packed_frame + corrections) + working_rows;
}

static size_t single_reference_working_bytes(const HLV1Header *header) {
    size_t width = ((size_t)header->width + 15U) & ~(size_t)15U;
    size_t height = ((size_t)header->height + 15U) & ~(size_t)15U;
    size_t rows =
        height < HLV1_SINGLE_REFERENCE_LUMA_ROWS
            ? height
            : HLV1_SINGLE_REFERENCE_LUMA_ROWS;
    size_t packed_frame = width * HLV1_V14_LUMA_BITS / 8U * height +
                          2U * (width / 2U * HLV1_V14_CHROMA_BITS / 8U) *
                              (height / 2U);
    size_t corrections =
        width / 8U * (height / 8U) +
        2U * (width / 16U) * (height / 16U);
    size_t rolling_rows =
        width * HLV1_V14_LUMA_BITS / 8U * rows +
        2U * (width / 2U * HLV1_V14_CHROMA_BITS / 8U) * (rows / 2U) +
        width / 8U * (rows / 8U) +
        2U * (width / 16U) * (rows / 16U);
    size_t working_rows = width * 16U + 2U * (width / 2U) * 8U;
    return packed_frame + corrections + rolling_rows + working_rows;
}

static void usage(const char *program) {
    fprintf(stderr, "Usage: %s INPUT.hlv [LOOPS]\n", program);
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        usage(argv[0]);
        return 2;
    }
    int loops = argc == 3 ? atoi(argv[2]) : 3;
    if (loops < 1 || loops > 1000) {
        usage(argv[0]);
        return 2;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        perror(argv[1]);
        return 1;
    }
    HLV1Header header;
    int result = hlv1_header_read(file, &header);
    if (result < 0) {
        fprintf(stderr, "Header: %s\n", hlv1_strerror(result));
        fclose(file);
        return 1;
    }

    PacketList list = {0};
    size_t maximum_packet = 0;
    for (;;) {
        HLV1Packet packet = {0};
        result = hlv1_packet_read(file, &packet);
        if (result == HLV1_EOF) break;
        if (result < 0 || packet_list_push(&list, &packet) < 0) {
            fprintf(stderr, "Packet read: %s\n", hlv1_strerror(result));
            hlv1_packet_free(&packet);
            packet_list_free(&list);
            fclose(file);
            return 1;
        }
        if (list.packets[list.count - 1].payload_size > maximum_packet)
            maximum_packet = list.packets[list.count - 1].payload_size;
    }
    fclose(file);
    if (!list.count || maximum_packet > PACKET_BLOCK_COUNT * PACKET_BLOCK_BYTES) {
        fprintf(stderr, "Input does not fit the segmented validation view\n");
        packet_list_free(&list);
        return 1;
    }

    uint64_t frame_hash = 0;
    uint64_t expanded_hash = 0;
    uint64_t streamed_hash = 0;
    uint64_t single_hash = 0;
    uint64_t single_streamed_hash = 0;
    uint64_t guard = 0;
    HLV1Stats stats = {0};
    result = decode_pass(&header, &list, 1, 1, &frame_hash, &guard, &stats);
    if (result < 0) {
        fprintf(stderr, "Verification decode: %s\n", hlv1_strerror(result));
        packet_list_free(&list);
        return 1;
    }
    result = decode_pass(
        &header, &list, 0, 1, &expanded_hash, &guard, NULL);
    if (result < 0) {
        fprintf(stderr, "Expanded verification decode: %s\n",
                hlv1_strerror(result));
        packet_list_free(&list);
        return 1;
    }
    if (frame_hash != expanded_hash) {
        verify_compact_expanded(&header, &list);
        fprintf(stderr,
                "Compact/expanded reconstruction mismatch: %016" PRIx64
                " != %016" PRIx64 "\n",
                frame_hash, expanded_hash);
        packet_list_free(&list);
        return 1;
    }
    result = decode_file_pass(argv[1], 0, &streamed_hash);
    if (result < 0 || streamed_hash != frame_hash) {
        fprintf(stderr,
                "Streamed reconstruction mismatch: %s, %016" PRIx64
                " != %016" PRIx64 "\n",
                hlv1_strerror(result), streamed_hash, frame_hash);
        packet_list_free(&list);
        return 1;
    }
    result = decode_pass(
        &header, &list, 2, 1, &single_hash, &guard, NULL);
    if (result < 0 || single_hash != frame_hash) {
        fprintf(stderr,
                "Single-reference reconstruction mismatch: %s, %016" PRIx64
                " != %016" PRIx64 "\n",
                hlv1_strerror(result), single_hash, frame_hash);
        packet_list_free(&list);
        return 1;
    }
    result = decode_file_pass(argv[1], 1, &single_streamed_hash);
    if (result < 0 || single_streamed_hash != frame_hash) {
        fprintf(stderr,
                "Single-reference streamed reconstruction mismatch: %s, "
                "%016" PRIx64 " != %016" PRIx64 "\n",
                hlv1_strerror(result), single_streamed_hash, frame_hash);
        packet_list_free(&list);
        return 1;
    }

    double start = now_seconds();
    for (int loop = 0; loop < loops; ++loop) {
        result = decode_pass(&header, &list, 1, 0, NULL, &guard, NULL);
        if (result < 0) break;
    }
    double elapsed = now_seconds() - start;
    uint64_t timed_frames = (uint64_t)list.count * (uint64_t)loops;
    double fps = elapsed > 0.0 ? (double)timed_frames / elapsed : 0.0;
    double microseconds = timed_frames
        ? elapsed * 1000000.0 / (double)timed_frames : 0.0;

    printf("ESP32 compact simulator: HLV v%u, %ux%u, %zu frames\n",
           header.version ? header.version : 1, header.width, header.height,
           list.count);
    printf("Packet view: %u x %u bytes, maximum packet %zu bytes\n",
           PACKET_BLOCK_COUNT, PACKET_BLOCK_BYTES, maximum_packet);
    printf("Frame storage + working rows: %zu bytes\n",
           compact_frame_working_bytes(&header));
    printf("Single reference + rolling rows: %zu bytes\n",
           single_reference_working_bytes(&header));
    printf("Reconstruction hash: %016" PRIx64 "\n", frame_hash);
    printf("Compact/expanded reconstruction: bit exact\n");
    printf("257-byte refill reconstruction: bit exact\n");
    printf("Single-reference segmented/refill reconstruction: bit exact\n");
    printf("Timed decode: %.3f s, %.1f fps, %.2f us/frame (%d loop%s)\n",
           elapsed, fps, microseconds, loops, loops == 1 ? "" : "s");
    if (stats.frames) {
        printf("Modes/frame: skip %.2f (%" PRIu64 " runs), inter %.2f, global %.2f, split %.2f, joint %.2f, rect %.2f, "
               "fill %.2f, palette %.2f (2/4/8 %.2f/%.2f/%.2f), "
               "literal %.2f; "
               "coeff %.1f, WHT %.1f\n",
               (double)stats.skipped / stats.frames,
               stats.skip_runs,
               (double)stats.inter / stats.frames,
               (double)stats.global / stats.frames,
               (double)stats.split_inter / stats.frames,
               (double)stats.split_joint / stats.frames,
               (double)stats.rect_split / stats.frames,
               (double)stats.fill / stats.frames,
               (double)stats.palette / stats.frames,
               (double)stats.palette_2 / stats.frames,
               (double)stats.palette_4 / stats.frames,
               (double)stats.palette_8 / stats.frames,
               (double)stats.literal / stats.frames,
               (double)stats.coefficient_symbols / stats.frames,
               (double)stats.inverse_wht_blocks / stats.frames);
        printf("Residual blocks: %" PRIu64 ", zero %" PRIu64
               ", DC-only %" PRIu64 ", single %" PRIu64
               ", two %" PRIu64 ", WHT %" PRIu64 "\n",
               stats.residual_blocks, stats.zero_residual_blocks,
               stats.dc_only_blocks, stats.single_coefficient_blocks,
               stats.two_coefficient_blocks, stats.inverse_wht_blocks);
    }
    if (guard == UINT64_MAX) fputc(0, stderr);
    packet_list_free(&list);
    return result < 0 ? 1 : 0;
}
