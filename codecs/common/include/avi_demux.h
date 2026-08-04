#ifndef HLV_AVI_DEMUX_H
#define HLV_AVI_DEMUX_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AVI_DEMUX_NO_STREAM 0xffU
#define AVI_DEMUX_MAX_VIDEO_FORMAT_EXTRA 256U

enum {
    AVI_DEMUX_OK = 0,
    AVI_DEMUX_EOF = 1,
    AVI_DEMUX_ERR_ARGUMENT = -1,
    AVI_DEMUX_ERR_IO = -2,
    AVI_DEMUX_ERR_FORMAT = -3,
    AVI_DEMUX_ERR_RANGE = -4
};

typedef enum AviDemuxPacketKind {
    AVI_DEMUX_PACKET_VIDEO = 1,
    AVI_DEMUX_PACKET_AUDIO = 2
} AviDemuxPacketKind;

typedef struct AviDemuxVideoInfo {
    uint32_t handler_fourcc;
    uint32_t compression_fourcc;
    uint32_t scale;
    uint32_t rate;
    uint32_t frame_count;
    uint32_t suggested_buffer_size;
    uint32_t max_packet_size;
    uint16_t width;
    uint16_t height;
    uint16_t format_extra_size;
    uint8_t stream_index;
    uint8_t format_extra[AVI_DEMUX_MAX_VIDEO_FORMAT_EXTRA];
} AviDemuxVideoInfo;

typedef struct AviDemuxAudioInfo {
    uint32_t sample_rate;
    uint32_t average_bytes_per_second;
    uint16_t format_tag;
    uint16_t channels;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint16_t extra_size;
    uint16_t samples_per_block;
    uint8_t stream_index;
} AviDemuxAudioInfo;

typedef struct AviDemuxInfo {
    AviDemuxVideoInfo video;
    AviDemuxAudioInfo audio;
    uint32_t microseconds_per_frame;
    uint32_t main_frame_count;
    long movi_start;
    long movi_end;
} AviDemuxInfo;

typedef struct AviDemuxPacket {
    uint32_t chunk_id;
    uint32_t payload_size;
    long payload_offset;
    long next_offset;
    uint8_t stream_index;
    AviDemuxPacketKind kind;
} AviDemuxPacket;

uint32_t avi_demux_fourcc(char a, char b, char c, char d);

/* Parse RIFF/AVI headers and leave file positioned at movi_start. */
int avi_demux_read_info(FILE *file, AviDemuxInfo *info);

/* Find the next packet of the requested kind from the current file cursor.
 * LIST 'rec ' nesting and unrelated streams are skipped. The cursor is left
 * at packet payload; packet.next_offset includes the RIFF padding byte. */
int avi_demux_next_packet(FILE *file, const AviDemuxInfo *info,
                          AviDemuxPacketKind kind,
                          AviDemuxPacket *packet);

int avi_demux_finish_packet(FILE *file, const AviDemuxPacket *packet);
const char *avi_demux_strerror(int result);

#ifdef __cplusplus
}
#endif

#endif
