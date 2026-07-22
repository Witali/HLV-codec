/* Host-side performance and correctness harness for the ESP32 compact
 * decoder. It deliberately uses the segmented packet API and the same
 * 8 x 7680-byte view as the firmware, without copying packet data while the
 * timed decoder loop is running. */
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
    PACKET_BLOCK_COUNT = 8,
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
                       int compute_hash, uint64_t *hash,
                       uint64_t *guard, HLV1Stats *stats) {
    HLV1Decoder *decoder = hlv1_decoder_create_y6_u5_v5(header);
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

static size_t compact_frame_working_bytes(const HLV1Header *header) {
    size_t width = ((size_t)header->width + 15U) & ~(size_t)15U;
    size_t height = ((size_t)header->height + 15U) & ~(size_t)15U;
    size_t packed_frame = width * 6U / 8U * height +
                          2U * (width / 2U * 5U / 8U) * (height / 2U);
    size_t working_rows = width * 16U + 2U * (width / 2U) * 8U;
    return 2U * packed_frame + working_rows;
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
        fprintf(stderr, "Input does not fit the ESP32 packet pool\n");
        packet_list_free(&list);
        return 1;
    }

    uint64_t frame_hash = 0;
    uint64_t guard = 0;
    HLV1Stats stats = {0};
    result = decode_pass(&header, &list, 1, &frame_hash, &guard, &stats);
    if (result < 0) {
        fprintf(stderr, "Verification decode: %s\n", hlv1_strerror(result));
        packet_list_free(&list);
        return 1;
    }

    double start = now_seconds();
    for (int loop = 0; loop < loops; ++loop) {
        result = decode_pass(&header, &list, 0, NULL, &guard, NULL);
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
    printf("Reconstruction hash: %016" PRIx64 "\n", frame_hash);
    printf("Timed decode: %.3f s, %.1f fps, %.2f us/frame (%d loop%s)\n",
           elapsed, fps, microseconds, loops, loops == 1 ? "" : "s");
    if (stats.frames) {
        printf("Modes/frame: skip %.2f, inter %.2f, global %.2f, split %.2f; "
               "coeff %.1f, WHT %.1f\n",
               (double)stats.skipped / stats.frames,
               (double)stats.inter / stats.frames,
               (double)stats.global / stats.frames,
               (double)stats.split_inter / stats.frames,
               (double)stats.coefficient_symbols / stats.frames,
               (double)stats.inverse_wht_blocks / stats.frames);
    }
    if (guard == UINT64_MAX) fputc(0, stderr);
    packet_list_free(&list);
    return result < 0 ? 1 : 0;
}
