/*
 * hlvbenchdec: repeatable decoder throughput and complexity estimator.
 *
 * Packets are loaded once, decoded repeatedly without display conversion, and
 * scored with conservative architecture-independent operation weights.  The
 * project target is 320x240 at 25 fps on a scalar 100 MHz processor.
 */
#include "hlv1.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct PacketList {
    HLV1Packet *items;
    size_t count;
    size_t capacity;
} PacketList;

static double now_sec(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
    return (double)clock() / CLOCKS_PER_SEC;
}

static void packet_list_free(PacketList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; ++i)
        hlv1_packet_free(&list->items[i]);
    free(list->items);
    memset(list, 0, sizeof *list);
}

static int packet_list_push(PacketList *list, HLV1Packet *packet) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 256;
        HLV1Packet *items = realloc(list->items, capacity * sizeof *items);
        if (!items) return HLV1_ERR_MEMORY;
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = *packet;
    memset(packet, 0, sizeof *packet);
    return HLV1_OK;
}

/* Convert operation counters to a deliberately pessimistic scalar cycle
 * estimate.  It is a regression guard, not a claim about one specific CPU. */
static uint64_t conservative_cycles(const HLV1Stats *s) {
    /* Deliberately conservative scalar weights. They are not tied to any
       instruction set; the purpose is to keep format changes under the
       QVGA/25 fps budget of 4 million cycles on a 100 MHz processor. */
    uint64_t cycles = 0;
    cycles += s->copied_samples * 2;
    cycles += s->interpolated_hv_samples * 8;
    cycles += s->interpolated_bilinear_samples * 14;
    cycles += s->intra_samples * 4;
    cycles += s->fill_samples * 2;
    cycles += s->palette_samples * 3;
    cycles += s->coefficient_symbols * 25;
    cycles += s->dc_only_blocks * 45;
    cycles += s->inverse_wht_blocks * 180;
    cycles += s->macroblocks * 100;
    cycles += s->motion_predictor_blocks * 20;
    cycles += (s->decoded_bits + 7) / 8 * 8;
    return cycles;
}

static void usage(const char *program) {
    fprintf(stderr, "Usage: %s input.hlv [loops]\n", program);
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        usage(argv[0]);
        return 2;
    }
    int loops = argc == 3 ? atoi(argv[2]) : 20;
    if (loops < 1 || loops > 100000) {
        fprintf(stderr, "Invalid loop count\n");
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

    PacketList packets = {0};
    for (;;) {
        HLV1Packet packet = {0};
        result = hlv1_packet_read(file, &packet);
        if (result == HLV1_EOF) break;
        if (result < 0 || packet_list_push(&packets, &packet) < 0) {
            fprintf(stderr, "Packet read: %s\n", hlv1_strerror(result));
            hlv1_packet_free(&packet);
            packet_list_free(&packets);
            fclose(file);
            return 1;
        }
    }
    fclose(file);
    if (!packets.count) {
        fprintf(stderr, "No frames\n");
        packet_list_free(&packets);
        return 1;
    }

    HLV1Stats one_pass = {0};
    const HLV1Frame *last = NULL;
    volatile uint64_t checksum = 0;
    double start = now_sec();
    for (int loop = 0; loop < loops; ++loop) {
        HLV1Decoder *decoder = hlv1_decoder_create(&header);
        if (!decoder) {
            fprintf(stderr, "Cannot allocate decoder\n");
            packet_list_free(&packets);
            return 1;
        }
        for (size_t i = 0; i < packets.count; ++i) {
            result = hlv1_decoder_decode(decoder, &packets.items[i], &last);
            if (result < 0) {
                fprintf(stderr, "Decode frame %zu: %s\n", i, hlv1_strerror(result));
                hlv1_decoder_destroy(decoder);
                packet_list_free(&packets);
                return 1;
            }
        }
        if (loop == 0) one_pass = *hlv1_decoder_stats(decoder);
        /* Prevent an over-aggressive optimizer from treating the decode as dead. */
        if (last) checksum += last->y[0];
        hlv1_decoder_destroy(decoder);
    }
    double elapsed = now_sec() - start;
    uint64_t total_frames = (uint64_t)packets.count * (uint64_t)loops;
    double fps = elapsed > 0.0 ? total_frames / elapsed : 0.0;
    uint64_t cycles = conservative_cycles(&one_pass);
    double cycles_per_frame = one_pass.frames ? (double)cycles / one_pass.frames : 0.0;
    double estimated_fps_100 = cycles_per_frame > 0.0 ? 100000000.0 / cycles_per_frame : 0.0;

    printf("HLV stream v%u, %ux%u, %zu frames, %d loops\n",
           header.version ? header.version : 1, header.width, header.height,
           packets.count, loops);
    printf("Native scalar decode: %.3f s, %.1f fps\n", elapsed, fps);
    printf("Work/frame: copy %.0f, interp-H/V %.0f, interp-2D %.0f, "
           "intra %.0f, palette %.0f, coeff %.1f, WHT %.1f\n",
           (double)one_pass.copied_samples / one_pass.frames,
           (double)one_pass.interpolated_hv_samples / one_pass.frames,
           (double)one_pass.interpolated_bilinear_samples / one_pass.frames,
           (double)one_pass.intra_samples / one_pass.frames,
           (double)one_pass.palette_samples / one_pass.frames,
           (double)one_pass.coefficient_symbols / one_pass.frames,
           (double)one_pass.inverse_wht_blocks / one_pass.frames);
    printf("Coefficient structure: single %.1f%%, two %.1f%%, run=0 %.1f%%, |level|=1 %.1f%%\n",
           one_pass.residual_blocks ? 100.0 * one_pass.single_coefficient_blocks / one_pass.residual_blocks : 0.0,
           one_pass.residual_blocks ? 100.0 * one_pass.two_coefficient_blocks / one_pass.residual_blocks : 0.0,
           one_pass.coefficient_symbols ? 100.0 * one_pass.run_zero_symbols / one_pass.coefficient_symbols : 0.0,
           one_pass.coefficient_symbols ? 100.0 * one_pass.unit_level_symbols / one_pass.coefficient_symbols : 0.0);
    printf("Conservative scalar estimate: %.0f cycles/frame, %.1f fps at 100 MHz\n",
           cycles_per_frame, estimated_fps_100);
    printf("QVGA 25 fps codec-core budget: %s (limit 4,000,000 cycles/frame)\n",
           cycles_per_frame <= 4000000.0 ? "PASS" : "FAIL");
    if (checksum == UINT64_MAX) fputc(0, stderr);

    packet_list_free(&packets);
    return 0;
}
