/* hlvinfo: print sequence metadata and aggregate packet statistics. */
#include "hlv1.h"
#include <inttypes.h>

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: %s input.hlv\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    HLV1Header h; int r = hlv1_header_read(f, &h);
    if (r < 0) { fprintf(stderr, "%s\n", hlv1_strerror(r)); return 1; }
    uint64_t payload = 0, video_payload = 0, audio_payload = 0;
    uint64_t packets = 0, keys = 0;
    for (;;) {
        HLV1Packet p; r = hlv1_packet_read(f, &p);
        if (r == HLV1_EOF) break;
        if (r < 0) { fprintf(stderr, "Frame %" PRIu64 ": %s\n", packets, hlv1_strerror(r)); return 1; }
        payload += p.payload_size;
        video_payload += hlv1_packet_video_payload_size(&p);
        audio_payload += hlv1_packet_audio_size(&p);
        keys += p.frame_type == HLV1_FRAME_KEY;
        ++packets;
        hlv1_packet_free(&p);
    }
    printf("HLV-1 stream v%u\n%ux%u  %u/%u fps\nDeclared frames: %u\nPackets: %" PRIu64 "\nKeyframes: %" PRIu64 "\nGOP: %u\nQuality: %u\nSearch: %u\nVideo payload: %" PRIu64 " bytes\nAudio payload: %" PRIu64 " bytes\nPayload: %" PRIu64 " bytes\n",
           h.version ? h.version : 1,h.width,h.height,h.fps_num,h.fps_den,
           h.frame_count,packets,keys,h.gop,h.quality,h.search_radius,
           video_payload,audio_payload,payload);
    if (h.flags & HLV1_FLAG_AUDIO)
        printf("Audio: %s mono %u Hz\n",
               h.audio_codec == HLV1_AUDIO_IMA_ADPCM
                   ? "IMA_ADPCM" : "PCM_U8",
               h.audio_sample_rate);
    fclose(f); return 0;
}
