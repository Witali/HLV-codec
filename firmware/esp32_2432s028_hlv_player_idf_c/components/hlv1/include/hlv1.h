/*
 * HLV-1 public C API.
 *
 * The library exposes a small predictive YUV 4:2:0 codec, container helpers,
 * and YUV4MPEG2 adapters.  Encoder-only search and rate-control features are
 * deliberately kept out of the bitstream so that they never increase decoder
 * complexity.  Unless stated otherwise, functions return an HLV1Result value.
 */
#ifndef HLV1_H
#define HLV1_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "compact_yuv420.h"

#ifdef __cplusplus
extern "C" {
#endif

/* HLV v15 is the current standalone format. Readers retain v14 compatibility;
 * historical syntax constants remain named for shared v14/v15 code paths. */
#define HLV1_STREAM_VERSION_1 1
#define HLV1_STREAM_VERSION_2 2
#define HLV1_STREAM_VERSION_3 3
#define HLV1_STREAM_VERSION_4 4
#define HLV1_STREAM_VERSION_5 5
#define HLV1_STREAM_VERSION_6 6
#define HLV1_STREAM_VERSION_7 7
#define HLV1_STREAM_VERSION_8 8
#define HLV1_STREAM_VERSION_9 9
#define HLV1_STREAM_VERSION_10 10
#define HLV1_STREAM_VERSION_11 11
#define HLV1_STREAM_VERSION_12 12
#define HLV1_STREAM_VERSION_13 13
#define HLV1_STREAM_VERSION_14 14
#define HLV1_STREAM_VERSION_15 15
#define HLV1_MIN_VERSION HLV1_STREAM_VERSION_14
#define HLV1_VERSION HLV1_STREAM_VERSION_15
#define HLV1_MAX_VERSION HLV1_STREAM_VERSION_15

/* Effective quantizer steps are represented as an 8-bit mantissa and a small
 * left shift.  2040 is therefore the largest stable v4+ step. */
#define HLV1_MAX_QSTEP 2040
#define HLV1_HEADER_SIZE 28
#define HLV1_FRAME_HEADER_SIZE 20

/* Optional sequence features.  Audio reuses reserved container bytes and does
 * not change the video syntax version. */
#define HLV1_FLAG_AUDIO 0x01

/* Audio samples are interleaved into the tail of each video packet. */
#define HLV1_AUDIO_NONE   0
#define HLV1_AUDIO_PCM_U8 1
#define HLV1_AUDIO_IMA_ADPCM 2

/* Frame types.  Decoding order is identical to display order; HLV-1 has no
 * B-frames or future-frame dependencies. */
#define HLV1_FRAME_KEY 0
#define HLV1_FRAME_P   1
#define HLV1_FRAME_REPEAT 2

/* Macroblock prediction modes used by the latest syntax. */
#define HLV1_MODE_SKIP        0
#define HLV1_MODE_INTER       1
#define HLV1_MODE_FILL        2
#define HLV1_MODE_INTRA_DC    3
#define HLV1_MODE_SPLIT_INTER 4
#define HLV1_MODE_GLOBAL      5
#define HLV1_MODE_PALETTE     6
#define HLV1_MODE_GRADIENT    7
#define HLV1_MODE_LITERAL     8
#define HLV1_MODE_SKIP_RUN    9
#define HLV1_MODE_SPLIT_JOINT 10
#define HLV1_MODE_RECT_SPLIT  11

/* Intra predictors.  Plane prediction uses already reconstructed top/left
 * samples, so encoder and decoder must process macroblocks in raster order. */
#define HLV1_INTRA_DC         0
#define HLV1_INTRA_VERTICAL   1
#define HLV1_INTRA_HORIZONTAL 2
#define HLV1_INTRA_PLANE      3

/** Library status and error codes. */
typedef enum HLV1Result {
    HLV1_OK = 0,
    HLV1_EOF = 1,
    HLV1_ERR_ARGUMENT = -1,
    HLV1_ERR_MEMORY = -2,
    HLV1_ERR_IO = -3,
    HLV1_ERR_FORMAT = -4,
    HLV1_ERR_CRC = -5,
    HLV1_ERR_RANGE = -6,
    HLV1_ERR_BITSTREAM = -7
} HLV1Result;

/** Sequence-level metadata serialized in the fixed 28-byte file header. */
typedef struct HLV1Header {
    uint16_t width;          /**< Visible luma width in pixels. */
    uint16_t height;         /**< Visible luma height in pixels. */
    uint16_t fps_num;        /**< Frame-rate numerator. */
    uint16_t fps_den;        /**< Frame-rate denominator. */
    uint32_t frame_count;    /**< Zero is allowed for streaming outputs. */
    uint16_t gop;            /**< Maximum distance between keyframes. */
    uint8_t quality;         /**< Informational friendly quality setting. */
    uint8_t search_radius;   /**< Informational encoder search radius. */
    uint8_t flags;           /**< HLV1_FLAG_* sequence features. */
    uint8_t version;         /**< Zero selects the current v15 syntax. */
    uint16_t audio_sample_rate; /**< Audio samples per second; zero without audio. */
    uint8_t audio_codec;     /**< HLV1_AUDIO_* value. */
    uint8_t audio_channels;  /**< Interleaved channels; PCM_U8 currently requires 1. */
} HLV1Header;

/** One compressed frame packet, excluding its on-disk 20-byte packet header. */
typedef struct HLV1Packet {
    uint8_t frame_type;      /**< HLV1_FRAME_KEY, P, or v15 REPEAT. */
    uint8_t q_y;             /**< Quantizer mantissa for luma. */
    uint8_t q_uv;            /**< Quantizer mantissa for chroma. */
    uint8_t q_shift;         /**< v4+: effective qstep = q_* << q_shift. */
    uint32_t bit_length;     /**< Number of valid bits in payload. */
    uint32_t payload_size;   /**< Allocated payload bytes. */
    uint8_t *payload;        /**< Owned buffer; release with hlv1_packet_free(). */
    uint8_t **payload_blocks; /**< Borrowed fixed-size blocks, or NULL. */
    size_t payload_block_count; /**< Number of pointers in payload_blocks. */
    size_t payload_block_size;  /**< Capacity of every borrowed block. */
} HLV1Packet;

/**
 * Padded planar YUV 4:2:0 frame.
 *
 * Width/height are visible dimensions.  Planes are padded to 16 luma pixels so
 * every 16x16 macroblock and its 8x8 chroma region can be accessed without a
 * partial-block branch.  Edge padding is generated by the Y4M reader.  Plane
 * pointers must not be assumed contiguous on fragmented-memory targets.
 */
typedef struct HLV1Frame {
    int width;
    int height;
    int padded_width;
    int padded_height;
    int stride_y;
    int stride_u;
    int stride_v;
    uint8_t *storage;        /**< Backing storage starting at Y. */
    uint8_t storage_mode;    /**< Internal ownership mode; callers leave unchanged. */
    uint8_t *y;
    uint8_t *u;
    uint8_t *v;
    int correction_stride_y;
    int correction_stride_u;
    int correction_stride_v;
    int8_t *correction_storage;
    int8_t *correction_y;
    int8_t *correction_u;
    int8_t *correction_v;
} HLV1Frame;

/* ESP-class compact frame storage. Samples are packed little-endian within
 * each byte-aligned row. A signed Q4 correction per 8x8 plane block preserves
 * the discarded local average when samples expand back to 8 bits. */
#define HLV1_FRAME_STORAGE_CONTIGUOUS 0
#define HLV1_FRAME_STORAGE_PLANAR 1
#define HLV1_FRAME_STORAGE_Y7_U6_V6 2
#define HLV1_V14_LUMA_BITS 7
#define HLV1_V14_CHROMA_BITS 6

static inline uint8_t hlv1_frame_packed_sample(const uint8_t *row,
                                                int x, unsigned bits) {
    return compact_yuv420_packed_sample(row, x, bits);
}

/* The signed correction is a Q4 block average.  A fixed 4x4 threshold map
 * distributes its fractional part without extra state, so every 8x8 block
 * retains the discarded average to 1/16 sample. */
static inline int hlv1_frame_compact_correction(const int8_t *correction,
                                                 int correction_stride,
                                                 int x, int y) {
    return compact_yuv420_correction(
        correction, correction_stride, x, y);
}

/* Expand a consecutive span without repeating bit-offset multiplication and
 * cross-byte branches for every sample.  Motion compensation and display
 * conversion use this hot path for complete 8/16-pixel blocks or scanlines. */
static inline void hlv1_frame_unpack_packed_samples(const uint8_t *row,
                                                     int x, unsigned bits,
                                                     uint8_t *output,
                                                     int count) {
    compact_yuv420_unpack_packed_samples(
        row, x, bits, output, count);
}

static inline void hlv1_frame_unpack_corrected_samples(
    const uint8_t *row, int x, int y, unsigned bits,
    const int8_t *correction, int correction_stride,
    uint8_t *output, int count) {
    compact_yuv420_unpack_corrected_samples(
        row, x, y, bits, correction, correction_stride, output, count);
}

static inline uint8_t hlv1_frame_y_sample(const HLV1Frame *frame,
                                           int x, int y) {
    const uint8_t *row = frame->y + y * frame->stride_y;
    if (frame->storage_mode != HLV1_FRAME_STORAGE_Y7_U6_V6) return row[x];
    int value = hlv1_frame_packed_sample(
                    row, x, HLV1_V14_LUMA_BITS) +
                hlv1_frame_compact_correction(
                    frame->correction_y, frame->correction_stride_y, x, y);
    return (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
}

static inline uint8_t hlv1_frame_u_sample(const HLV1Frame *frame,
                                           int x, int y) {
    const uint8_t *row = frame->u + y * frame->stride_u;
    if (frame->storage_mode != HLV1_FRAME_STORAGE_Y7_U6_V6) return row[x];
    int value = hlv1_frame_packed_sample(
                    row, x, HLV1_V14_CHROMA_BITS) +
                hlv1_frame_compact_correction(
                    frame->correction_u, frame->correction_stride_u, x, y);
    return (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
}

static inline uint8_t hlv1_frame_v_sample(const HLV1Frame *frame,
                                           int x, int y) {
    const uint8_t *row = frame->v + y * frame->stride_v;
    if (frame->storage_mode != HLV1_FRAME_STORAGE_Y7_U6_V6) return row[x];
    int value = hlv1_frame_packed_sample(
                    row, x, HLV1_V14_CHROMA_BITS) +
                hlv1_frame_compact_correction(
                    frame->correction_v, frame->correction_stride_v, x, y);
    return (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
}

/**
 * Syntax statistics and architecture-independent decoder work counters.
 *
 * Work counters intentionally measure operations rather than wall-clock time.
 * They are used to reject compression tools that would violate the project's
 * 320x240 at 25 fps target on a scalar 100 MHz processor.
 */
typedef struct HLV1Stats {
    uint64_t frames;
    uint64_t keyframes;
    uint64_t repeated_frames;
    uint64_t macroblocks;
    uint64_t skipped;
    uint64_t skip_runs;
    uint64_t inter;
    uint64_t global;
    uint64_t split_inter;
    uint64_t split_joint;
    uint64_t rect_split;
    uint64_t fill;
    uint64_t palette;
    uint64_t palette_2;
    uint64_t palette_4;
    uint64_t palette_8;
    uint64_t gradient;
    uint64_t literal;
    uint64_t intra_dc;
    uint64_t intra_vertical;
    uint64_t intra_horizontal;
    uint64_t intra_plane;
    uint64_t residual_blocks;
    uint64_t zero_residual_blocks;
    uint64_t dc_only_blocks;
    uint64_t zero_residual_macroblocks;
    uint64_t payload_bytes;
    uint64_t copied_samples;
    uint64_t interpolated_hv_samples;
    uint64_t interpolated_bilinear_samples;
    uint64_t intra_samples;
    uint64_t fill_samples;
    uint64_t palette_samples;
    uint64_t gradient_samples;
    uint64_t literal_samples;
    uint64_t coefficient_symbols;
    uint64_t single_coefficient_blocks;
    uint64_t two_coefficient_blocks;
    uint64_t run_zero_symbols;
    uint64_t unit_level_symbols;
    uint64_t inverse_wht_blocks;
    uint64_t decoded_bits;
    uint64_t motion_predictor_blocks;
    uint64_t estimated_decode_cycles;
} HLV1Stats;

/* Optional target-cycle profile, populated only when the decoder component is
 * built with HLV1_STAGE_PROFILE=ON. Release builds contain no timer reads in
 * hot loops. */
typedef struct HLV1StageProfile {
    uint64_t frames;
    uint64_t total_cycles;
    uint64_t input_cycles;
    uint64_t input_bytes;
    uint64_t input_refills;
    uint64_t crc_cycles;
    uint64_t prediction_cycles;
    uint64_t residual_cycles;
    uint64_t inverse_wht_cycles;
    uint64_t packing_cycles;
    uint64_t reference_commit_cycles;
} HLV1StageProfile;

typedef struct HLV1Encoder HLV1Encoder;
typedef struct HLV1Decoder HLV1Decoder;
typedef void (*HLV1ReferenceRowGuard)(
    void *opaque, int first_y, int rows);
enum {
    HLV1_SINGLE_REFERENCE_MAX_RADIUS = 8,
    HLV1_SINGLE_REFERENCE_LUMA_ROWS = 32
};

/** Return a stable human-readable description of an HLV1Result code. */
const char *hlv1_strerror(int result);

/* Container I/O.  Packet read verifies CRC before returning ownership of the
 * payload.  Packet write computes CRC over exactly payload_size bytes. */
int hlv1_header_write(FILE *file, const HLV1Header *header);
int hlv1_header_read(FILE *file, HLV1Header *header);
int hlv1_packet_write(FILE *file, const HLV1Packet *packet);
int hlv1_packet_header_parse(
    const uint8_t header[HLV1_FRAME_HEADER_SIZE],
    HLV1Packet *packet, uint32_t *expected_crc);
int hlv1_packet_read(FILE *file, HLV1Packet *packet);
/* Read into caller-owned, reusable fixed-size blocks.  Blocks are not freed by
 * hlv1_packet_free().  This avoids a large contiguous allocation and per-frame
 * malloc/free on fragmented-memory targets. */
int hlv1_packet_read_blocks(FILE *file, HLV1Packet *packet,
                            uint8_t **blocks, size_t block_count,
                            size_t block_size);
void hlv1_packet_free(HLV1Packet *packet);

/* Packet payload layout helpers.  Video occupies ceil(bit_length/8) bytes;
 * any remaining bytes are audio for that frame's presentation interval. */
size_t hlv1_packet_video_payload_size(const HLV1Packet *packet);
size_t hlv1_packet_audio_size(const HLV1Packet *packet);
/* Contiguous audio pointer, or NULL for segmented packets.  Use payload_span
 * to consume a segmented audio tail without copying it. */
const uint8_t *hlv1_packet_audio_data(const HLV1Packet *packet);
/* Return the contiguous span beginning at payload offset.  A segmented packet
 * may require repeated calls; zero means an invalid offset or storage. */
size_t hlv1_packet_payload_span(const HLV1Packet *packet, size_t offset,
                                const uint8_t **data);
int hlv1_packet_append_audio(HLV1Packet *packet,
                             const uint8_t *samples, size_t size);

/* Frame lifetime helpers. */
int hlv1_frame_alloc(HLV1Frame *frame, int width, int height);
void hlv1_frame_free(HLV1Frame *frame);
int hlv1_frame_copy_visible(HLV1Frame *dst, const HLV1Frame *src);

/** Create an encoder.  scene_cut is a mean-luma-difference threshold. */
HLV1Encoder *hlv1_encoder_create(const HLV1Header *header, double scene_cut);

/**
 * Deep-copy the complete predictive encoder state.
 *
 * Trial encodes for constant quality, local two-pass control, and K/P RDO must
 * begin from an identical reference frame.  Cloning prevents those trials from
 * mutating the real stream state.  The decoder and bitstream are unaffected.
 */
HLV1Encoder *hlv1_encoder_clone(const HLV1Encoder *encoder);

/* Encoder-only rate/distortion controls.  None changes decoder behavior. */
int hlv1_encoder_set_chroma_scale(HLV1Encoder *encoder, double scale);

/** Set exact quantizer steps serialized in subsequent frame packets. */
int hlv1_encoder_set_quantization(HLV1Encoder *encoder, int q_y, int q_uv);

/** Configure RDO lambda and relative luma distortion weight. */
int hlv1_encoder_set_rd_parameters(HLV1Encoder *encoder,
                                   double lambda_scale, int luma_weight);

/**
 * Add estimated decoder cycles to RDO as equivalent payload bits.
 *
 * The default value of zero preserves distortion/rate-only mode selection.
 * For example, 0.05 makes 100 estimated decoder cycles cost the same as five
 * payload bits and favours low-complexity modes over dense transform residuals.
 */
int hlv1_encoder_set_decode_cycle_weight(HLV1Encoder *encoder,
                                         double bits_per_cycle);

/** AC zero threshold in quantizer-step units; 0.5 is ordinary rounding. */
int hlv1_encoder_set_ac_deadzone(HLV1Encoder *encoder, double deadzone);

/**
 * Number of motion candidates fully tested with encoded residual RDO.
 * One reproduces the original SAD-only choice; larger values improve only the
 * encoder decision and leave syntax and decoder complexity unchanged.
 */
int hlv1_encoder_set_motion_candidates(HLV1Encoder *encoder, int candidates);

/**
 * Enable encoder-only adaptive keyframe selection.
 *
 * header.gop remains a hard maximum interval.  A zero minimum interval disables
 * adaptive decisions.  keyframe_bias >= 1 permits a K-frame only when its RDO
 * cost is no greater than P cost times the bias.
 */
int hlv1_encoder_set_adaptive_gop(HLV1Encoder *encoder,
                                  unsigned minimum_key_interval,
                                  double keyframe_bias);

void hlv1_encoder_destroy(HLV1Encoder *encoder);

/** Encode one visible YUV420 frame and optionally return its reconstruction. */
int hlv1_encoder_encode(HLV1Encoder *encoder,
                        const HLV1Frame *input,
                        HLV1Packet *packet,
                        const HLV1Frame **reconstructed);
const HLV1Stats *hlv1_encoder_stats(const HLV1Encoder *encoder);

/** Create/destroy a sequential decoder for one sequence header. */
HLV1Decoder *hlv1_decoder_create(const HLV1Header *header);

/**
 * Create the ESP-oriented decoder with packed Y7/U6/V6 4:2:0 reference
 * frames and signed Q4 average-error coefficients for every 8x8 plane tile.
 * Expanded and packed decoding reconstruct identical YUV samples.
 */
HLV1Decoder *hlv1_decoder_create_y7_u6_v6(const HLV1Header *header);
/**
 * Create the packed decoder with one complete previous-frame reference and
 * bounded rolling current rows. Streams must declare a motion search radius
 * no larger than eight pixels; every decoded vector is validated.
 */
HLV1Decoder *hlv1_decoder_create_y7_u6_v6_single_reference(
    const HLV1Header *header);
/**
 * Before progressively replacing old reference rows, call guard so a
 * concurrent renderer can finish consuming those rows.
 */
void hlv1_decoder_set_reference_row_guard(
    HLV1Decoder *decoder, HLV1ReferenceRowGuard guard, void *opaque);
void hlv1_decoder_destroy(HLV1Decoder *decoder);

/** Decode one packet.  P-frames require a successfully decoded reference. */
int hlv1_decoder_decode(HLV1Decoder *decoder,
                        const HLV1Packet *packet,
                        const HLV1Frame **frame);
/* ESP32-style segmented input path.  The packet must have been populated by
 * hlv1_packet_read_blocks(); decoding semantics and reference state match the
 * ordinary contiguous decoder. */
int hlv1_decoder_decode_blocks(HLV1Decoder *decoder,
                               const HLV1Packet *packet,
                               const HLV1Frame **frame);
/* Read, CRC-check and decode the next packet through one reusable buffer.
 * The decoder consumes video bits while the refill path advances over the
 * complete payload, so packet size is not limited by available heap. */
int hlv1_decoder_decode_file(HLV1Decoder *decoder, FILE *file,
                             uint8_t *buffer, size_t buffer_size,
                             HLV1Packet *packet_info,
                             const HLV1Frame **frame);
/* Decode a packet whose 20-byte container header has already been parsed.
 * The file cursor must point at the first payload byte.  The complete bounded
 * payload is consumed and checked against expected_crc. */
int hlv1_decoder_decode_file_packet(
    HLV1Decoder *decoder, FILE *file, uint8_t *buffer, size_t buffer_size,
    const HLV1Packet *packet, uint32_t expected_crc,
    const HLV1Frame **frame);
const HLV1Stats *hlv1_decoder_stats(const HLV1Decoder *decoder);
const HLV1StageProfile *hlv1_decoder_stage_profile(
    const HLV1Decoder *decoder);
void hlv1_decoder_stage_profile_reset(HLV1Decoder *decoder);

/** Map the user-facing 1..100 scale to stable v1/v2 quantizer mantissas. */
int hlv1_quality_to_qsteps(int quality, int *q_y, int *q_uv);

/** Minimal YUV4MPEG2 reader/writer used by the tools and tests. */
typedef struct HLV1Y4M {
    FILE *file;
    int width;
    int height;
    int fps_num;
    int fps_den;
    int writing;
} HLV1Y4M;

int hlv1_y4m_open_read(HLV1Y4M *y4m, FILE *file);
int hlv1_y4m_open_write(HLV1Y4M *y4m, FILE *file,
                        int width, int height, int fps_num, int fps_den);
int hlv1_y4m_read_frame(HLV1Y4M *y4m, HLV1Frame *frame);
int hlv1_y4m_write_frame(HLV1Y4M *y4m, const HLV1Frame *frame);

#ifdef __cplusplus
}
#endif
#endif
