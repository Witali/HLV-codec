#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "mjpeg_huffman_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

enum mjpeg_avi_result {
    MJPEG_AVI_OK = 0,
    MJPEG_AVI_EOF = 1,
    MJPEG_AVI_ERR_ARGUMENT = -1,
    MJPEG_AVI_ERR_MEMORY = -2,
    MJPEG_AVI_ERR_IO = -3,
    MJPEG_AVI_ERR_FORMAT = -4,
    MJPEG_AVI_ERR_RANGE = -5,
    MJPEG_AVI_ERR_DECODE = -6,
};

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t frame_count;
    uint32_t audio_sample_rate;
    uint32_t max_video_frame_size;
    uint16_t audio_format_tag;
    uint16_t audio_block_align;
    uint16_t audio_samples_per_block;
    uint8_t audio_channels;
    uint8_t audio_bits_per_sample;
    uint8_t video_stream;
    uint8_t audio_stream;
    long movi_start;
    long movi_end;
} mjpeg_avi_info_t;

typedef struct {
    const uint8_t *jpeg;
    size_t jpeg_size;
    FILE *file;
    long payload_offset;
    long next_offset;
} mjpeg_avi_packet_t;

typedef struct {
    uint32_t input;
    uint32_t parse_header;
    uint32_t geometry;
    uint32_t process;
} mjpeg_avi_decode_cycles_t;

typedef bool (*mjpeg_avi_strip_output_t)(
    void *context, const uint16_t *rgb565, uint16_t y, uint16_t rows);
typedef uint16_t *(*mjpeg_avi_strip_acquire_t)(
    void *context, uint16_t y, uint16_t rows);

#define MJPEG_AVI_STRIP_ROWS 16U

typedef struct {
    mjpeg_avi_info_t info;
    uint8_t *compressed;
    size_t compressed_capacity;
    uint16_t *strip;
    void *decoder;
    uint32_t packet_index;
    long packet_offset;
    FILE *stream_file;
    uint32_t stream_remaining;
    mjpeg_huffman_stream_t entropy_stream;
    uint16_t decode_height;
    bool stream_failed;
    bool need_strip;
    mjpeg_avi_decode_cycles_t last_decode_cycles;
} mjpeg_avi_decoder_t;

const char *mjpeg_avi_strerror(int result);
int mjpeg_avi_read_info(FILE *file, mjpeg_avi_info_t *info);

/*
 * Find the next audio chunk and leave the file positioned at its
 * payload. The caller reads exactly payload_size bytes and consumes one
 * padding byte when payload_size is odd.
 */
int mjpeg_avi_next_audio_chunk(FILE *file,
                               const mjpeg_avi_info_t *info,
                               uint32_t *payload_size);

int mjpeg_avi_decoder_begin(mjpeg_avi_decoder_t *decoder,
                            FILE *file,
                            mjpeg_avi_info_t *info,
                            bool need_strip);
void mjpeg_avi_decoder_end(mjpeg_avi_decoder_t *decoder);
bool mjpeg_avi_decoder_ready(const mjpeg_avi_decoder_t *decoder);
const mjpeg_avi_info_t *mjpeg_avi_decoder_info(
    const mjpeg_avi_decoder_t *decoder);
size_t mjpeg_avi_decoder_compressed_capacity(
    const mjpeg_avi_decoder_t *decoder);
size_t mjpeg_avi_decoder_input_buffer_bytes(
    const mjpeg_avi_decoder_t *decoder);
long mjpeg_avi_decoder_last_packet_offset(
    const mjpeg_avi_decoder_t *decoder);
const mjpeg_avi_decode_cycles_t *mjpeg_avi_decoder_last_decode_cycles(
    const mjpeg_avi_decoder_t *decoder);
size_t mjpeg_avi_decoder_strip_buffer_bytes(
    const mjpeg_avi_decoder_t *decoder);
int mjpeg_avi_decoder_read_packet(mjpeg_avi_decoder_t *decoder,
                                  FILE *file,
                                  mjpeg_avi_packet_t *packet);
int mjpeg_avi_decoder_skip_packet(const mjpeg_avi_packet_t *packet);
int mjpeg_avi_decoder_decode(mjpeg_avi_decoder_t *decoder,
                             const mjpeg_avi_packet_t *packet,
                             mjpeg_avi_strip_output_t output,
                             void *output_context);
int mjpeg_avi_decoder_decode_direct(
    mjpeg_avi_decoder_t *decoder,
    const mjpeg_avi_packet_t *packet,
    mjpeg_avi_strip_acquire_t acquire,
    mjpeg_avi_strip_output_t output,
    void *output_context);

#ifdef __cplusplus
}
#endif
