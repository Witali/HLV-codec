#ifndef H263_3GP_H
#define H263_3GP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "compact_yuv420.h"
#include "h263_decode_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    H263_3GP_OK = 0,
    H263_3GP_EOF = 1,
    H263_3GP_ERR_ARGUMENT = -1,
    H263_3GP_ERR_IO = -2,
    H263_3GP_ERR_FORMAT = -3,
    H263_3GP_ERR_UNSUPPORTED = -4,
    H263_3GP_ERR_MEMORY = -5,
    H263_3GP_ERR_DECODE = -6,
    H263_3GP_ERR_FRAME_MEMORY = -7,
    H263_3GP_ERR_DECODER_MEMORY = -8,
    H263_3GP_ERR_PACKET_MEMORY = -9,
};

enum {
    H263_CONTAINER_UNKNOWN = 0,
    H263_CONTAINER_3GP = 1,
    H263_CONTAINER_AVI = 2,
};

enum {
    H263_VIDEO_CODEC_UNKNOWN = 0,
    H263_VIDEO_CODEC_H263 = 1,
    H263_VIDEO_CODEC_MPEG4_SIMPLE = 2,
};

enum {
    H263_FRAME_STORAGE_YUV420 = 0,
    H263_FRAME_STORAGE_Y6_U5_V5 = 1,
};

#define H263_AVI_PCM_MAX_SAMPLES 256

typedef struct H2633gpInfo {
    uint16_t width;
    uint16_t height;
    uint32_t timescale;
    uint64_t duration_ticks;
    uint32_t frame_count;
    uint32_t max_sample_size;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t audio_sample_rate;
    uint8_t profile;
    uint8_t level;
    uint8_t container;
    uint8_t audio_channels;
    uint8_t audio_bits_per_sample;
    uint8_t video_codec;
} H2633gpInfo;

typedef struct H2633gpFrame {
    const uint8_t *y;
    const uint8_t *u;
    const uint8_t *v;
    uint16_t width;
    uint16_t height;
    uint16_t y_stride;
    uint16_t chroma_stride;
    uint8_t storage_mode;
    CompactYuv420Frame compact;
    uint64_t timestamp_ticks;
    uint32_t duration_ticks;
    uint32_t index;
} H2633gpFrame;

typedef struct H2633gpDecoder H2633gpDecoder;
typedef struct H263AviPcmReader H263AviPcmReader;
typedef void (*H263OutputRowGuard)(void *opaque, uint16_t first_y);

typedef struct H263AviPcmFrame {
    uint8_t samples[H263_AVI_PCM_MAX_SAMPLES];
    uint16_t sample_count;
} H263AviPcmFrame;

H2633gpDecoder *h263_3gp_decoder_create(void);
void h263_3gp_decoder_destroy(H2633gpDecoder *decoder);

/*
 * Requests one or two ordinary output frame buffers. Two buffers allow a
 * caller to render frame N while frame N+1 is decoded. Predictive QCIF H.263
 * always uses two byte-planar buffers. MPEG-4 with a request of one instead
 * returns two pointer-swapped compact Y6/U5/V5 frames and reconstructs
 * through one 16-luma-row workspace. Call before open().
 */
int h263_3gp_decoder_set_output_buffer_count(H2633gpDecoder *decoder,
                                              uint8_t count);
uint8_t h263_3gp_decoder_output_buffer_count(
    const H2633gpDecoder *decoder);

/*
 * Installs an optional callback before each 16-pixel output row is written.
 * This supports safe overlap between rendering and intra-frame decoding when
 * only one output frame fits in memory. Call after open(); pass NULL to clear.
 */
void h263_3gp_decoder_set_output_row_guard(
    H2633gpDecoder *decoder, H263OutputRowGuard guard, void *opaque);

/*
 * Opens the first supported video track. H.263 is accepted in 3GP or AVI;
 * MPEG-4 Part 2 Simple Profile is accepted in AVI with the M4S2 FourCC.
 * The embedded H.263 profiles accept only 176x144 QCIF and intra-only
 * baseline 352x288 CIF; all H.263+ custom-size modes are rejected. MPEG-4
 * Simple Profile uses 320x240 I/P pictures; its embedded path retains two
 * compact predictive/display frames rather than two full byte-planar
 * buffers. 3GP audio is handled by the companion AMR-NB decoder; AVI accepts
 * mono PCM at 8 kHz through the reader below.
 */
int h263_3gp_decoder_open(H2633gpDecoder *decoder, FILE *file,
                          H2633gpInfo *info);
int h263_3gp_decoder_decode_next(H2633gpDecoder *decoder, FILE *file,
                                 H2633gpFrame *frame);

size_t h263_3gp_decoder_memory_bytes(const H2633gpDecoder *decoder);
const H263DecodeProfile *h263_3gp_decoder_decode_profile(
    const H2633gpDecoder *decoder);
void h263_3gp_decoder_decode_profile_reset(H2633gpDecoder *decoder);
const char *h263_3gp_strerror(int result);
const char *h263_3gp_codec_strerror(int codec, int result);

/*
 * Probe the bounded H.263 or MPEG-4 Simple Profile AVI profile without
 * allocating video buffers.
 */
int h263_avi_probe(FILE *file, H2633gpInfo *info);

/*
 * Stream mono AVI PCM through an independent FILE cursor. Signed 16-bit
 * little-endian samples are converted to unsigned 8-bit samples for the
 * project players; unsigned 8-bit PCM is passed through.
 */
H263AviPcmReader *h263_avi_pcm_reader_create(void);
void h263_avi_pcm_reader_destroy(H263AviPcmReader *reader);
int h263_avi_pcm_reader_open(H263AviPcmReader *reader, FILE *file,
                             H2633gpInfo *info);
/*
 * Reuse AVI stream metadata from an already-open video decoder. This avoids
 * rereading the RIFF header through a second FILE cursor on slow media.
 */
int h263_avi_pcm_reader_open_from_decoder(
    H263AviPcmReader *reader, const H2633gpDecoder *decoder,
    H2633gpInfo *info);
int h263_avi_pcm_reader_decode_next(H263AviPcmReader *reader, FILE *file,
                                    H263AviPcmFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
