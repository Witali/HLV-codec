#ifndef H263_DECODE_PROFILE_H
#define H263_DECODE_PROFILE_H

#include <stdint.h>

typedef struct H263DecodeProfile {
    uint64_t total_cycles;
    uint64_t header_cycles;
    uint64_t body_cycles;
    uint64_t input_cycles;
    uint64_t motion_vector_cycles;
    uint64_t motion_comp_cycles;
    uint64_t vlc_dequant_cycles;
    uint64_t idct_cycles;
    uint64_t packing_cycles;
    uint64_t compact_copy_cycles;
    uint32_t frames;
    uint32_t i_frames;
    uint32_t p_frames;
    uint32_t input_refills;
    uint32_t input_bytes;
    uint32_t macroblocks;
    uint32_t skipped_macroblocks;
    uint32_t intra_macroblocks;
    uint32_t inter_macroblocks;
    uint32_t cbp_zero_macroblocks;
    uint32_t one_vector_macroblocks;
    uint32_t four_vector_macroblocks;
    uint32_t coded_blocks;
    uint32_t dc_only_blocks;
    uint32_t sparse_blocks;
    uint32_t dense_blocks;
    uint32_t one_row_blocks;
    uint32_t one_column_blocks;
    uint32_t two_column_blocks;
    uint32_t compact_copy_calls;
    uint32_t compact_prediction8_calls;
    uint32_t compact_prediction16_calls;
    uint32_t compact_integer_predictions;
    uint32_t compact_horizontal_predictions;
    uint32_t compact_vertical_predictions;
    uint32_t compact_diagonal_predictions;
    uint32_t compact_edge_predictions;
} H263DecodeProfile;

#endif
