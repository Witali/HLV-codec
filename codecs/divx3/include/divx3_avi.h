#ifndef HLV_DIVX3_AVI_H
#define HLV_DIVX3_AVI_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Divx3AviInfo {
    uint16_t width;
    uint16_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t frame_count;
    uint32_t max_video_packet_size;
    uint32_t audio_sample_rate;
    uint8_t audio_channels;
    uint8_t audio_bits_per_sample;
    uint8_t video_stream;
    uint8_t audio_stream;
    long movi_start;
    long movi_end;
} Divx3AviInfo;

enum {
    DIVX3_AVI_OK = 0,
    DIVX3_AVI_EOF = 1,
    DIVX3_AVI_ERR_ARGUMENT = -20,
    DIVX3_AVI_ERR_IO = -21,
    DIVX3_AVI_ERR_FORMAT = -22,
    DIVX3_AVI_ERR_RANGE = -23,
};

int divx3_avi_read_info(FILE *file, Divx3AviInfo *info);

/*
 * Locate the next compressed video packet and leave the file at its payload.
 * next_offset points after the packet and its optional RIFF padding byte.
 */
int divx3_avi_begin_video_packet(
    FILE *file, const Divx3AviInfo *info, uint32_t *packet_size,
    long *next_offset);

/* Seek to the next chunk after a packet opened by the function above. */
int divx3_avi_finish_video_packet(FILE *file, long next_offset);

/*
 * Read the next compressed video packet. The file must initially be at
 * info.movi_start. Non-video chunks, including PCM audio, are skipped.
 */
int divx3_avi_read_video_packet(FILE *file, const Divx3AviInfo *info,
                                uint8_t *buffer, size_t capacity,
                                size_t *packet_size);

/*
 * Locate the next PCM audio payload and leave the file positioned at it.
 * The caller consumes payload_size bytes and its optional RIFF padding byte.
 */
int divx3_avi_next_audio_chunk(FILE *file, const Divx3AviInfo *info,
                               uint32_t *payload_size);

int divx3_avi_is_v3_fourcc(uint32_t fourcc_value);
const char *divx3_avi_strerror(int result);

#ifdef __cplusplus
}
#endif

#endif
