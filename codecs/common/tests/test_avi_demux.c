#include "avi_demux.h"

#include <inttypes.h>
#include <stdio.h>

static int test_finish_packet(void) {
    static const uint8_t bytes[] = {0, 1, 2, 3, 4, 5, 6, 7};
    AviDemuxPacket packet = {0};
    AviDemuxFinishStats stats;
    FILE *file = tmpfile();
    if (!file || fwrite(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) {
        if (file) fclose(file);
        return 1;
    }

    packet.next_offset = 5;
    avi_demux_finish_stats_reset();
    if (fseek(file, 5, SEEK_SET) != 0 ||
        avi_demux_finish_packet(file, &packet) != AVI_DEMUX_OK ||
        ftell(file) != 5) {
        fclose(file);
        return 1;
    }
    avi_demux_finish_stats_get(&stats);
    if (stats.cursor_matches != 1 || stats.sequential_single_bytes != 0 ||
        stats.fallback_seeks != 0) {
        fclose(file);
        return 1;
    }

    avi_demux_finish_stats_reset();
    if (fseek(file, 4, SEEK_SET) != 0 ||
        avi_demux_finish_packet(file, &packet) != AVI_DEMUX_OK ||
        ftell(file) != 5) {
        fclose(file);
        return 1;
    }
    avi_demux_finish_stats_get(&stats);
    if (stats.cursor_matches != 0 || stats.sequential_single_bytes != 1 ||
        stats.fallback_seeks != 0) {
        fclose(file);
        return 1;
    }

    avi_demux_finish_stats_reset();
    if (fseek(file, 2, SEEK_SET) != 0 ||
        avi_demux_finish_packet(file, &packet) != AVI_DEMUX_OK ||
        ftell(file) != 5) {
        fclose(file);
        return 1;
    }
    avi_demux_finish_stats_get(&stats);
    if (stats.cursor_matches != 0 || stats.sequential_single_bytes != 0 ||
        stats.fallback_seeks != 1) {
        fclose(file);
        return 1;
    }
    fclose(file);

    file = tmpfile();
    if (!file || fwrite(bytes, 1, 4, file) != 4 ||
        fseek(file, 4, SEEK_SET) != 0 ||
        avi_demux_finish_packet(file, &packet) != AVI_DEMUX_ERR_IO) {
        if (file) fclose(file);
        return 1;
    }
    fclose(file);
    return 0;
}

static int scan_packets(FILE *file, const AviDemuxInfo *info,
                        AviDemuxPacketKind kind, uint32_t *count,
                        uint32_t *maximum) {
    int result;
    *count = 0;
    *maximum = 0;
    if (fseek(file, info->movi_start, SEEK_SET) != 0)
        return AVI_DEMUX_ERR_IO;
    for (;;) {
        AviDemuxPacket packet;
        result = avi_demux_next_packet(file, info, kind, &packet);
        if (result == AVI_DEMUX_EOF) return AVI_DEMUX_OK;
        if (result != AVI_DEMUX_OK) return result;
        if (packet.payload_offset < info->movi_start ||
            packet.next_offset > info->movi_end ||
            packet.next_offset <= packet.payload_offset)
            return AVI_DEMUX_ERR_FORMAT;
        if (packet.payload_size > *maximum) *maximum = packet.payload_size;
        ++*count;
        result = avi_demux_finish_packet(file, &packet);
        if (result != AVI_DEMUX_OK) return result;
    }
}

static int validate_file(const char *path) {
    AviDemuxInfo info;
    FILE *file = fopen(path, "rb");
    uint32_t video_packets;
    uint32_t audio_packets;
    uint32_t maximum_video;
    uint32_t maximum_audio;
    int result;
    if (!file) {
        fprintf(stderr, "%s: cannot open\n", path);
        return 1;
    }
    result = avi_demux_read_info(file, &info);
    if (result != AVI_DEMUX_OK) {
        fprintf(stderr, "%s: %s\n", path, avi_demux_strerror(result));
        fclose(file);
        return 1;
    }
    result = scan_packets(file, &info, AVI_DEMUX_PACKET_VIDEO,
                          &video_packets, &maximum_video);
    if (result != AVI_DEMUX_OK || !video_packets) {
        fprintf(stderr, "%s: video packet scan failed: %s\n", path,
                avi_demux_strerror(result));
        fclose(file);
        return 1;
    }
    audio_packets = 0;
    maximum_audio = 0;
    if (info.audio.stream_index != AVI_DEMUX_NO_STREAM) {
        result = scan_packets(file, &info, AVI_DEMUX_PACKET_AUDIO,
                              &audio_packets, &maximum_audio);
        if (result != AVI_DEMUX_OK || !audio_packets) {
            fprintf(stderr, "%s: audio packet scan failed: %s\n", path,
                    avi_demux_strerror(result));
            fclose(file);
            return 1;
        }
    }
    if (info.video.max_packet_size &&
        info.video.max_packet_size < maximum_video) {
        fprintf(stderr, "%s: declared max video packet is too small\n", path);
        fclose(file);
        return 1;
    }
    printf("%s: video=%" PRIu32 " max=%" PRIu32
           " audio=%" PRIu32 " max=%" PRIu32 "\n",
           path, video_packets, maximum_video, audio_packets, maximum_audio);
    fclose(file);
    return 0;
}

int main(int argc, char **argv) {
    int failures = 0;
    int index;
    if (test_finish_packet()) {
        fprintf(stderr, "AVI packet completion regression failed\n");
        return 1;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: test_avi_demux FILE.avi [...]\n");
        return 2;
    }
    for (index = 1; index < argc; ++index)
        failures += validate_file(argv[index]);
    return failures ? 1 : 0;
}
